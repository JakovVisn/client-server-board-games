"""
Offline diagnostics for AZ models. No server connection needed.

For every model it checks:
  1. Value head — on positions from random-play games, does sign(v) predict
     the actual winner? A trained net must beat the ~50% of an untrained one.
  2. Raw policy — argmax of the policy head (no search) vs a uniform random player.
  3. MCTS strength — greedy MCTS vs the random player and vs the untrained seed.

Games use paired random openings (both sides of every opening), like pit().
Everything is appended to --log as markdown.

Usage:
    cd viruswar/az-bot
    ../.venv/bin/python evaluate.py                       # promoted models + latest checkpoint
    ../.venv/bin/python evaluate.py --models models/promoted/promoted_0000000400.pt
    ../.venv/bin/python evaluate.py --games 40 --sims 100
"""

import argparse
import random
import time
from pathlib import Path
from typing import Callable

import numpy as np
import viruswar_sim as sim
from viruswar_sim import TurnResult

from diagnostics import value_dataset, value_metrics
from mcts import make_root, run_mcts, get_action_probs
from net import AZNet, board_to_obs
from self_play import random_opening
from training_logger import TrainingLogger

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


def match(name: str, a: Player, b: Player, n_games: int, opening_moves: int,
          rng: np.random.Generator, log: TrainingLogger) -> float:
    """Paired-opening match; returns a's win rate."""
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
            log.log_line(f"eval game  {name}  pair={pair + 1}  a_as=P{a_player}"
                         f"  winner=P{winner} ({'a' if winner == a_player else 'b'})"
                         f"  sub_moves={sub_moves}")
    wr = wins / (2 * n_pairs)
    log.log_line(f"eval match  {name}  a_win_rate={wr:.1%}  games={2 * n_pairs}"
                 f"  side_wins P0={side_wins[0]} P1={side_wins[1]}"
                 f"  time={time.perf_counter() - t0:.0f}s")
    print(f"  {name}: a_win_rate={wr:.1%}  (P0 won {side_wins[0]}, P1 won {side_wins[1]})")
    return wr


def value_check(name: str, net: AZNet, data, log: TrainingLogger) -> None:
    line = value_metrics(net, *data)
    log.log_line(f"eval value  {name}  {line}")
    print(f"  value: {line}")


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
    parser.add_argument("--log", default="models/eval_log_az.md")
    args = parser.parse_args()

    log = TrainingLogger(args.log)
    paths = [Path(p) for p in args.models] if args.models else default_models()
    seed_path = Path(args.seed_model)
    seed_net = AZNet.load(str(seed_path)).eval() if seed_path.exists() else None

    log.log_note(f"eval start  models={[p.name for p in paths]}  seed_model="
                 f"{seed_path.name if seed_net else 'missing'}  games={args.games}"
                 f"  sims={args.sims}  opening={args.opening_moves}"
                 f"  value_games={args.value_games}  seed={args.seed}")

    print(f"Building value dataset from {args.value_games} random games...")
    data = value_dataset(args.value_games, random.Random(args.seed))
    log.log_line(f"eval value_dataset  positions={len(data[1])}"
                 f"  P_win_rate_for_mover={np.mean(data[1] > 0):.1%}")

    for path in paths:
        name = path.name
        print(f"\n== {name}")
        net = AZNet.load(str(path)).eval()
        value_check(name, net, data, log)

        rng = np.random.default_rng(args.seed)
        prng = random.Random(args.seed)
        match(f"{name} policy-only vs random", policy_player(net), random_player(prng),
              args.games, args.opening_moves, rng, log)
        match(f"{name} mcts{args.sims} vs random", mcts_player(net, args.sims),
              random_player(prng), args.games, args.opening_moves, rng, log)
        if seed_net is not None and path.resolve() != seed_path.resolve():
            match(f"{name} mcts{args.sims} vs seed mcts{args.sims}", mcts_player(net, args.sims),
                  mcts_player(seed_net, args.sims), args.games, args.opening_moves, rng, log)

    log.log_line("eval done")
    print(f"\nDone. Results appended to {args.log}")


if __name__ == "__main__":
    main()
