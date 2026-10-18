"""
AlphaZero-style MCTS for VirusWar.

Each sub-move (3 per turn) is its own tree node; the network gives priors and
values (no rollouts). Q is stored from node.player's view and flipped across
turn boundaries.
"""

import numpy as np
import viruswar_sim as sim
from viruswar_sim import TurnResult

from net import AZNet, board_to_obs


class MCTSNode:
    __slots__ = ('game', 'prior', 'N', 'W', 'children', 'player',
                 'terminal', 'terminal_value')

    def __init__(self, game: sim.VirusWarGame, prior: float):
        self.game = game
        self.prior = float(prior)
        self.N = 0
        self.W = 0.0
        self.children: dict[int, 'MCTSNode'] = {}
        self.player: int = game.current_player()
        self.terminal: bool = False
        self.terminal_value: float = 0.0  # +1.0 if self.player won

    @property
    def Q(self) -> float:
        """Mean value from self.player's perspective."""
        return self.W / self.N if self.N > 0 else 0.0

    def is_expanded(self) -> bool:
        return bool(self.children) or self.terminal

    def select_child(self, c_puct: float) -> tuple[int, 'MCTSNode']:
        """PUCT selection. Q is adjusted for player switches between parent and child."""
        sqrt_n = self.N ** 0.5
        best_score = -float('inf')
        best_action, best_child = -1, None

        for action, child in self.children.items():
            # child.Q is from child.player's view: flip it across a turn boundary
            q = child.Q if child.player == self.player else -child.Q
            score = q + c_puct * child.prior * sqrt_n / (1 + child.N)
            if score > best_score:
                best_score = score
                best_action = action
                best_child = child

        return best_action, best_child

    def expand(self, policy: np.ndarray, mask: np.ndarray) -> None:
        """Create child nodes for all legal moves using network policy as priors."""
        w = self.game.width()
        for action in np.where(mask)[0]:
            action = int(action)
            child_game = self.game.copy()
            child_game.apply_move(action % w, action // w)

            tr = child_game.turn_result()
            is_terminal = tr == TurnResult.GameOver

            # Advance before creating the node so child.player is the player to move
            # (on GameOver it stays the winner)
            if tr == TurnResult.TurnComplete:
                child_game.advance_turn()

            child = MCTSNode(child_game, prior=float(policy[action]))

            if is_terminal:
                child.terminal = True
                child.terminal_value = 1.0

            self.children[action] = child


# ── Helpers ───────────────────────────────────────────────────────────────────

def _add_dirichlet_noise(node: MCTSNode, alpha: float, eps: float) -> None:
    actions = list(node.children.keys())
    noise = np.random.dirichlet([alpha] * len(actions))
    for a, n in zip(actions, noise):
        node.children[a].prior = (1 - eps) * node.children[a].prior + eps * float(n)


def _evaluate(node: MCTSNode, net: AZNet) -> tuple[np.ndarray, float]:
    """Network output for node's position: ((policy, value), legal mask)."""
    g = node.game
    mask = np.array(
        sim.valid_action_mask(g.serialize(), g.width(), g.height()),
        dtype=bool,
    )
    obs = board_to_obs(g.serialize(), g.moves_remaining(), g.width(), g.height())
    return net.predict(obs, mask), mask


# ── Main search ───────────────────────────────────────────────────────────────

def run_mcts(root: MCTSNode,
             net: AZNet,
             n_sims: int,
             c_puct: float = 1.5,
             dirichlet_alpha: float = 0.3,
             dirichlet_eps: float = 0.25,
             add_noise: bool = True) -> MCTSNode:
    """Run n_sims simulations from root, updating visit counts in place; returns root.
    add_noise (training only) mixes Dirichlet noise into the root priors."""
    # Noise must hit every move's root, reused (already expanded) subtrees included,
    # so expand the root up front
    if add_noise:
        if not root.is_expanded():
            (policy, v), mask = _evaluate(root, net)
            root.expand(policy, mask)
            root.N += 1
            root.W += v
        if root.children:
            _add_dirichlet_noise(root, dirichlet_alpha, dirichlet_eps)

    for _ in range(n_sims):
        node = root
        path = [node]

        # ── Selection ─────────────────────────────────────────────────────────
        while node.is_expanded() and not node.terminal:
            _, node = node.select_child(c_puct)
            path.append(node)

        # ── Expansion + Evaluation ────────────────────────────────────────────
        if node.terminal:
            v = node.terminal_value
        else:
            (policy, v), mask = _evaluate(node, net)
            node.expand(policy, mask)

        # ── Backpropagation ───────────────────────────────────────────────────
        # v is from the leaf player's view: negate it for the other player's nodes
        leaf_player = node.player
        for n in path:
            n.N += 1
            n.W += v if n.player == leaf_player else -v

    return root


def get_action_probs(root: MCTSNode, temperature: float = 1.0) -> np.ndarray:
    """Root visit counts as action probabilities: τ=1 proportional, τ=0 one-hot argmax."""
    n_actions = root.game.width() * root.game.height()
    counts = np.zeros(n_actions, dtype=np.float32)
    for action, child in root.children.items():
        counts[action] = child.N

    if temperature == 0:
        probs = np.zeros(n_actions, dtype=np.float32)
        probs[int(np.argmax(counts))] = 1.0
        return probs

    counts = counts ** (1.0 / temperature)
    return counts / counts.sum()


def make_root(game: sim.VirusWarGame) -> MCTSNode:
    return MCTSNode(game, prior=1.0)
