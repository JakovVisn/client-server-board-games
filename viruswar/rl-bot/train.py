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

class CustomCNN(BaseFeaturesExtractor):
    def __init__(self, observation_space, features_dim=256):
        super().__init__(observation_space, features_dim)

        n_input_channels = observation_space.shape[0]

        self.cnn = nn.Sequential(
            nn.Conv2d(n_input_channels, 32, kernel_size=3, padding=1),
            nn.BatchNorm2d(32),
            nn.ReLU(),

            nn.Conv2d(32, 64, kernel_size=3, padding=1),
            nn.BatchNorm2d(64),
            nn.ReLU(),

            nn.Conv2d(64, 64, kernel_size=3, padding=1),
            nn.BatchNorm2d(64),
            nn.ReLU(),
        )

        with torch.no_grad():
            sample = torch.zeros(1, *observation_space.shape)
            n_flatten = self.cnn(sample).view(1, -1).shape[1]

        self.linear = nn.Sequential(
            nn.Flatten(),
            nn.Linear(n_flatten, features_dim),
            nn.ReLU(),
        )

    def forward(self, observations):
        return self.linear(self.cnn(observations))

class PromoteCallback(BaseCallback):
    def __init__(self, promote_path: str, threshold: float,
                 cooldown_steps: int, min_episodes: int,
                 streak_required: int):
        super().__init__()

        self._path = promote_path[:-4] if promote_path.endswith(".zip") else promote_path
        self._full_path = self._path + ".zip"

        self._threshold = threshold
        self._cooldown = cooldown_steps
        self._min_episodes = min_episodes
        self._streak_required = streak_required

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


class RolloutLogCallback(BaseCallback):
    def __init__(self):
        super().__init__()
        self._rollout_wins = []

    def _on_step(self) -> bool:
        for info in self.locals.get("infos", []):
            if "game_won" in info:
                self._rollout_wins.append(1 if info["game_won"] else 0)
        return True

    def _on_rollout_end(self) -> None:
        if not self._rollout_wins:
            return

        win_rate = sum(self._rollout_wins) / len(self._rollout_wins)
        self._rollout_wins = []

        print(f"[{datetime.now().strftime('%H:%M:%S')}][train] "
              f"step={self.num_timesteps} "
              f"rollout_win_rate={win_rate:.1%}")

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
    parser.add_argument("--steps", type=int, default=500_000)
    parser.add_argument("--save-dir", default="models")
    parser.add_argument("--load", default=None)
    parser.add_argument("--promote", default="")
    args = parser.parse_args()

    os.makedirs(args.save_dir, exist_ok=True)

    env = VirusWarEnv(args.host, args.port,
                      args.login, args.password,
                      opponent=args.opponent)

    env = ActionMasker(env, mask_fn)

    policy_kwargs = dict(
        features_extractor_class=CustomCNN,
        features_extractor_kwargs=dict(features_dim=256),
    )

    if args.load:
        model = MaskablePPO.load(args.load, env=env)
        print(f"Resumed from {args.load}")
    else:
        model = MaskablePPO(
            "CnnPolicy",
            env,
            policy_kwargs=policy_kwargs,
            verbose=1,
            gamma=0.995,
            n_steps=4096,
            ent_coef=0.02,
            learning_rate=1e-4,
            batch_size=512,
            clip_range=0.2,
            target_kl=0.01,
        )
        print("Started fresh CNN training")

    checkpoint_cb = CheckpointCallback(
        save_freq=20_000,
        save_path=args.save_dir,
        name_prefix="rl_bot",
    )

    callbacks = [checkpoint_cb, RolloutLogCallback()]

    if args.promote:
        callbacks.append(
            PromoteCallback(
                args.promote,
                threshold=0.55,
                cooldown_steps=20_000,
                min_episodes=50,
                streak_required=2,
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


if __name__ == "__main__":
    main()
