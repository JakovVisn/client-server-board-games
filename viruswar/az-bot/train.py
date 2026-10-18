"""
AlphaZero training loop for VirusWar.

Each iteration:
  1. Self-play  — N_SELF_PLAY games with MCTS, run in parallel worker processes
  2. Value check — the net's value head on the fresh (not yet trained-on) games
  3. Buffer     — keep a random POSITION_KEEP fraction of each game's positions
  4. Train      — augmented mini-batches from the replay buffer; the number of
                  steps follows the amount of new data (SAMPLE_REUSE)
  5. Pit        — every EVAL_EVERY iters, evaluate net vs previous best (parallel)
  6. Promote    — save to promoted/ if win rate >= PROMOTE_THRESHOLD

Auto-resumes from the latest checkpoint in --models-dir if no --load given,
restoring the replay buffer and optimizer from train_state.pt when it matches
that checkpoint. The pit opponent is always the newest promoted model.
Saves an untrained seed model to promoted/ on first run if none exists.

Usage:
    cd viruswar/az-bot
    ../.venv/bin/python train.py
    ../.venv/bin/python train.py --load models/checkpoints/az_0000001000.pt
"""

import argparse
import copy
import logging
import math
import os
import random
import signal
import time
from pathlib import Path

import numpy as np
import torch
import torch.nn.functional as F
from dotenv import load_dotenv

load_dotenv()

from diagnostics import policy_metrics, value_dataset, value_metrics
from log import setup as setup_logging
from net import AZNet
from parallel import NetStore, make_executor, run_pit, run_self_play
from symmetry import augment, check_symmetries

# ── Hyperparameters (can be overridden via .env) ──────────────────────────────

N_WORKERS         = int(os.getenv("AZ_WORKERS",            str(max(1, (os.cpu_count() or 2) - 2))))

N_SELF_PLAY       = int(os.getenv("AZ_SELF_PLAY_GAMES",    "40"))
N_SIMS            = int(os.getenv("AZ_N_SIMS",             "200"))
TEMPERATURE_MOVES = int(os.getenv("AZ_TEMPERATURE_MOVES",  "20"))

# Consecutive positions of a game are near-identical boards sharing one outcome,
# so the value head can memorise "which game is this" instead of learning to
# evaluate (AlphaGo hit the same problem and trained on one position per game).
# Keeping a random fraction per game decorrelates the buffer.
POSITION_KEEP     = float(os.getenv("AZ_POSITION_KEEP",    "0.25"))
# Training steps per iteration = kept_positions * SAMPLE_REUSE / BATCH_SIZE,
# i.e. each kept position is sampled ~SAMPLE_REUSE times while in the buffer.
# The first run trained ~20x per position and memorised its games.
SAMPLE_REUSE      = float(os.getenv("AZ_SAMPLE_REUSE",     "3"))
MIN_BUFFER        = int(os.getenv("AZ_MIN_BUFFER",         "10000"))
BATCH_SIZE        = int(os.getenv("AZ_BATCH_SIZE",         "256"))
BUFFER_SIZE       = int(os.getenv("AZ_BUFFER_SIZE",        "200000"))
LR                = float(os.getenv("AZ_LR",               "1e-3"))
WEIGHT_DECAY      = float(os.getenv("AZ_WEIGHT_DECAY",     "1e-4"))
VALUE_LOSS_WEIGHT = float(os.getenv("AZ_VALUE_LOSS_WEIGHT","0.25"))

EVAL_EVERY        = int(os.getenv("AZ_EVAL_EVERY",         "5"))
N_PIT_GAMES       = int(os.getenv("AZ_PIT_GAMES",          "100"))
N_SIMS_PIT        = int(os.getenv("AZ_N_SIMS_PIT",         "100"))
PIT_OPENING_MOVES = int(os.getenv("AZ_PIT_OPENING_MOVES",  "6"))
PROMOTE_THRESHOLD = float(os.getenv("AZ_PROMOTE_THRESHOLD","0.55"))
VALUE_CHECK_GAMES = int(os.getenv("AZ_VALUE_CHECK_GAMES",  "100"))

SAVE_EVERY        = int(os.getenv("AZ_SAVE_EVERY",         "5"))

# ─────────────────────────────────────────────────────────────────────────────

log = logging.getLogger(__name__)

_stop = False


def _handle_signal(sig, frame):
    global _stop
    log.info("stop_requested when=after_this_iteration")
    _stop = True


def _latest_checkpoint(models_dir: str) -> str | None:
    d = Path(models_dir)
    if not d.exists():
        return None
    pts = sorted(d.glob("az_*.pt"), key=lambda p: p.stem, reverse=True)
    return str(pts[0]) if pts else None


