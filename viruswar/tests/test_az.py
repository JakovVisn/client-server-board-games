"""AZ bot: observations, symmetries, training state, MCTS, ladder helpers, bindings."""

import random
from pathlib import Path

import numpy as np
import pytest
import torch
import viruswar_sim as sim

import ladder
from mcts import get_action_probs, make_root, run_mcts
from net import AZNet, board_to_obs
from symmetry import augment, check_symmetries, transforms
from train import _load_state, _save_state


# ── Observations and symmetries ──────────────────────────────────────────────

def test_board_to_obs_channels():
    board = "1234" + "0" * 21
    obs = board_to_obs(board, 2, 5, 5)
    assert obs.shape == (6, 5, 5)
    assert obs[0, 0, 0] == 1 and obs[2, 0, 1] == 1 and obs[1, 0, 2] == 1 and obs[3, 0, 3] == 1
    assert obs[4, 0, 4] == 1                       # empty
    assert obs[:5].sum() == 25                     # one-hot over the 5 cell planes
    assert np.allclose(obs[5], 2 / 3)


def test_rules_are_symmetric():
    assert check_symmetries(3, random.Random(0)) == 0


def test_augment_moves_policy_with_the_board():
    rng = np.random.default_rng(0)
    obs = np.zeros((6, 4, 4), dtype=np.float32)
    obs[0, 0, 1] = 1                               # one piece at (x=1, y=0)
    pi = np.zeros(16, dtype=np.float32)
    pi[0 * 4 + 1] = 1                              # all policy on that cell
    for _ in range(20):
        o, p = augment(obs, pi, rng)
        y, x = np.argwhere(o[0] == 1)[0]
        assert p[y * 4 + x] == 1


def test_transforms_are_their_own_inverse():
    a = np.arange(16).reshape(4, 4)
    for t in transforms(4, 4):
        assert np.array_equal(t(t(a)), a)
    assert len(transforms(5, 4)) == 2              # diagonal flips only on square boards


# ── Training state ───────────────────────────────────────────────────────────

def test_train_state_round_trip(tmp_path):
    rng = np.random.default_rng(0)
    buffer = []
    for _ in range(5):
        board = "".join(map(str, rng.integers(0, 5, size=100)))
        obs = board_to_obs(board, int(rng.integers(1, 4)), 10, 10)
        pi = rng.random(100).astype(np.float32)
        buffer.append((obs, pi / pi.sum(), float(rng.choice([-1.0, 1.0]))))

    net = AZNet()
    opt = torch.optim.Adam(net.parameters(), lr=1e-3)
    path = str(tmp_path / "train_state.pt")
    _save_state(path, 1234, opt, buffer)

    restored = _load_state(path, 1234, torch.optim.Adam(net.parameters(), lr=1e-3))
    assert restored is not None and len(restored) == len(buffer)
    for (o1, p1, z1), (o2, p2, z2) in zip(buffer, restored):
        assert np.array_equal(o1, o2)
        assert np.allclose(p1, p2)
        assert z1 == z2
    assert _load_state(path, 999, opt) is None     # another checkpoint's state is ignored


# ── MCTS ─────────────────────────────────────────────────────────────────────

class _UniformNet:
    """Stand-in for AZNet: uniform prior over legal moves, value 0."""

    def predict(self, obs, mask):
        return mask.astype(np.float32) / mask.sum(), 0.0


def test_mcts_visit_counts_add_up():
    root = make_root(sim.VirusWarGame(5, 5))
    run_mcts(root, _UniformNet(), 50, add_noise=False)
    assert root.N == 50
    assert sum(c.N for c in root.children.values()) == 49   # the first simulation expanded the root
    assert set(root.children) == {0, 24}                     # the two corners


def test_action_probs():
    root = make_root(sim.VirusWarGame(5, 5))
    run_mcts(root, _UniformNet(), 30, add_noise=False)
    greedy = get_action_probs(root, temperature=0)
    assert greedy.sum() == 1 and greedy.max() == 1
    soft = get_action_probs(root, temperature=1.0)
    assert np.isclose(soft.sum(), 1)
    assert set(np.nonzero(soft)[0]) <= {0, 24}


# ── Ladder helpers ───────────────────────────────────────────────────────────

def test_pick_opponents_works_for_checkpoints():
    promoted = [Path(f"models/promoted/promoted_{n:010d}.pt") for n in (0, 4000, 9000, 20000)]
    picks = ladder.pick_opponents(Path("models/checkpoints/az_0000010000.pt"), promoted, 5)
    assert picks[0] == "random"
    assert all(ladder._games_in_name(Path(p)) < 10000 for p in picks[1:])
    assert len(picks) == 4                         # random + the three older models


def test_wilson_and_elo():
    lo, hi = ladder.wilson(100, 200)
    assert lo < 0.5 < hi
    assert ladder.wilson(200, 200)[1] == pytest.approx(1.0)
    assert ladder.elo_diff(0.5, 200) == pytest.approx(0.0)
    assert ladder.elo_diff(0.75, 200) == pytest.approx(-ladder.elo_diff(0.25, 200))


# ── viruswar_sim bindings ────────────────────────────────────────────────────

def test_bindings_reject_bad_input():
    with pytest.raises(ValueError):
        sim.VirusWarGame(3, 3)
    g = sim.VirusWarGame(5, 5)
    with pytest.raises(ValueError):
        g.init_from_board("10")
    with pytest.raises(ValueError):
        sim.valid_action_mask("0" * 25, 0, 5)
    with pytest.raises(ValueError):
        g.init_from_board("0" * 25, 0)


def test_init_from_board_sets_moves_remaining():
    g = sim.VirusWarGame(5, 5)
    g.init_from_board("1" + "0" * 24, 2)
    assert g.moves_remaining() == 2
    g.init_from_board("1" + "0" * 24)
    assert g.moves_remaining() == 3
