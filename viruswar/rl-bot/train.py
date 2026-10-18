"""
Training script for the VirusWar RL bot (CNN version).
"""

import argparse
import json
import logging
import multiprocessing
import os
import signal
import sys
from collections import deque
from functools import partial
from pathlib import Path

from dotenv import load_dotenv
load_dotenv()

import numpy as np
import torch
import torch.nn as nn

from sb3_contrib import MaskablePPO
from sb3_contrib.common.wrappers import ActionMasker
from stable_baselines3.common.callbacks import BaseCallback, CheckpointCallback
from stable_baselines3.common.monitor import Monitor
from stable_baselines3.common.torch_layers import BaseFeaturesExtractor
from stable_baselines3.common.vec_env import SubprocVecEnv, DummyVecEnv

from env import VirusWarEnv
from log import setup as setup_logging

log = logging.getLogger(__name__)


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
    def __init__(self, promoted_dir: str, threshold: float,
                 cooldown_steps: int, min_episodes: int,
                 streak_required: int, keep_last: int = 10):
        super().__init__()

        self._dir = promoted_dir
        self._threshold = threshold
        self._cooldown = cooldown_steps
        self._min_episodes = min_episodes
        self._streak_required = streak_required
        self._keep_last = keep_last

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
            log.info(f"promote_streak streak={self._streak}/{self._streak_required}"
                     f" win_rate={win_rate:.1%} step={self.num_timesteps}")
        else:
            self._streak = 0

        if self._streak >= self._streak_required:
            os.makedirs(self._dir, exist_ok=True)
            name = f"promoted_{self.num_timesteps:010d}"
            full_path = os.path.join(self._dir, name + ".zip")
            self.model.save(os.path.join(self._dir, name))
            self._last_promote = self.num_timesteps
            self._streak = 0
            log.info(f"promote step={self.num_timesteps} win_rate={win_rate:.1%} path={full_path}")

            # Prune old models, keep only the most recent keep_last
            existing = sorted(
                (f for f in os.scandir(self._dir) if f.name.startswith("promoted_") and f.name.endswith(".zip")),
                key=lambda f: f.name,
                reverse=True,
            )
            for old in existing[self._keep_last:]:
                os.remove(old.path)
                log.info(f"promote_pruned model={old.name}")


class RolloutLogCallback(BaseCallback):
    def __init__(self):
        super().__init__()
        self._rollout_wins = []
        self._ep_rewards = []
        self._ep_lens = []
        self._server_rt_ms = []

    def _on_step(self) -> bool:
        for info in self.locals.get("infos", []):
            if "game_won" in info:
                self._rollout_wins.append(1 if info["game_won"] else 0)
            if "episode" in info:
                self._ep_rewards.append(info["episode"]["r"])
                self._ep_lens.append(info["episode"]["l"])
            if "server_rt_ms" in info:
                self._server_rt_ms.append(info["server_rt_ms"])
        return True

    def _on_rollout_end(self) -> None:
        if not self._rollout_wins:
            return

        win_rate = sum(self._rollout_wins) / len(self._rollout_wins)
        ep_rew_mean = (sum(self._ep_rewards) / len(self._ep_rewards)
                       if self._ep_rewards else 0.0)
        ep_len_mean = (sum(self._ep_lens) / len(self._ep_lens)
                       if self._ep_lens else 0.0)
        server_rt = (sum(self._server_rt_ms) / len(self._server_rt_ms)
                     if self._server_rt_ms else 0.0)
        raw_ent = self.model.logger.name_to_value.get("train/entropy_loss")
        entropy = -raw_ent if raw_ent is not None else None

        self._rollout_wins = []
        self._ep_rewards = []
        self._ep_lens = []
        self._server_rt_ms = []

        log.info(f"rollout step={self.num_timesteps} win_rate={win_rate:.1%}"
                 f" rew={ep_rew_mean:+.3f} len={ep_len_mean:.1f}"
                 f" entropy={'-' if entropy is None else f'{entropy:.2f}'}"
                 f" server_rt_ms={server_rt:.1f}")


_stop_count = 0


