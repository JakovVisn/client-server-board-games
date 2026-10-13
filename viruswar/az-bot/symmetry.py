"""
Board symmetries for training-data augmentation.

The rules are invariant under any grid symmetry that maps the pair of starting
corners {(0,0), (w-1,h-1)} onto itself (reachability uses all 8 neighbours, so
every grid symmetry preserves it):
  - identity and 180° rotation (any board)
  - main-diagonal and anti-diagonal flips (square boards only)

All transforms act on the last two axes (H, W), so the same function maps an
observation (C, H, W) and a policy reshaped to (H, W).
"""

import random

import numpy as np
import viruswar_sim as sim
from viruswar_sim import TurnResult


def _identity(a: np.ndarray) -> np.ndarray:
    return a


def _rot180(a: np.ndarray) -> np.ndarray:
    return a[..., ::-1, ::-1]


def _transpose(a: np.ndarray) -> np.ndarray:
    return np.swapaxes(a, -1, -2)


def _anti_transpose(a: np.ndarray) -> np.ndarray:
    return _rot180(_transpose(a))


def transforms(w: int, h: int) -> list:
    ts = [_identity, _rot180]
    if w == h:
        ts += [_transpose, _anti_transpose]
    return ts


def augment(obs: np.ndarray, pi: np.ndarray,
            rng: np.random.Generator) -> tuple[np.ndarray, np.ndarray]:
    """Apply one random symmetry to an (obs (C,H,W), pi (H*W,)) training pair."""
    _, h, w = obs.shape
    ts = transforms(w, h)
    t = ts[rng.integers(len(ts))]
    return (np.ascontiguousarray(t(obs)),
            np.ascontiguousarray(t(pi.reshape(h, w))).reshape(-1))


def check_symmetries(n_games: int, rng: random.Random, w: int = 10, h: int = 10) -> int:
    """
    Verify the rules really are symmetric: for positions from random games,
    valid_action_mask(t(board)) must equal t(valid_action_mask(board)).

    Returns:
        Number of mismatching (position, transform) pairs; 0 means safe to augment.
    """
    mismatches = 0
    for _ in range(n_games):
        g = sim.VirusWarGame(w, h)
        while True:
            board = np.array([int(c) for c in g.serialize()]).reshape(h, w)
            mask = np.array(sim.valid_action_mask(g.serialize(), w, h)).reshape(h, w)
            for t in transforms(w, h):
                t_board = "".join(str(v) for v in t(board).ravel())
                t_mask = np.array(sim.valid_action_mask(t_board, w, h)).reshape(h, w)
                mismatches += not np.array_equal(t_mask, t(mask))
            x, y = rng.choice(g.valid_moves())
            g.apply_move(x, y)
            tr = g.turn_result()
            if tr == TurnResult.GameOver:
                break
            if tr == TurnResult.TurnComplete:
                g.advance_turn()
    return mismatches
