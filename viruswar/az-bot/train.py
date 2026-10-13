"""
AlphaZero training loop for VirusWar.

Each iteration:
  1. Self-play  — N_SELF_PLAY games with MCTS, run in parallel worker processes
  2. Value check — the net's value head on the fresh (not yet trained-on) games
  3. Train      — augmented mini-batches from the replay buffer; the number of
                  steps follows the amount of new data (SAMPLE_REUSE)
  4. Pit        — every EVAL_EVERY iters, evaluate net vs previous best (parallel)
  5. Promote    — save to promoted/ if win rate >= PROMOTE_THRESHOLD

Auto-resumes from the latest checkpoint in --models-dir if no --load given.
Saves an untrained seed model to promoted/ on first run if none exists.

Usage:
    cd viruswar/az-bot
    ../.venv/bin/python train.py
    ../.venv/bin/python train.py --load models/checkpoints/az_0000001000.pt
"""

import argparse
import copy
import math
import os
import random
import signal
import time
import traceback
from pathlib import Path

import numpy as np
import torch
import torch.nn.functional as F
from dotenv import load_dotenv

load_dotenv()

from diagnostics import value_dataset, value_metrics
from net import AZNet
from parallel import NetStore, make_executor, run_pit, run_self_play
from symmetry import augment, check_symmetries
from training_logger import TrainingLogger

# ── Hyperparameters (can be overridden via .env) ──────────────────────────────

N_WORKERS         = int(os.getenv("AZ_WORKERS",            str(max(1, (os.cpu_count() or 2) - 2))))

N_SELF_PLAY       = int(os.getenv("AZ_SELF_PLAY_GAMES",    "40"))
N_SIMS            = int(os.getenv("AZ_N_SIMS",             "200"))
TEMPERATURE_MOVES = int(os.getenv("AZ_TEMPERATURE_MOVES",  "20"))

# Training steps per iteration = new_positions * SAMPLE_REUSE / BATCH_SIZE,
# i.e. each position is sampled ~SAMPLE_REUSE times while in the buffer.
# The first run trained ~20x per position and memorised its games.
SAMPLE_REUSE      = float(os.getenv("AZ_SAMPLE_REUSE",     "3"))
MIN_BUFFER        = int(os.getenv("AZ_MIN_BUFFER",         "20000"))
BATCH_SIZE        = int(os.getenv("AZ_BATCH_SIZE",         "256"))
BUFFER_SIZE       = int(os.getenv("AZ_BUFFER_SIZE",        "200000"))
LR                = float(os.getenv("AZ_LR",               "1e-3"))
WEIGHT_DECAY      = float(os.getenv("AZ_WEIGHT_DECAY",     "1e-4"))

EVAL_EVERY        = int(os.getenv("AZ_EVAL_EVERY",         "5"))
N_PIT_GAMES       = int(os.getenv("AZ_PIT_GAMES",          "100"))
N_SIMS_PIT        = int(os.getenv("AZ_N_SIMS_PIT",         "100"))
PIT_OPENING_MOVES = int(os.getenv("AZ_PIT_OPENING_MOVES",  "6"))
PROMOTE_THRESHOLD = float(os.getenv("AZ_PROMOTE_THRESHOLD","0.55"))
VALUE_CHECK_GAMES = int(os.getenv("AZ_VALUE_CHECK_GAMES",  "100"))

SAVE_EVERY        = int(os.getenv("AZ_SAVE_EVERY",         "5"))

# ─────────────────────────────────────────────────────────────────────────────

_stop = False


def _handle_signal(sig, frame):
    global _stop
    print("\n[train] Stopping after current iteration...")
    _stop = True


def _latest_checkpoint(models_dir: str) -> str | None:
    d = Path(models_dir)
    if not d.exists():
        return None
    pts = sorted(d.glob("az_*.pt"), key=lambda p: p.stem, reverse=True)
    return str(pts[0]) if pts else None


def _promote(net: AZNet, step: int, win_rate: float,
             promoted_dir: str, logger: TrainingLogger) -> None:
    os.makedirs(promoted_dir, exist_ok=True)
    path = os.path.join(promoted_dir, f"promoted_{step:010d}.pt")
    net.save(path)
    logger.log_promotion(step, win_rate, path)
    print(f"[train] promoted → {path}")


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
    parser.add_argument("--log",          default="models/training_log_az.md",
                        help="Training log file")
    args = parser.parse_args()

    os.makedirs(args.models_dir, exist_ok=True)
    logger = TrainingLogger(args.log)

    signal.signal(signal.SIGINT,  _handle_signal)
    signal.signal(signal.SIGTERM, _handle_signal)

    try:
        _train(args, logger)
    except Exception:
        logger.log_line("train crash\n\n```\n" + traceback.format_exc() + "```\n")
        raise