def _handle_signal(sig, frame):
    global _stop_count
    _stop_count += 1
    if _stop_count == 1:
        log.info("shutdown_requested")
        raise KeyboardInterrupt
    else:
        sys.exit(1)


def mask_fn(env: VirusWarEnv) -> np.ndarray:
    return env.action_masks()


def _env_factory(host: str, port: int,
                 credentials: list[tuple[str, str]],
                 opponent: str, log_path: str):
    """Module-level factory so SubprocVecEnv can pickle it (runs in the env's process)."""
    if multiprocessing.current_process().name != "MainProcess":  # SubprocVecEnv worker
        setup_logging("rlenv", log_path)
    env = VirusWarEnv(host, port, credentials, opponent=opponent)
    return Monitor(ActionMasker(env, mask_fn))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default=os.getenv("VIRUSWAR_HOST", "127.0.0.1"))
    parser.add_argument("--port", type=int, default=int(os.getenv("VIRUSWAR_PORT", "1234")))
    parser.add_argument("--credentials",
                        default=os.getenv("VIRUSWAR_TRAIN_CREDENTIALS", "[]"),
                        help="JSON array of [login, password] pairs")
    parser.add_argument("--opponent",
                        default=os.getenv("VIRUSWAR_TRAIN_OPPONENT", ""))
    parser.add_argument("--n-envs", type=int,
                        default=int(os.getenv("VIRUSWAR_N_ENVS", "1")))
    parser.add_argument("--steps", type=int, default=2_000_000)
    parser.add_argument("--save-dir", default="models")
    parser.add_argument("--load", default=None)
    parser.add_argument("--promoted-dir", default="models/promoted")
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

    if args.load is None:
        checkpoints = sorted(Path(args.save_dir).glob("rl_bot_*_steps.zip"),
                             key=lambda p: int(p.stem.split("_")[2]), reverse=True)
        if checkpoints:
            args.load = str(checkpoints[0])

    target_kl = args.target_kl if args.target_kl != 0.0 else None

    raw_creds = json.loads(args.credentials)
    if not raw_creds:
        parser.error("No credentials — set VIRUSWAR_TRAIN_CREDENTIALS or --credentials")
    creds: list[tuple[str, str]] = [tuple(c) for c in raw_creds]

    os.makedirs(args.save_dir, exist_ok=True)

    log_path = args.log or os.path.join(args.save_dir, "train_rl.log")
    setup_logging("rltrain", log_path)

    make = partial(_env_factory, args.host, args.port, creds, args.opponent, log_path)
    if args.n_envs > 1:
        env = SubprocVecEnv([make for _ in range(args.n_envs)])
    else:
        env = DummyVecEnv([make])

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
        log.info(f"session_start loaded={args.load} opponent={args.opponent or 'any'}"
                 f" target_kl={target_kl} clip_range={args.clip_range}"
                 f" n_epochs={args.n_epochs} ent_coef={args.ent_coef} n_envs={args.n_envs}")
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
        log.info(f"session_start loaded=scratch opponent={args.opponent or 'any'}"
                 f" target_kl={target_kl} clip_range={args.clip_range}"
                 f" n_epochs={args.n_epochs} ent_coef={args.ent_coef} n_envs={args.n_envs}")

    checkpoint_cb = CheckpointCallback(
        save_freq=20_000,
        save_path=args.save_dir,
        name_prefix="rl_bot",
    )

    callbacks = [
        checkpoint_cb,
        RolloutLogCallback(),
        PromoteCallback(
            args.promoted_dir,
            threshold=float(os.getenv("VIRUSWAR_PROMOTE_THRESHOLD", "0.55")),
            cooldown_steps=int(os.getenv("VIRUSWAR_PROMOTE_COOLDOWN", "20000")),
            min_episodes=int(os.getenv("VIRUSWAR_PROMOTE_MIN_EP", "100")),
            streak_required=int(os.getenv("VIRUSWAR_PROMOTE_STREAK", "3")),
            keep_last=int(os.getenv("VIRUSWAR_PROMOTE_KEEP", "10")),
        ),
    ]

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
        log.info(f"session_end step={model.num_timesteps} path={save_path}.zip")


if __name__ == "__main__":
    main()
