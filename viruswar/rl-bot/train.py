"""
Training script for the VirusWar RL bot.

Credentials are read from .env (see .env.example). CLI args override env vars.

Usage:
  # Start fresh:
  python train.py --steps 500000

  # Resume from checkpoint:
  python train.py --steps 200000 --load models/rl_bot_100000_steps.zip
"""

import argparse
import os
import signal
import sys
from collections import deque
from datetime import datetime

from dotenv import load_dotenv
load_dotenv()

import numpy as np
from sb3_contrib import MaskablePPO
from sb3_contrib.common.wrappers import ActionMasker
from stable_baselines3.common.callbacks import BaseCallback, CheckpointCallback

from env import VirusWarEnv


class PromoteCallback(BaseCallback):
    """Saves the model to promote_path when the training signal is strong enough.

    Promote fires only when ALL of the following are true simultaneously:
      - at least min_episodes game outcomes are in the rolling buffer (reliable estimate)
      - win_rate >= threshold for streak_required consecutive rollouts in a row
      - at least cooldown_steps have passed since the last promote

    play.py watches the output file and hot-reloads it to update the opponent.

    Win rate is tracked via game_won key in the info dict (independent of shaped rewards).
    """

    def __init__(self, promote_path: str, threshold: float, cooldown_steps: int,
                 min_episodes: int, streak_required: int):
        super().__init__()
        # Strip .zip so model.save() doesn't double-add the extension
        self._path = promote_path[:-4] if promote_path.endswith(".zip") else promote_path
        self._full_path = self._path + ".zip"
        self._threshold = threshold
        self._cooldown = cooldown_steps
        self._min_episodes = min_episodes
        self._streak_required = streak_required
        self._last_promote = -cooldown_steps  # allow promote on first eligible check
        self._streak = 0
        self._outcome_buf: deque[int] = deque(maxlen=100)

    def _on_step(self) -> bool:
        for info in self.locals.get("infos", []):
            if "game_won" in info:
                self._outcome_buf.append(1 if info["game_won"] else 0)
        return True

    def _on_rollout_end(self) -> None:
        # Reset streak if we're still inside the cooldown window
        if self.num_timesteps - self._last_promote < self._cooldown:
            self._streak = 0
            return

        if len(self._outcome_buf) < self._min_episodes:
            return

        win_rate = sum(self._outcome_buf) / len(self._outcome_buf)

        if win_rate >= self._threshold:
            self._streak += 1
            print(f"[{datetime.now().strftime('%H:%M:%S')}][promote] streak {self._streak}/{self._streak_required}  "
                  f"win_rate={win_rate:.1%}  (step {self.num_timesteps})")
        else:
            if self._streak > 0:
                print(f"[{datetime.now().strftime('%H:%M:%S')}][promote] streak reset  win_rate={win_rate:.1%}")
            self._streak = 0

        if self._streak >= self._streak_required:
            self.model.save(self._path)
            self._last_promote = self.num_timesteps
            self._streak = 0
            print(f"[{datetime.now().strftime('%H:%M:%S')}][promote] PROMOTED → {self._full_path} (step {self.num_timesteps})")
            self._outcome_buf.clear()


class RolloutLogCallback(BaseCallback):
    """Logs win_rate and ep_len after every rollout in [train] format."""

    def __init__(self):
        super().__init__()
        self._rollout_wins: list[int] = []

    def _on_step(self) -> bool:
        for info in self.locals.get("infos", []):
            if "game_won" in info:
                self._rollout_wins.append(1 if info["game_won"] else 0)
        return True

    def _on_rollout_end(self) -> None:
        wins = self._rollout_wins
        self._rollout_wins = []
        if not wins:
            return
        win_rate = sum(wins) / len(wins)
        ep_len = float(np.mean([ep["l"] for ep in self.model.ep_info_buffer])) \
            if self.model.ep_info_buffer else 0.0
        print(f"[{datetime.now().strftime('%H:%M:%S')}][train] step {self.num_timesteps:>10}  "
              f"win_rate={win_rate:.1%}  ep_len={ep_len:.1f}  episodes={len(wins)}")


_stop_count = 0