def _train(args, logger: TrainingLogger) -> None:
    rng = np.random.default_rng()

    # ── Sanity checks ─────────────────────────────────────────────────────────
    mismatches = check_symmetries(20, random.Random(0))
    logger.log_line(f"train symmetry_check  mismatches={mismatches}")
    if mismatches:
        raise RuntimeError(f"board symmetries break the rules ({mismatches} mismatches); "
                           "augmentation would corrupt training data")
    value_data = value_dataset(VALUE_CHECK_GAMES, random.Random(0))

    # ── Load or initialise network ────────────────────────────────────────────
    load_path = args.load or _latest_checkpoint(args.models_dir)
    if load_path:
        net = AZNet.load(load_path)
        print(f"[train] resumed from {load_path}")
    else:
        net = AZNet()
        print("[train] starting from scratch")

    # Infer total_games from checkpoint filename (az_NNNNNNNNNN.pt)
    total_games = 0
    if load_path:
        try:
            total_games = int(Path(load_path).stem.split("_")[-1])
        except ValueError:
            pass

    optimizer = torch.optim.Adam(net.parameters(), lr=LR, weight_decay=WEIGHT_DECAY)
    # Plain list, not deque: random.sample indexes into the middle, which is O(n) on a deque
    replay_buffer: list = []

    # Seed promoted/ with the untrained model if the folder is empty
    promoted_path = Path(args.promoted_dir)
    if not any(promoted_path.glob("promoted_*.pt")) if promoted_path.exists() else True:
        _promote(net, total_games, 0.0, args.promoted_dir, logger)

    # best_net is the snapshot we pit against; starts as the loaded (or fresh) model.
    # It only ever runs inference, so keep it in eval mode (BatchNorm running stats).
    best_net   = copy.deepcopy(net).eval()
    best_games = total_games

    logger.log_note(
        f"az-bot session start  loaded={load_path or 'scratch'}  workers={N_WORKERS}"
        f"  self_play={N_SELF_PLAY}  n_sims={N_SIMS}  temp_moves={TEMPERATURE_MOVES}"
        f"  sample_reuse={SAMPLE_REUSE}  min_buffer={MIN_BUFFER}  batch={BATCH_SIZE}"
        f"  buffer={BUFFER_SIZE}  lr={LR}  weight_decay={WEIGHT_DECAY}"
        f"  eval_every={EVAL_EVERY}  pit_games={N_PIT_GAMES}  pit_sims={N_SIMS_PIT}"
        f"  pit_opening={PIT_OPENING_MOVES}  promote={PROMOTE_THRESHOLD}"
        f"  value_check_positions={len(value_data[1])}  save_every={SAVE_EVERY}"
    )
    net.eval()
    logger.log_line(f"train value_random  iter=0  {value_metrics(net, *value_data)}")

    store = NetStore()
    executor = make_executor(N_WORKERS)
    try:
        best_path = store.save(best_net, "best")
        iteration = 0
        while not _stop:
            iteration += 1
            print(f"\n[train] ── iter {iteration}  total_games={total_games:,} ──")

            # ── 1. Self-play ──────────────────────────────────────────────────
            net.eval()
            net_path = store.save(net, "latest")

            def on_self_play_game(i: int, n_pos: int, winner: int, secs: float) -> None:
                logger.log_line(f"train self_play_game  iter={iteration}  game={i + 1}/{N_SELF_PLAY}"
                                f"  positions={n_pos}  winner=P{winner}  time={secs:.1f}s")

            t0 = time.perf_counter()
            games = run_self_play(executor, net_path, N_SELF_PLAY, N_SIMS, TEMPERATURE_MOVES,
                                  on_game=on_self_play_game)
            sp_secs = time.perf_counter() - t0
            total_games += N_SELF_PLAY

            examples = [ex for game_examples, _ in games for ex in game_examples]
            lengths = [len(game_examples) for game_examples, _ in games]
            p0_wins = sum(1 for _, winner in games if winner == 0)
            print(f"[train] self-play  new={len(examples)}  ({sp_secs:.0f}s)")
            logger.log_line(
                f"train self_play  iter={iteration}  games={N_SELF_PLAY}  positions={len(examples)}"
                f"  len_mean={np.mean(lengths):.0f}  len_min={min(lengths)}  len_max={max(lengths)}"
                f"  winners P0={p0_wins} P1={N_SELF_PLAY - p0_wins}"
                f"  time={sp_secs:.0f}s  games/min={N_SELF_PLAY / sp_secs * 60:.1f}"
                f"  sims/s={len(examples) * N_SIMS / sp_secs:.0f}"
            )

            # ── 2. Value check on games the net has not trained on yet ────────
            fresh_obs = torch.from_numpy(np.stack([ex[0] for ex in examples]))
            fresh_z = np.array([ex[2] for ex in examples])
            fresh_phase = np.concatenate([np.arange(n) / n for n in lengths])
            logger.log_line(f"train value_fresh  iter={iteration}"
                            f"  {value_metrics(net, fresh_obs, fresh_z, fresh_phase)}")

            replay_buffer.extend(examples)
            if len(replay_buffer) > BUFFER_SIZE:
                del replay_buffer[:len(replay_buffer) - BUFFER_SIZE]

            if len(replay_buffer) < MIN_BUFFER:
                print(f"[train] warming up buffer ({len(replay_buffer)} < {MIN_BUFFER})")
                logger.log_line(f"train skip_train  iter={iteration}"
                                f"  buffer={len(replay_buffer)} < min_buffer={MIN_BUFFER}")
                continue

            # ── 3. Train ──────────────────────────────────────────────────────
            n_steps = max(1, math.ceil(len(examples) * SAMPLE_REUSE / BATCH_SIZE))
            net.train()
            total_loss = total_ploss = total_vloss = 0.0

            t0 = time.perf_counter()
            for _ in range(n_steps):
                obs, pi, z = _sample_batch(replay_buffer, BATCH_SIZE, rng)
                log_p, v = net(obs)
                policy_loss = -(pi * log_p).sum(dim=-1).mean()
                value_loss  = F.mse_loss(v.squeeze(-1), z)
                loss        = policy_loss + value_loss

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
            print(f"[train] loss={avg_loss:.4f}  "
                  f"policy={avg_ploss:.4f}  value={avg_vloss:.4f}")
            logger.log_note(
                f"az-bot iter={iteration}  games={total_games:,}"
                f"  loss={avg_loss:.4f}  p_loss={avg_ploss:.4f}  v_loss={avg_vloss:.4f}"
                f"  steps={n_steps}  buffer={len(replay_buffer)}  train_time={train_secs:.0f}s"
            )

            # ── 4. Save checkpoint ────────────────────────────────────────────
            if iteration % SAVE_EVERY == 0:
                ckpt = os.path.join(args.models_dir, f"az_{total_games:010d}.pt")
                net.save(ckpt)
                print(f"[train] saved → {ckpt}")
                logger.log_line(f"train checkpoint  games={total_games:,}  path={ckpt}")

            # ── 5. Pit vs best ────────────────────────────────────────────────
            if iteration % EVAL_EVERY == 0:
                net.eval()
                logger.log_line(f"train value_random  iter={iteration}"
                                f"  {value_metrics(net, *value_data)}")
                print(f"[train] pit vs checkpoint at games={best_games:,}"
                      f"  ({N_PIT_GAMES} games, {N_SIMS_PIT} sims each)...")

                def on_pit_game(pair: int, a_player: int, winner: int,
                                sub_moves: int, secs: float) -> None:
                    result = "new" if winner == a_player else "best"
                    logger.log_line(f"train pit_game  pair={pair + 1}  new_as=P{a_player}"
                                    f"  winner=P{winner} ({result})  sub_moves={sub_moves}"
                                    f"  time={secs:.1f}s")

                t0 = time.perf_counter()
                wr = run_pit(executor, store.save(net, "latest"), best_path,
                             N_PIT_GAMES, N_SIMS_PIT, opening_moves=PIT_OPENING_MOVES,
                             on_game=on_pit_game)
                pit_secs = time.perf_counter() - t0
                promoted = wr >= PROMOTE_THRESHOLD
                print(f"[train] pit win rate = {wr:.1%}  threshold={PROMOTE_THRESHOLD:.0%}")
                logger.log_note(
                    f"az-bot pit  games={total_games:,}  win_rate={wr:.1%}"
                    f"  vs_checkpoint={best_games:,}  threshold={PROMOTE_THRESHOLD:.0%}"
                    f"  promoted={'yes' if promoted else 'no'}  time={pit_secs:.0f}s"
                )

                if promoted:
                    _promote(net, total_games, wr, args.promoted_dir, logger)
                    best_net   = copy.deepcopy(net).eval()
                    best_games = total_games
                    best_path  = store.save(best_net, "best")
    finally:
        executor.shutdown(cancel_futures=True)
        store.cleanup()

    # ── Shutdown ──────────────────────────────────────────────────────────────
    ckpt = os.path.join(args.models_dir, f"az_{total_games:010d}.pt")
    net.save(ckpt)
    logger.log_session_end(total_games, ckpt)
    print(f"[train] stopped. Final checkpoint: {ckpt}")


if __name__ == "__main__":
    main()
