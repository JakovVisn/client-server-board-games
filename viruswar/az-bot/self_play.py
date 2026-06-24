"""
Self-play data generation for AlphaZero VirusWar.

Two MCTS instances sharing one network play against each other.
Each position yields one training example: (obs, pi, z).
"""

import numpy as np
import viruswar_sim as sim

from net import AZNet, board_to_obs
from mcts import make_root, run_mcts, get_action_probs


def play_game(
    net: AZNet,
    n_sims: int = 200,
    temperature_moves: int = 20,
    c_puct: float = 1.5,
    dirichlet_alpha: float = 0.3,
    dirichlet_eps: float = 0.25,
) -> list[tuple[np.ndarray, np.ndarray, float]]:
    """
    Play one complete self-play game.

    Temperature schedule:
      - First `temperature_moves` sub-moves: τ=1 (sample proportionally to visit counts)
      - Remaining moves: τ=0 (argmax — deterministic)
    This ensures early game diversity while late game is optimal.

    Returns:
        List of (obs, pi, z) where:
          obs: (6, H, W) float32  — board state from the moving player's view
          pi:  (H*W,)   float32  — MCTS visit count distribution (policy target)
          z:   float             — game outcome: +1.0 win, -1.0 loss (from obs player's view)
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

        temperature = 1.0 if move_count < temperature_moves else 0.0
        pi = get_action_probs(root, temperature=temperature)

        g = root.game
        obs = board_to_obs(g.serialize(), g.moves_remaining(), g.width(), g.height())
        history.append((obs, pi, root.player))

        # Sample or pick action from policy
        action = int(np.random.choice(len(pi), p=pi))
        move_count += 1

        # Tree reuse: descend into selected child
        root = root.children[action]
        root.prior = 1.0  # prior is irrelevant for root

        if root.terminal:
            winner = root.player  # this player just made the winning move
            break

    # Assign z retrospectively: +1 for the winner's positions, -1 for the loser's
    return [
        (obs, pi, 1.0 if player == winner else -1.0)
        for obs, pi, player in history
    ]


def self_play(
    net: AZNet,
    n_games: int,
    n_sims: int = 200,
    temperature_moves: int = 20,
    c_puct: float = 1.5,
    dirichlet_alpha: float = 0.3,
    dirichlet_eps: float = 0.25,
    verbose: bool = True,
) -> list[tuple[np.ndarray, np.ndarray, float]]:
    """
    Play n_games self-play games and return all training examples.

    Returns:
        Flat list of (obs, pi, z) from all games.
    """
    buffer: list[tuple[np.ndarray, np.ndarray, float]] = []
    for i in range(n_games):
        examples = play_game(
            net,
            n_sims=n_sims,
            temperature_moves=temperature_moves,
            c_puct=c_puct,
            dirichlet_alpha=dirichlet_alpha,
            dirichlet_eps=dirichlet_eps,
        )
        buffer.extend(examples)
        if verbose:
            print(f"  [{i + 1}/{n_games}] {len(examples)} positions")
    return buffer


def pit(
    net_a: AZNet,
    net_b: AZNet,
    n_games: int,
    n_sims: int = 400,
    c_puct: float = 1.5,
) -> float:
    """
    Evaluate net_a vs net_b by playing n_games deterministic games.

    net_a plays as player 0 for the first half of games,
    then as player 1 for the second half (to remove first-mover advantage).

    Returns:
        Win rate of net_a (0.0 – 1.0).
    """
    wins = 0

    for i in range(n_games):
        # Alternate who plays as player 0
        first, second = (net_a, net_b) if i % 2 == 0 else (net_b, net_a)
        net_a_is_player = 0 if i % 2 == 0 else 1

        game = sim.VirusWarGame()
        nets = [first, second]
        root = make_root(game)

        while True:
            current_net = nets[root.player]
            run_mcts(root, current_net, n_sims, c_puct=c_puct, add_noise=False)
            pi = get_action_probs(root, temperature=0)
            action = int(np.argmax(pi))

            root = root.children[action]
            root.prior = 1.0

            if root.terminal:
                if root.player == net_a_is_player:
                    wins += 1
                break

    return wins / n_games
