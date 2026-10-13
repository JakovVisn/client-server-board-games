"""
Value-head diagnostics shared by train.py and evaluate.py.

A useful value head predicts the eventual winner, at least late in the game.
value_metrics() reports, per game phase, how often sign(v) matches the outcome
(0.5 = coin flip), the correlation of v with the outcome, and mean |v|
(confidence). High confidence with ~0.5 accuracy means the net memorised.
"""

import random

import numpy as np
import torch
import viruswar_sim as sim
from viruswar_sim import TurnResult

from net import AZNet, board_to_obs

PHASES = ((0.0, 0.5), (0.5, 0.9), (0.9, 1.01))


def value_dataset(n_games: int, rng: random.Random,
                  w: int = 10, h: int = 10) -> tuple[torch.Tensor, np.ndarray, np.ndarray]:
    """
    Positions from random-play games.

    Returns:
        (obs (N,6,H,W), z (N,) outcome for the player to move, phase (N,) in [0,1))
    """
    obs, z, phase = [], [], []
    for _ in range(n_games):
        g = sim.VirusWarGame(w, h)
        hist = []
        while True:
            hist.append((board_to_obs(g.serialize(), g.moves_remaining(), w, h),
                         g.current_player()))
            x, y = rng.choice(g.valid_moves())
            g.apply_move(x, y)
            tr = g.turn_result()
            if tr == TurnResult.GameOver:
                winner = g.current_player()
                break
            if tr == TurnResult.TurnComplete:
                g.advance_turn()
        n = len(hist)
        for i, (o, p) in enumerate(hist):
            obs.append(o)
            z.append(1.0 if p == winner else -1.0)
            phase.append(i / n)
    return torch.from_numpy(np.stack(obs)), np.array(z), np.array(phase)


def value_metrics(net: AZNet, obs: torch.Tensor, z: np.ndarray,
                  phase: np.ndarray) -> tuple[str, float]:
    """
    Value quality on (obs, z); net must be in eval mode.

    Returns:
        (one-line summary, mse) — mse above 1.0 is worse than always predicting 0.
    """
    with torch.no_grad():  # chunked: tens of thousands of positions at once would need GBs
        v = torch.cat([net(chunk)[1].squeeze(-1) for chunk in obs.split(4096)]).numpy()
    parts = []
    for lo, hi in PHASES:
        m = (phase >= lo) & (phase < hi)
        if not m.any():
            continue
        acc = np.mean(np.sign(v[m]) == z[m])
        corr = np.corrcoef(v[m], z[m])[0, 1] if v[m].std() > 0 and z[m].std() > 0 else float("nan")
        parts.append(f"phase {lo:.1f}-{min(hi, 1.0):.1f}: sign_acc={acc:.2f} corr={corr:+.2f}"
                     f" mean|v|={np.abs(v[m]).mean():.2f}")
    mse = float(np.mean((v - z) ** 2))
    return f"mse={mse:.3f}  " + "  |  ".join(parts), mse