def _games_in_name(path: str) -> int:
    """Games count encoded in az_NNNNNNNNNN.pt / promoted_NNNNNNNNNN.pt."""
    try:
        return int(Path(path).stem.split("_")[-1])
    except ValueError:
        return 0


def _save_state(path: str, games: int, optimizer: torch.optim.Optimizer, buffer: list) -> None:
    """
    Replay buffer + optimizer, so a restart does not re-warm the buffer.

    Observations are stored as cell codes (argmax of the 5 one-hot channels)
    plus moves remaining, ~25x smaller than the float planes; decoding is exact.
    """
    if not buffer:
        return
    state = {
        "games":     games,
        "optimizer": optimizer.state_dict(),
        "cells":     torch.from_numpy(np.stack([ex[0][:5].argmax(axis=0) for ex in buffer])
                                      .astype(np.uint8)),
        "moves":     torch.tensor([round(float(ex[0][5, 0, 0]) * 3) for ex in buffer],
                                  dtype=torch.uint8),
        "pi":        torch.from_numpy(np.stack([ex[1] for ex in buffer])),
        "z":         torch.tensor([ex[2] for ex in buffer], dtype=torch.float32),
    }
    tmp = path + ".tmp"
    torch.save(state, tmp)
    os.replace(tmp, path)  # never leave a half-written state behind


def _load_state(path: str, games: int, optimizer: torch.optim.Optimizer) -> list | None:
    """Restore what _save_state wrote, or None if it is missing or from another checkpoint."""
    if not os.path.exists(path):
        return None
    state = torch.load(path, map_location="cpu", weights_only=True)
    if state["games"] != games:
        return None
    # Keep this run's lr / weight decay (.env may have changed) over the saved ones
    hyper = [{k: g[k] for k in ("lr", "weight_decay")} for g in optimizer.param_groups]
    optimizer.load_state_dict(state["optimizer"])
    for g, h in zip(optimizer.param_groups, hyper):
        g.update(h)
    cells, moves = state["cells"].numpy(), state["moves"].numpy()
    pi, z = state["pi"].numpy(), state["z"].numpy()
    buffer = []
    for i in range(len(z)):
        obs = np.zeros((6, *cells.shape[1:]), dtype=np.float32)
        for c in range(5):
            obs[c] = cells[i] == c
        obs[5] = moves[i] / 3.0  # same expression as board_to_obs
        buffer.append((obs, pi[i], float(z[i])))
    return buffer


def _promote(net: AZNet, step: int, win_rate: float, promoted_dir: str) -> bool:
    """
    Save net as promoted_<step>.pt. Refused when that name already exists or is not the
    newest (possible after resuming from an older checkpoint): it would overwrite a model,
    or sort as older so play.py's "latest" and the pit would never pick it.
    """
    os.makedirs(promoted_dir, exist_ok=True)
    path = os.path.join(promoted_dir, f"promoted_{step:010d}.pt")
    newest = max((_games_in_name(str(p)) for p in Path(promoted_dir).glob("promoted_*.pt")),
                 default=-1)
    if step <= newest:
        log.warning(f"promote_refused games={step} newest_promoted={newest} reason=not_newer")
        return False
    net.save(path)
    log.info(f"promote games={step} win_rate={win_rate:.1%} path={path}")
    return True


def _sample_batch(buffer: list, batch_size: int,
                  rng: np.random.Generator) -> tuple[torch.Tensor, torch.Tensor, torch.Tensor]:
    """Random mini-batch, each sample under a random board symmetry."""
    batch = random.sample(buffer, min(batch_size, len(buffer)))
    pairs = [augment(obs, pi, rng) for obs, pi, _ in batch]
    obs = torch.from_numpy(np.stack([p[0] for p in pairs]))
    pi  = torch.from_numpy(np.stack([p[1] for p in pairs]))
    z   = torch.tensor([b[2] for b in batch], dtype=torch.float32)
    return obs, pi, z


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--load",         default=None,
                        help="Path to a .pt checkpoint to load")
    parser.add_argument("--models-dir",   default="models/checkpoints",
                        help="Directory for checkpoint saves")
    parser.add_argument("--promoted-dir", default="models/promoted",
                        help="Directory for promoted models (read by play.py)")
    parser.add_argument("--log",          default="models/train_az.log",
                        help="Log file (events, one line each)")
    args = parser.parse_args()

    os.makedirs(args.models_dir, exist_ok=True)
    setup_logging("aztrain", args.log)

    signal.signal(signal.SIGINT,  _handle_signal)
    signal.signal(signal.SIGTERM, _handle_signal)

    try:
        _train(args)
    except Exception:
        log.exception("crash")
        raise


