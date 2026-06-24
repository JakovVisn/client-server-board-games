"""
AlphaZero training loop for VirusWar.

Each iteration:
  1. Self-play  — N_SELF_PLAY games with MCTS (eval mode)
  2. Train      — sample mini-batches from replay buffer (train mode)
  3. Pit        — every EVAL_EVERY iters, evaluate net vs previous best
  4. Promote    — save to promoted/ if win rate > PROMOTE_THRESHOLD

Auto-resumes from the latest checkpoint in --models-dir if no --load given.
Saves an untrained seed model to promoted/ on first run if none exists.

Usage:
    cd viruswar/az-bot
    python train.py
    python train.py --load models/az_0000001000.pt
"""

import argparse
import copy
import os
import random
import signal
from collections import deque
from pathlib import Path

import numpy as np
import torch
import torch.nn.functional as F
from dotenv import load_dotenv

load_dotenv()

from net import AZNet
from self_play import self_play, pit
from training_logger import TrainingLogger

# ── Hyperparameters (can be overridden via .env) ──────────────────────────────

N_SELF_PLAY       = int(os.getenv("AZ_SELF_PLAY_GAMES",    "25"))
N_SIMS            = int(os.getenv("AZ_N_SIMS",             "200"))
TEMPERATURE_MOVES = int(os.getenv("AZ_TEMPERATURE_MOVES",  "20"))

N_TRAIN_STEPS     = int(os.getenv("AZ_TRAIN_STEPS",        "200"))
BATCH_SIZE        = int(os.getenv("AZ_BATCH_SIZE",         "256"))
BUFFER_SIZE       = int(os.getenv("AZ_BUFFER_SIZE",        "200000"))
LR                = float(os.getenv("AZ_LR",               "1e-3"))

EVAL_EVERY        = int(os.getenv("AZ_EVAL_EVERY",         "4"))
N_PIT_GAMES       = int(os.getenv("AZ_PIT_GAMES",          "20"))
N_SIMS_PIT        = int(os.getenv("AZ_N_SIMS_PIT",         "200"))
PROMOTE_THRESHOLD = float(os.getenv("AZ_PROMOTE_THRESHOLD","0.55"))

SAVE_EVERY        = int(os.getenv("AZ_SAVE_EVERY",         "4"))

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


def _sample_batch(buffer: deque,
                  batch_size: int) -> tuple[torch.Tensor, torch.Tensor, torch.Tensor]:
    batch = random.sample(buffer, min(batch_size, len(buffer)))
    obs = torch.from_numpy(np.stack([b[0] for b in batch]))
    pi  = torch.from_numpy(np.stack([b[1] for b in batch]))
    z   = torch.tensor([b[2] for b in batch], dtype=torch.float32)
    return obs, pi, z


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--load",         default=None,
                        help="Path to a .pt checkpoint to load")
    parser.add_argument("--models-dir",   default="models",
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

    optimizer = torch.optim.Adam(net.parameters(), lr=LR)
    replay_buffer: deque = deque(maxlen=BUFFER_SIZE)

    # Seed promoted/ with the untrained model if the folder is empty
    promoted_path = Path(args.promoted_dir)
    if not any(promoted_path.glob("promoted_*.pt")) if promoted_path.exists() else True:
        _promote(net, total_games, 0.0, args.promoted_dir, logger)

    # best_net is the snapshot we pit against; starts as the loaded (or fresh) model
    best_net   = copy.deepcopy(net)
    best_games = total_games

    logger.log_note(
        f"az-bot session start  loaded={load_path or 'scratch'}"
        f"  self_play={N_SELF_PLAY}  n_sims={N_SIMS}"
        f"  train_steps={N_TRAIN_STEPS}  batch={BATCH_SIZE}"
        f"  lr={LR}  promote={PROMOTE_THRESHOLD}"
    )

    iteration = 0
    while not _stop:
        iteration += 1
        print(f"\n[train] ── iter {iteration}  total_games={total_games:,} ──")

        # ── 1. Self-play ──────────────────────────────────────────────────────
        net.eval()
        examples = self_play(
            net, n_games=N_SELF_PLAY, n_sims=N_SIMS,
            temperature_moves=TEMPERATURE_MOVES, verbose=False,
        )
        replay_buffer.extend(examples)
        total_games += N_SELF_PLAY
        print(f"[train] self-play  new={len(examples)}  buffer={len(replay_buffer)}")

        if len(replay_buffer) < BATCH_SIZE:
            print(f"[train] buffer too small ({len(replay_buffer)} < {BATCH_SIZE}), "
                  "skipping train step")
            continue

        # ── 2. Train ──────────────────────────────────────────────────────────
        net.train()
        total_loss = total_ploss = total_vloss = 0.0

        for _ in range(N_TRAIN_STEPS):
            obs, pi, z = _sample_batch(replay_buffer, BATCH_SIZE)
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

        avg_loss  = total_loss  / N_TRAIN_STEPS
        avg_ploss = total_ploss / N_TRAIN_STEPS
        avg_vloss = total_vloss / N_TRAIN_STEPS
        print(f"[train] loss={avg_loss:.4f}  "
              f"policy={avg_ploss:.4f}  value={avg_vloss:.4f}")
        logger.log_note(
            f"az-bot iter={iteration}  games={total_games:,}"
            f"  loss={avg_loss:.4f}  p_loss={avg_ploss:.4f}  v_loss={avg_vloss:.4f}"
        )

        # ── 3. Save checkpoint ────────────────────────────────────────────────
        if iteration % SAVE_EVERY == 0:
            ckpt = os.path.join(args.models_dir, f"az_{total_games:010d}.pt")
            net.save(ckpt)
            print(f"[train] saved → {ckpt}")

        # ── 4. Pit vs best ────────────────────────────────────────────────────
        if iteration % EVAL_EVERY == 0:
            net.eval()
            print(f"[train] pit vs checkpoint at games={best_games:,}"
                  f"  ({N_PIT_GAMES} games, {N_SIMS_PIT} sims each)...")
            wr = pit(net, best_net, n_games=N_PIT_GAMES, n_sims=N_SIMS_PIT)
            print(f"[train] pit win rate = {wr:.1%}  threshold={PROMOTE_THRESHOLD:.0%}")
            logger.log_note(
                f"az-bot pit  games={total_games:,}  win_rate={wr:.1%}"
                f"  vs_checkpoint={best_games:,}"
            )

            if wr >= PROMOTE_THRESHOLD:
                _promote(net, total_games, wr, args.promoted_dir, logger)
                best_net   = copy.deepcopy(net)
                best_games = total_games

    # ── Shutdown ──────────────────────────────────────────────────────────────
    ckpt = os.path.join(args.models_dir, f"az_{total_games:010d}.pt")
    net.save(ckpt)
    logger.log_session_end(total_games, ckpt)
    print(f"[train] stopped. Final checkpoint: {ckpt}")


if __name__ == "__main__":
    main()