def _handle_signal(sig, frame):
    global _stop_count
    _stop_count += 1
    if _stop_count == 1:
        print(f"\n[{datetime.now().strftime('%H:%M:%S')}][train] Graceful shutdown — finishing current rollout...")
        raise KeyboardInterrupt
    else:
        print(f"\n[{datetime.now().strftime('%H:%M:%S')}][train] Forced shutdown.")
        sys.exit(1)


def mask_fn(env: VirusWarEnv) -> np.ndarray:
    return env.action_masks()


def main():
    parser = argparse.ArgumentParser(description="Train the VirusWar RL bot")
    parser.add_argument("--host",     default=os.getenv("VIRUSWAR_HOST", "127.0.0.1"))
    parser.add_argument("--port",     type=int, default=int(os.getenv("VIRUSWAR_PORT", "1234")))
    parser.add_argument("--login",    default=os.getenv("VIRUSWAR_TRAIN_LOGIN"))
    parser.add_argument("--password", default=os.getenv("VIRUSWAR_TRAIN_PASSWORD"))
    parser.add_argument("--opponent", default=os.getenv("VIRUSWAR_TRAIN_OPPONENT", ""),
                        help="Login of the bot to train against")
    parser.add_argument("--steps",    type=int, default=0,
                        help="Total training timesteps (0 = run until Ctrl+C)")
    parser.add_argument("--save-dir", default="models",
                        help="Directory for checkpoints and final model")
    parser.add_argument("--save-freq", type=int, default=10_000,
                        help="Save a checkpoint every N steps")
    parser.add_argument("--load",     default=None,
                        help="Path to a saved model (.zip) to resume training from")
    parser.add_argument("--promote",           default="",
                        help="Path to write promote.zip when training signal is strong "
                             "(e.g. models/promote.zip). Empty = disabled (default).")
    parser.add_argument("--promote-threshold", type=float, default=0.90,
                        help="Win rate threshold to trigger promote (default: 0.90 = 90%%)")
    parser.add_argument("--promote-cooldown",  type=int, default=50_000,
                        help="Minimum steps between promotes (default: 50000)")
    parser.add_argument("--promote-streak",    type=int, default=3,
                        help="Consecutive rollouts above threshold required (default: 3)")
    parser.add_argument("--promote-min-ep",    type=int, default=50,
                        help="Minimum episodes in buffer for a reliable estimate (default: 50)")
    args = parser.parse_args()

    if args.login is None or args.password is None:
        parser.error("--login and --password are required (or set VIRUSWAR_TRAIN_LOGIN / VIRUSWAR_TRAIN_PASSWORD in .env)")

    os.makedirs(args.save_dir, exist_ok=True)

    env = VirusWarEnv(args.host, args.port, args.login, args.password,
                      opponent=args.opponent)
    env = ActionMasker(env, mask_fn)

    if args.load:
        model = MaskablePPO.load(args.load, env=env)
        print(f"Resumed training from {args.load}")
    else:
        model = MaskablePPO(
            "MlpPolicy",
            env,
            verbose=1,
        )
        print("Starting fresh training run")

    checkpoint_cb = CheckpointCallback(
        save_freq=args.save_freq,
        save_path=args.save_dir,
        name_prefix="rl_bot",
        verbose=1,
    )
    callbacks = [checkpoint_cb, RolloutLogCallback()]
    if args.promote:
        callbacks.append(PromoteCallback(
            args.promote,
            threshold=args.promote_threshold,
            cooldown_steps=args.promote_cooldown,
            min_episodes=args.promote_min_ep,
            streak_required=args.promote_streak,
        ))
        print(f"Promote enabled: threshold={args.promote_threshold:.0%} win rate, "
              f"streak={args.promote_streak}, min_ep={args.promote_min_ep}, "
              f"cooldown={args.promote_cooldown} steps → {args.promote}")

    signal.signal(signal.SIGINT,  _handle_signal)
    signal.signal(signal.SIGTERM, _handle_signal)

    total_timesteps = args.steps if args.steps > 0 else int(1e12)
    try:
        model.learn(
            total_timesteps=total_timesteps,
            callback=callbacks,
            progress_bar=False,
            reset_num_timesteps=args.load is None,
        )
        print("Training complete.")
    except KeyboardInterrupt:
        pass
    finally:
        save_path = os.path.join(args.save_dir, f"rl_bot_{model.num_timesteps}_steps")
        model.save(save_path)
        print(f"Model saved to {save_path}.zip")


if __name__ == "__main__":
    main()
