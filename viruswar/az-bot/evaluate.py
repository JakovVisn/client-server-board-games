"""
Offline diagnostics per AZ model: value-head sign accuracy on random-play games,
raw policy vs random, and greedy MCTS vs random and vs the untrained seed.
Paired random openings like the pit; results go to --log. For strength, use ladder.py.

Usage:
    cd viruswar/az-bot
    ../.venv/bin/python evaluate.py                       # promoted models + latest checkpoint
    ../.venv/bin/python evaluate.py --models models/promoted/promoted_0000000400.pt
    ../.venv/bin/python evaluate.py --games 40 --sims 100
"""

import argparse
import logging
import random
import time
from pathlib import Path
from typing import Callable

import numpy as np
import viruswar_sim as sim
from viruswar_sim import TurnResult

from diagnostics import value_dataset, value_metrics
from log import setup as setup_logging
from mcts import make_root, run_mcts, get_action_probs
from net import AZNet, board_to_obs
from self_play import random_opening

log = logging.getLogger(__name__)

# A player maps a game (current player to move) to an action index y*w + x.
Player = Callable[[sim.VirusWarGame], int]


def random_player(rng: random.Random) -> Player:
    def play(game: sim.VirusWarGame) -> int:
        x, y = rng.choice(game.valid_moves())
        return y * game.width() + x
    return play


def policy_player(net: AZNet) -> Player:
    def play(game: sim.VirusWarGame) -> int:
        w, h = game.width(), game.height()
        mask = np.array(sim.valid_action_mask(game.serialize(), w, h), dtype=bool)
        obs = board_to_obs(game.serialize(), game.moves_remaining(), w, h)
        policy, _ = net.predict(obs, mask)
        return int(np.argmax(policy))
    return play


def mcts_player(net: AZNet, n_sims: int) -> Player:
    def play(game: sim.VirusWarGame) -> int:
        root = make_root(game.copy())
        run_mcts(root, net, n_sims, add_noise=False)
        return int(np.argmax(get_action_probs(root, temperature=0)))
    return play


def play_game(game: sim.VirusWarGame, players: list[Player]) -> tuple[int, int]:
    """Play to the end with players[p] moving for player p. Returns (winner, sub_moves)."""
    sub_moves = 0
    while True:
        action = players[game.current_player()](game)
        game.apply_move(action % game.width(), action // game.width())
        sub_moves += 1
        tr = game.turn_result()
        if tr == TurnResult.GameOver:
            return game.current_player(), sub_moves
        if tr == TurnResult.TurnComplete:
            game.advance_turn()


def match(model: str, kind: str, a: Player, b: Player, n_games: int, opening_moves: int,
          rng: np.random.Generator) -> float:
    """Paired-opening match of `model` (player a) in a match `kind`; returns a's win rate."""
    n_pairs = (n_games + 1) // 2
    wins = 0
    side_wins = [0, 0]  # games won by P0 / P1, regardless of who played them
    t0 = time.perf_counter()
    for pair in range(n_pairs):
        opening = random_opening(opening_moves, rng)
        for a_player in (0, 1):
            players = [a, b] if a_player == 0 else [b, a]
            winner, sub_moves = play_game(opening.copy(), players)
            wins += winner == a_player
            side_wins[winner] += 1
            log.info(f"game model={model} match={kind} pair={pair + 1} a_as=P{a_player}"
                     f" winner=P{winner} result={'a' if winner == a_player else 'b'}"
                     f" sub_moves={sub_moves}")
    wr = wins / (2 * n_pairs)
    log.info(f"match model={model} match={kind} a_win_rate={wr:.1%} games={2 * n_pairs}"
             f" p0_wins={side_wins[0]} p1_wins={side_wins[1]}"
             f" secs={time.perf_counter() - t0:.0f}")
    return wr


def value_check(model: str, net: AZNet, data) -> None:
    line, _ = value_metrics(net, *data)
    log.info(f"value model={model} {line}")


def default_models() -> list[Path]:
    models = sorted(Path("models/promoted").glob("promoted_*.pt"))
    ckpts = sorted(Path("models/checkpoints").glob("az_*.pt"))
    if ckpts:
        models.append(ckpts[-1])
    return models


def main():
    parser = argparse.ArgumentParser(description="Offline AZ model diagnostics")
    parser.add_argument("--models", nargs="*", default=None,
                        help="Model .pt files (default: all promoted + latest checkpoint)")
    parser.add_argument("--seed-model", default="models/promoted/promoted_0000000000.pt",
                        help="Untrained reference model")
    parser.add_argument("--games", type=int, default=20, help="Games per match (rounded up to pairs)")
    parser.add_argument("--sims", type=int, default=50, help="MCTS simulations per move")
    parser.add_argument("--opening-moves", type=int, default=6)
    parser.add_argument("--value-games", type=int, default=200,
                        help="Random-play games used for the value check")
    parser.add_argument("--seed", type=int, default=0)
    parser.add_argument("--log", default="models/eval_az.log",
                        help="Log file (events, one line each)")
    args = parser.parse_args()

    setup_logging("azeval", args.log)
    paths = [Path(p) for p in args.models] if args.models else default_models()
    seed_path = Path(args.seed_model)
    seed_net = AZNet.load(str(seed_path)).eval() if seed_path.exists() else None

    log.info(f"start models={','.join(p.name for p in paths)} seed_model="
             f"{seed_path.name if seed_net else 'missing'} games={args.games}"
             f" sims={args.sims} opening={args.opening_moves}"
             f" value_games={args.value_games} seed={args.seed}")

    data = value_dataset(args.value_games, random.Random(args.seed))
    log.info(f"value_dataset positions={len(data[1])}"
             f" mover_win_rate={np.mean(data[1] > 0):.1%}")

    for path in paths:
        model = path.stem
        net = AZNet.load(str(path)).eval()
        value_check(model, net, data)

        rng = np.random.default_rng(args.seed)
        prng = random.Random(args.seed)
        match(model, "policy_vs_random", policy_player(net), random_player(prng),
              args.games, args.opening_moves, rng)
        match(model, f"mcts{args.sims}_vs_random", mcts_player(net, args.sims),
              random_player(prng), args.games, args.opening_moves, rng)
        if seed_net is not None and path.resolve() != seed_path.resolve():
            match(model, f"mcts{args.sims}_vs_seed", mcts_player(net, args.sims),
                  mcts_player(seed_net, args.sims), args.games, args.opening_moves, rng)

    log.info("done")


if __name__ == "__main__":
    main()
