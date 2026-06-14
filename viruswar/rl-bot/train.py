"""
Training script for the VirusWar RL bot (CNN version).
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
import torch
import torch.nn as nn

from sb3_contrib import MaskablePPO
from sb3_contrib.common.wrappers import ActionMasker
from stable_baselines3.common.callbacks import BaseCallback, CheckpointCallback
from stable_baselines3.common.torch_layers import BaseFeaturesExtractor

from env import VirusWarEnv
from training_logger import TrainingLogger

class ResidualBlock(nn.Module):
    def __init__(self, channels: int):
        super().__init__()
        self.conv1 = nn.Conv2d(channels, channels, 3, padding=1, bias=False)
        self.bn1   = nn.BatchNorm2d(channels)
        self.conv2 = nn.Conv2d(channels, channels, 3, padding=1, bias=False)
        self.bn2   = nn.BatchNorm2d(channels)
        self.relu  = nn.ReLU(inplace=True)

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        residual = x
        out = self.relu(self.bn1(self.conv1(x)))
        out = self.bn2(self.conv2(out))
        return self.relu(out + residual)


class CustomCNN(BaseFeaturesExtractor):
    def __init__(self, observation_space, features_dim: int = 256,
                 n_channels: int = 64, n_res_blocks: int = 4):
        super().__init__(observation_space, features_dim)
        n_input = observation_space.shape[0]
        h, w = observation_space.shape[1], observation_space.shape[2]

        self.stem = nn.Sequential(
            nn.Conv2d(n_input, n_channels, kernel_size=3, padding=1, bias=False),
            nn.BatchNorm2d(n_channels),
            nn.ReLU(inplace=True),
        )
        self.tower = nn.Sequential(
            *[ResidualBlock(n_channels) for _ in range(n_res_blocks)]
        )
        self.head = nn.Sequential(
            nn.Flatten(),
            nn.Linear(n_channels * h * w, features_dim),
            nn.ReLU(inplace=True),
        )

    def forward(self, observations: torch.Tensor) -> torch.Tensor:
        return self.head(self.tower(self.stem(observations)))

class PromoteCallback(BaseCallback):
    def __init__(self, promote_path: str, threshold: float,
                 cooldown_steps: int, min_episodes: int,
                 streak_required: int, logger: TrainingLogger | None = None):
        super().__init__()

        self._path = promote_path[:-4] if promote_path.endswith(".zip") else promote_path
        self._full_path = self._path + ".zip"

        self._threshold = threshold
        self._cooldown = cooldown_steps
        self._min_episodes = min_episodes
        self._streak_required = streak_required
        self._logger = logger

        self._last_promote = -cooldown_steps
        self._streak = 0
        self._outcome_buf: deque[int] = deque(maxlen=300)

    def _on_step(self) -> bool:
        for info in self.locals.get("infos", []):
            if "game_won" in info:
                self._outcome_buf.append(1 if info["game_won"] else 0)
        return True

    def _on_rollout_end(self) -> None:

        if self.num_timesteps - self._last_promote < self._cooldown:
            self._streak = 0
            return

        if len(self._outcome_buf) < self._min_episodes:
            return

        win_rate = sum(self._outcome_buf) / len(self._outcome_buf)

        if win_rate >= self._threshold:
            self._streak += 1
            print(f"[{datetime.now().strftime('%H:%M:%S')}][promote] "
                  f"streak {self._streak}/{self._streak_required} "
                  f"win_rate={win_rate:.1%} step={self.num_timesteps}")
        else:
            self._streak = 0

        if self._streak >= self._streak_required:
            self.model.save(self._path)
            self._last_promote = self.num_timesteps
            self._streak = 0
            print(f"[{datetime.now().strftime('%H:%M:%S')}][promote] "
                  f"PROMOTED → {self._full_path}")
            if self._logger:
                self._logger.log_promotion(self.num_timesteps, win_rate, self._full_path)


class RolloutLogCallback(BaseCallback):
    def __init__(self, logger: TrainingLogger | None = None, log_every: int = 5):
        super().__init__()
        self._rollout_wins = []
        self._ep_rewards = []
        self._logger = logger
        self._log_every = log_every
        self._iteration = 0

    def _on_step(self) -> bool:
        for info in self.locals.get("infos", []):
            if "game_won" in info:
                self._rollout_wins.append(1 if info["game_won"] else 0)
            if "episode" in info:
                self._ep_rewards.append(info["episode"]["r"])
        return True

    def _on_rollout_end(self) -> None:
        if not self._rollout_wins:
            return

        win_rate = sum(self._rollout_wins) / len(self._rollout_wins)
        ep_rew_mean = (sum(self._ep_rewards) / len(self._ep_rewards)
                       if self._ep_rewards else 0.0)
        self._rollout_wins = []
        self._ep_rewards = []
        self._iteration += 1

        print(f"[{datetime.now().strftime('%H:%M:%S')}][train] "
              f"step={self.num_timesteps} "
              f"rollout_win_rate={win_rate:.1%}")

        if self._logger and self._iteration % self._log_every == 0:
            self._logger.log_rollout(self.num_timesteps, win_rate, ep_rew_mean)

_stop_count = 0


def _handle_signal(sig, frame):
    global _stop_count
    _stop_count += 1
    if _stop_count == 1:
        print("\nGraceful shutdown...")
        raise KeyboardInterrupt
    else:
        sys.exit(1)


def mask_fn(env: VirusWarEnv) -> np.ndarray:
    return env.action_masks()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default=os.getenv("VIRUSWAR_HOST", "127.0.0.1"))
    parser.add_argument("--port", type=int, default=int(os.getenv("VIRUSWAR_PORT", "1234")))
    parser.add_argument("--login", default=os.getenv("VIRUSWAR_TRAIN_LOGIN"))
    parser.add_argument("--password", default=os.getenv("VIRUSWAR_TRAIN_PASSWORD"))
    parser.add_argument("--opponent", default=os.getenv("VIRUSWAR_TRAIN_OPPONENT", ""))
    parser.add_argument("--steps", type=int, default=2_000_000)
    parser.add_argument("--save-dir", default="models")
    parser.add_argument("--load", default=None)
    parser.add_argument("--promote", default="")
    parser.add_argument("--log", default="")
    parser.add_argument("--target-kl", type=float,
                        default=float(os.getenv("VIRUSWAR_TARGET_KL", "0")) or None)
    parser.add_argument("--clip-range", type=float,
                        default=float(os.getenv("VIRUSWAR_CLIP_RANGE", "0.2")))
    parser.add_argument("--n-epochs", type=int,
                        default=int(os.getenv("VIRUSWAR_N_EPOCHS", "10")))
    parser.add_argument("--ent-coef", type=float,
                        default=float(os.getenv("VIRUSWAR_ENT_COEF", "0.05")))
    args = parser.parse_args()

    # 0.0 in env means None (no KL limit)
    target_kl = args.target_kl if args.target_kl != 0.0 else None

    os.makedirs(args.save_dir, exist_ok=True)

    log_path = args.log or os.path.join(args.save_dir, "training_log.md")
    tlog = TrainingLogger(log_path)

    env = VirusWarEnv(args.host, args.port,
                      args.login, args.password,
                      opponent=args.opponent)

    env = ActionMasker(env, mask_fn)

    def lr_schedule(progress: float) -> float:
        """Linear decay from 3e-4 to 5e-5 as progress goes 1→0."""
        return 5e-5 + (3e-4 - 5e-5) * progress

    policy_kwargs = dict(
        features_extractor_class=CustomCNN,
        features_extractor_kwargs=dict(
            features_dim=256,
            n_channels=64,
            n_res_blocks=4,
        ),
    )

    if args.load:
        model = MaskablePPO.load(args.load, env=env)
        model.learning_rate = lr_schedule
        model.target_kl = target_kl
        model.clip_range = (lambda c: lambda _: c)(args.clip_range)
        model.n_epochs = args.n_epochs
        model.ent_coef = args.ent_coef
        print(f"Resumed from {args.load} "
              f"(target_kl={target_kl}, clip_range={args.clip_range}, "
              f"n_epochs={args.n_epochs}, ent_coef={args.ent_coef})")
        tlog.log_session_start(args.load, args.opponent, target_kl,
                               args.clip_range, args.n_epochs, args.ent_coef)
    else:
        model = MaskablePPO(
            "CnnPolicy",
            env,
            policy_kwargs=policy_kwargs,
            verbose=1,
            gamma=0.99,
            n_steps=2048,
            n_epochs=args.n_epochs,
            ent_coef=args.ent_coef,
            learning_rate=lr_schedule,
            batch_size=256,
            clip_range=args.clip_range,
            target_kl=target_kl,
            vf_coef=0.5,
        )
        print(f"Started fresh training (residual CNN) "
              f"(target_kl={target_kl}, clip_range={args.clip_range}, "
              f"n_epochs={args.n_epochs}, ent_coef={args.ent_coef})")
        tlog.log_session_start(None, args.opponent, target_kl,
                               args.clip_range, args.n_epochs, args.ent_coef)

    checkpoint_cb = CheckpointCallback(
        save_freq=20_000,
        save_path=args.save_dir,
        name_prefix="rl_bot",
    )

    callbacks = [checkpoint_cb, RolloutLogCallback(logger=tlog)]

    if args.promote:
        callbacks.append(
            PromoteCallback(
                args.promote,
                threshold=0.55,
                cooldown_steps=20_000,
                min_episodes=100,
                streak_required=3,
                logger=tlog,
            )
        )

    signal.signal(signal.SIGINT, _handle_signal)
    signal.signal(signal.SIGTERM, _handle_signal)

    try:
        model.learn(
            total_timesteps=args.steps,
            callback=callbacks,
            progress_bar=False,
            reset_num_timesteps=args.load is None,
        )
    except KeyboardInterrupt:
        pass
    finally:
        save_path = os.path.join(args.save_dir,
                                 f"rl_bot_{model.num_timesteps}_steps")
        model.save(save_path)
        print(f"Saved → {save_path}.zip")
        tlog.log_session_end(model.num_timesteps, f"{save_path}.zip")


if __name__ == "__main__":
    main()
