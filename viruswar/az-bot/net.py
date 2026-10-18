"""
AlphaZero network for VirusWar: (6, H, W) observation (same encoding as rl-bot)
→ stem conv + residual tower → policy logits (H*W) and value in [-1, 1].
"""

import numpy as np
import torch
import torch.nn as nn
import torch.nn.functional as F


def board_to_obs(board: str, moves_remaining: int, w: int, h: int) -> np.ndarray:
    """Convert a serialized board string to a (6, H, W) float32 observation."""
    arr = np.array([int(c) for c in board], dtype=np.int8).reshape(h, w)
    obs = np.zeros((6, h, w), dtype=np.float32)
    obs[0] = (arr == 1)              # my cells (X)
    obs[1] = (arr == 3)              # my captured cells (CatchX)
    obs[2] = (arr == 2)              # enemy cells (O)
    obs[3] = (arr == 4)              # enemy captured cells (CatchO)
    obs[4] = (arr == 0)              # empty cells
    obs[5] = moves_remaining / 3.0  # sub-moves left this turn, normalised
    return obs


class ResBlock(nn.Module):
    def __init__(self, channels: int):
        super().__init__()
        self.conv1 = nn.Conv2d(channels, channels, 3, padding=1, bias=False)
        self.bn1   = nn.BatchNorm2d(channels)
        self.conv2 = nn.Conv2d(channels, channels, 3, padding=1, bias=False)
        self.bn2   = nn.BatchNorm2d(channels)
        self.relu  = nn.ReLU(inplace=True)

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        out = self.relu(self.bn1(self.conv1(x)))
        out = self.bn2(self.conv2(out))
        return self.relu(x + out)


class AZNet(nn.Module):
    def __init__(self,
                 board_h: int = 10,
                 board_w: int = 10,
                 in_channels: int = 6,
                 trunk_channels: int = 64,
                 n_blocks: int = 6):
        super().__init__()
        self.h = board_h
        self.w = board_w
        n_actions = board_h * board_w

        self.stem = nn.Sequential(
            nn.Conv2d(in_channels, trunk_channels, 3, padding=1, bias=False),
            nn.BatchNorm2d(trunk_channels),
            nn.ReLU(inplace=True),
        )

        self.tower = nn.Sequential(
            *[ResBlock(trunk_channels) for _ in range(n_blocks)]
        )

        self.policy_head = nn.Sequential(
            nn.Conv2d(trunk_channels, 2, 1, bias=False),
            nn.BatchNorm2d(2),
            nn.ReLU(inplace=True),
            nn.Flatten(),
            nn.Linear(2 * board_h * board_w, n_actions),
        )

        self.value_head = nn.Sequential(
            nn.Conv2d(trunk_channels, 1, 1, bias=False),
            nn.BatchNorm2d(1),
            nn.ReLU(inplace=True),
            nn.Flatten(),
            nn.Linear(board_h * board_w, 64),
            nn.ReLU(inplace=True),
            nn.Linear(64, 1),
            nn.Tanh(),
        )

    def forward(self, x: torch.Tensor) -> tuple[torch.Tensor, torch.Tensor]:
        """x: (B, 6, H, W) → log_p (B, H*W) log-softmax policy, v (B, 1) value in [-1, 1]."""
        trunk = self.tower(self.stem(x))
        log_p = F.log_softmax(self.policy_head(trunk), dim=-1)
        v     = self.value_head(trunk)
        return log_p, v

    @torch.no_grad()
    def predict(self, obs: np.ndarray, mask: np.ndarray) -> tuple[np.ndarray, float]:
        """
        Single-position inference: obs (6, H, W), mask (H*W,) bool of legal moves
        → (policy (H*W,) masked softmax, value in [-1, 1]).
        """
        x = torch.from_numpy(obs).unsqueeze(0)          # (1, 6, H, W)
        trunk = self.tower(self.stem(x))

        logits = self.policy_head(trunk).squeeze(0)      # (H*W,)
        logits[~torch.from_numpy(mask)] = -1e9
        policy = torch.softmax(logits, dim=-1).numpy()

        value = self.value_head(trunk).item()
        return policy, value

    def save(self, path: str) -> None:
        torch.save(self.state_dict(), path)

    @classmethod
    def load(cls, path: str, **kwargs) -> "AZNet":
        net = cls(**kwargs)
        net.load_state_dict(torch.load(path, map_location="cpu", weights_only=True))
        return net