def _train(args) -> None:
    rng = np.random.default_rng()

    # ── Sanity checks ─────────────────────────────────────────────────────────
    mismatches = check_symmetries(20, random.Random(0))
    log.info(f"symmetry_check mismatches={mismatches}")
    if mismatches:
        raise RuntimeError(f"board symmetries break the rules ({mismatches} mismatches); "
                           "augmentation would corrupt training data")
    value_data = value_dataset(VALUE_CHECK_GAMES, random.Random(0))

    # ── Load or initialise network ────────────────────────────────────────────
    load_path = args.load or _latest_checkpoint(args.models_dir)
    if load_path:
        net = AZNet.load(load_path)
    else:
        net = AZNet()

    total_games = _games_in_name(load_path) if load_path else 0

    optimizer = torch.optim.Adam(net.parameters(), lr=LR, weight_decay=WEIGHT_DECAY)
    # Plain list, not deque: random.sample indexes into the middle, which is O(n) on a deque
    state_path = os.path.join(args.models_dir, "train_state.pt")
    replay_buffer = (_load_state(state_path, total_games, optimizer) if load_path else None) or []
    log.info(f"resume_state buffer={len(replay_buffer)}"
             f" optimizer={'restored' if replay_buffer else 'fresh'}")

    # Seed promoted/ with the untrained model if the folder is empty
    promoted_path = Path(args.promoted_dir)
    if not any(promoted_path.glob("promoted_*.pt")) if promoted_path.exists() else True:
        _promote(net, total_games, 0.0, args.promoted_dir)

    # best_net is the snapshot we pit against: the newest promoted model, which on a
    # resume can be stronger than the checkpoint we continue from.
    # It only ever runs inference, so keep it in eval mode (BatchNorm running stats).
    best_file  = max(promoted_path.glob("promoted_*.pt"), key=lambda p: p.stem)
    best_net   = AZNet.load(str(best_file)).eval()
    best_games = _games_in_name(str(best_file))

    log.info(
        f"session_start loaded={load_path or 'scratch'} best={best_file.name}"
        f" workers={N_WORKERS}"
        f" self_play={N_SELF_PLAY} n_sims={N_SIMS} temp_moves={TEMPERATURE_MOVES}"
        f" position_keep={POSITION_KEEP} sample_reuse={SAMPLE_REUSE}"
        f" min_buffer={MIN_BUFFER} batch={BATCH_SIZE} buffer={BUFFER_SIZE}"
        f" lr={LR} weight_decay={WEIGHT_DECAY} value_loss_weight={VALUE_LOSS_WEIGHT}"
        f" eval_every={EVAL_EVERY} pit_games={N_PIT_GAMES} pit_sims={N_SIMS_PIT}"
        f" pit_opening={PIT_OPENING_MOVES} promote={PROMOTE_THRESHOLD}"
        f" value_check_positions={len(value_data[1])} save_every={SAVE_EVERY}"
    )
    net.eval()
    log.info(f"value_random iter=0 {value_metrics(net, *value_data)[0]}")

    store = NetStore()
    executor = make_executor(N_WORKERS)
    try:
        best_path = store.save(best_net, "best")
        iteration = 0
        while not _stop:
            iteration += 1

            # ── 1. Self-play ──────────────────────────────────────────────────
            net.eval()
            net_path = store.save(net, "latest")

            def on_self_play_game(i: int, n_pos: int, winner: int, secs: float) -> None:
                log.info(f"self_play_game iter={iteration} game={i + 1}/{N_SELF_PLAY}"
                         f" positions={n_pos} winner=P{winner} secs={secs:.1f}")

            t0 = time.perf_counter()
            games = run_self_play(executor, net_path, N_SELF_PLAY, N_SIMS, TEMPERATURE_MOVES,
                                  on_game=on_self_play_game)
            sp_secs = time.perf_counter() - t0
            total_games += N_SELF_PLAY

            examples = [ex for game_examples, _ in games for ex in game_examples]
            lengths = [len(game_examples) for game_examples, _ in games]
            p0_wins = sum(1 for _, winner in games if winner == 0)
            log.info(
                f"self_play iter={iteration} games={N_SELF_PLAY} positions={len(examples)}"
                f" len_mean={np.mean(lengths):.0f} len_min={min(lengths)} len_max={max(lengths)}"
                f" p0_wins={p0_wins} p1_wins={N_SELF_PLAY - p0_wins}"
                f" secs={sp_secs:.0f} games_per_min={N_SELF_PLAY / sp_secs * 60:.1f}"
                f" sims_per_sec={len(examples) * N_SIMS / sp_secs:.0f}"
            )

            # ── 2. Value check on games the net has not trained on yet ────────
            fresh_obs = torch.from_numpy(np.stack([ex[0] for ex in examples]))
            fresh_z = np.array([ex[2] for ex in examples])
            fresh_phase = np.concatenate([np.arange(n) / n for n in lengths])
            fresh_line, fresh_mse = value_metrics(net, fresh_obs, fresh_z, fresh_phase)
            log.info(f"value_fresh iter={iteration} {fresh_line}")
            fresh_pi = np.stack([ex[1] for ex in examples])
            log.info(f"policy_fresh iter={iteration} {policy_metrics(net, fresh_obs, fresh_pi)}")

            # ── 3. Buffer: keep a random fraction of every game's positions ───
            kept = [ex for ex in examples if rng.random() < POSITION_KEEP]
            replay_buffer.extend(kept)
            if len(replay_buffer) > BUFFER_SIZE:
                del replay_buffer[:len(replay_buffer) - BUFFER_SIZE]

            if len(replay_buffer) < MIN_BUFFER:
                log.info(f"skip_train iter={iteration} buffer={len(replay_buffer)}"
                         f" min_buffer={MIN_BUFFER}")
                continue

            # ── 4. Train ──────────────────────────────────────────────────────
            n_steps = max(1, math.ceil(len(kept) * SAMPLE_REUSE / BATCH_SIZE))
            net.train()
            total_loss = total_ploss = total_vloss = 0.0

            t0 = time.perf_counter()
            for _ in range(n_steps):
                obs, pi, z = _sample_batch(replay_buffer, BATCH_SIZE, rng)
                log_p, v = net(obs)
                policy_loss = -(pi * log_p).sum(dim=-1).mean()
                value_loss  = F.mse_loss(v.squeeze(-1), z)
                loss        = policy_loss + VALUE_LOSS_WEIGHT * value_loss

                optimizer.zero_grad()
                loss.backward()
                optimizer.step()

                total_loss  += loss.item()
                total_ploss += policy_loss.item()
                total_vloss += value_loss.item()
            train_secs = time.perf_counter() - t0

            avg_loss  = total_loss  / n_steps
            avg_ploss = total_ploss / n_steps
            avg_vloss = total_vloss / n_steps
            # v_loss (games already trained on) far below fresh_v_mse (this
            # iteration's games, measured before training) = memorising outcomes.
            log.info(
                f"iteration iter={iteration} games={total_games}"
                f" loss={avg_loss:.4f} p_loss={avg_ploss:.4f} v_loss={avg_vloss:.4f}"
                f" fresh_v_mse={fresh_mse:.4f} gap={fresh_mse - avg_vloss:+.4f}"
                f" steps={n_steps} kept={len(kept)}/{len(examples)}"
                f" buffer={len(replay_buffer)} train_secs={train_secs:.0f}"
            )

            # ── 5. Save checkpoint ────────────────────────────────────────────
            if iteration % SAVE_EVERY == 0:
                ckpt = os.path.join(args.models_dir, f"az_{total_games:010d}.pt")
                net.save(ckpt)
                _save_state(state_path, total_games, optimizer, replay_buffer)
                log.info(f"checkpoint games={total_games} path={ckpt} buffer={len(replay_buffer)}")

            # ── 6. Pit vs best ────────────────────────────────────────────────
            if iteration % EVAL_EVERY == 0:
                net.eval()
                log.info(f"value_random iter={iteration} {value_metrics(net, *value_data)[0]}")
                log.info(f"pit_start vs_games={best_games} games={N_PIT_GAMES} sims={N_SIMS_PIT}")

                def on_pit_game(pair: int, a_player: int, winner: int,
                                sub_moves: int, secs: float) -> None:
                    result = "new" if winner == a_player else "best"
                    log.info(f"pit_game pair={pair + 1} new_as=P{a_player} winner=P{winner}"
                             f" result={result} sub_moves={sub_moves} secs={secs:.1f}")

                t0 = time.perf_counter()
                wr = run_pit(executor, store.save(net, "latest"), best_path,
                             N_PIT_GAMES, N_SIMS_PIT, opening_moves=PIT_OPENING_MOVES,
                             on_game=on_pit_game)
                pit_secs = time.perf_counter() - t0
                passed = wr >= PROMOTE_THRESHOLD  # promote (or promote_refused) follows
                log.info(
                    f"pit games={total_games} win_rate={wr:.1%}"
                    f" vs_games={best_games} threshold={PROMOTE_THRESHOLD:.0%}"
                    f" passed={'yes' if passed else 'no'} secs={pit_secs:.0f}"
                )

                if passed and _promote(net, total_games, wr, args.promoted_dir):
                    best_net   = copy.deepcopy(net).eval()
                    best_games = total_games
                    best_path  = store.save(best_net, "best")
    finally:
        executor.shutdown(cancel_futures=True)
        store.cleanup()

    # ── Shutdown ──────────────────────────────────────────────────────────────
    ckpt = os.path.join(args.models_dir, f"az_{total_games:010d}.pt")
    net.save(ckpt)
    _save_state(state_path, total_games, optimizer, replay_buffer)
    log.info(f"session_end games={total_games} path={ckpt}")


if __name__ == "__main__":
    main()
