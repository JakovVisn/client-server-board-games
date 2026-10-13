"""
Single-game building blocks for AlphaZero VirusWar: self-play and match games.

Games are run in worker processes by parallel.py. Openings are passed around
as action lists because VirusWarGame objects cannot be pickled.
"""

import numpy as np
import viruswar_sim as sim
from viruswar_sim import TurnResult

from net import AZNet, board_to_obs
from mcts import make_root, run_mcts, get_action_probs


def play_game(
    net: AZNet,
    n_sims: int = 200,
    temperature_moves: int = 20,
    c_puct: float = 1.5,
    dirichlet_alpha: float = 0.3,
    dirichlet_eps: float = 0.25,
) -> tuple[list[tuple[np.ndarray, np.ndarray, float]], int]:
    """
    Play one complete self-play game.

    Temperature schedule (move selection only):
      - First `temperature_moves` sub-moves: τ=1 (sample proportionally to visit counts)
      - Remaining moves: τ=0 (argmax — deterministic)
    This ensures early game diversity while late game is optimal.
    The policy target is always the τ=1 visit distribution.

    Returns:
        (examples, winner) where examples is a list of (obs, pi, z) in move order:
          obs: (6, H, W) float32  — board state from the moving player's view
          pi:  (H*W,)   float32  — MCTS visit count distribution (policy target)
          z:   float             — game outcome: +1.0 win, -1.0 loss (from obs player's view)
        and winner is the player (0 or 1) who won.
    """
    game = sim.VirusWarGame()
    root = make_root(game)

    # history stores (obs, pi, player_who_moved) before z is known
    history: list[tuple[np.ndarray, np.ndarray, int]] = []
    move_count = 0

    while True:
        run_mcts(root, net, n_sims,
                 c_puct=c_puct,
                 dirichlet_alpha=dirichlet_alpha,
                 dirichlet_eps=dirichlet_eps,
                 add_noise=True)

        # The training target is always the full visit distribution; temperature
        # only picks the move to play. Storing the τ=0 one-hot as the target
        # taught a collapsed prior that blinded the search.
        pi = get_action_probs(root, temperature=1.0)
        play_pi = pi if move_count < temperature_moves else get_action_probs(root, temperature=0)

        g = root.game
        obs = board_to_obs(g.serialize(), g.moves_remaining(), g.width(), g.height())
        history.append((obs, pi, root.player))

        # Sample or pick action from policy
        action = int(np.random.choice(len(play_pi), p=play_pi))
        move_count += 1

        # Tree reuse: descend into selected child
        root = root.children[action]
        root.prior = 1.0  # prior is irrelevant for root

        if root.terminal:
            winner = root.player  # this player just made the winning move
            break

    # Assign z retrospectively: +1 for the winner's positions, -1 for the loser's
    examples = [
        (obs, pi, 1.0 if player == winner else -1.0)
        for obs, pi, player in history
    ]
    return examples, winner


def _apply(game: sim.VirusWarGame, action: int) -> bool:
    """Apply one sub-move, advancing the turn when needed. Returns True on game over."""
    game.apply_move(action % game.width(), action // game.width())
    tr = game.turn_result()
    if tr == TurnResult.TurnComplete:
        game.advance_turn()
    return tr == TurnResult.GameOver


def random_opening_actions(n_moves: int, rng: np.random.Generator) -> list[int]:
    """Up to n_moves uniformly random legal sub-moves from the initial position."""
    game = sim.VirusWarGame()
    actions = []
    for _ in range(n_moves):
        moves = game.valid_moves()
        x, y = moves[rng.integers(len(moves))]
        action = y * game.width() + x
        actions.append(action)
        if _apply(game, action):
            break
    return actions


def replay_opening(actions: list[int]) -> sim.VirusWarGame:
    """Rebuild the position reached by an action list from random_opening_actions()."""
    game = sim.VirusWarGame()
    for action in actions:
        _apply(game, action)
    return game


def random_opening(n_moves: int, rng: np.random.Generator) -> sim.VirusWarGame:
    """Position after n_moves uniformly random legal sub-moves."""
    return replay_opening(random_opening_actions(n_moves, rng))


def play_match(game: sim.VirusWarGame, nets: list[AZNet],
               n_sims: int, c_puct: float = 1.5) -> tuple[int, int]:
    """
    Play greedy MCTS from `game` with nets[p] moving for player p.

    Returns:
        (winner, number of sub-moves played)
    """
    root = make_root(game)
    sub_moves = 0
    while True:
        run_mcts(root, nets[root.player], n_sims, c_puct=c_puct, add_noise=False)
        action = int(np.argmax(get_action_probs(root, temperature=0)))
        child = root.children[action]
        sub_moves += 1
        if child.terminal:
            return child.player, sub_moves
        if child.player == root.player:
            # Same net keeps moving: reuse its subtree
            root = child
            root.prior = 1.0
        else:
            # Never let one net search inside a tree built by the other
            root = make_root(child.game)
