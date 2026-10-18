"""
Ladder: one AZ model against a random bot and a spread of older promoted models.

Pit only compares each candidate with the previous best, so a run of
promotions says little about total progress (and 100-game pits let lucky
models through). The ladder measures the target model against fixed points
of its history instead: win rate should rise the further back the opponent is.

Every opponent plays the same paired random openings (both sides of each),
greedy MCTS with the pit's simulation count. Games run in parallel worker
processes; stop training first or both will fight over the CPU.

Each finished match is logged (--log, one event line per opponent: win rate,
95% interval, implied Elo difference); the summary table is appended to the
Markdown report (--report).

Any player can also be an RL bot model (rl-bot promoted_*.zip): it plays its
policy's best legal move, without search, as rl-bot/play.py does. With an RL
--model and no --opponents, the opponents are picked from its own folder.

Usage:
    cd viruswar/az-bot
    ../.venv/bin/python ladder.py                          # newest promoted vs auto-picked opponents
    ../.venv/bin/python ladder.py --games 100 --picks 4
    ../.venv/bin/python ladder.py --model models/checkpoints/az_0000012600.pt \\
        --opponents random models/promoted/promoted_0000006200.pt
    ../.venv/bin/python ladder.py --model ../rl-bot/models/promoted/promoted_0048354848.zip \\
        --opponents random models/promoted/promoted_0000021400.pt
"""

import argparse
import logging
import math
import os
import time
from collections import defaultdict
from concurrent.futures import as_completed
from pathlib import Path

import numpy as np
import viruswar_sim as sim
from dotenv import load_dotenv

load_dotenv()

from mcts import get_action_probs, make_root, run_mcts
from net import AZNet, board_to_obs
from parallel import make_executor
from self_play import _apply, random_opening_actions, replay_opening
from log import append_report, setup as setup_logging

RANDOM = "random"

log = logging.getLogger(__name__)


# ── Opponent selection ────────────────────────────────────────────────────────

def promoted_models(promoted_dir: str | Path, suffix: str = ".pt") -> list[Path]:
    """Promoted models (AZ .pt or RL .zip), oldest first."""
    return sorted(Path(promoted_dir).glob(f"promoted_*{suffix}"), key=lambda p: p.stem)


def pick_opponents(target: Path, promoted: list[Path], picks: int) -> list[str]:
    """
    The random bot plus `picks` promoted models spread evenly over the history
    before the target: always the seed (oldest) and the one just before it.
    """
    # By the games count in the name, not the file name: az_… checkpoints and
    # promoted_… models would otherwise compare as unrelated strings
    older = [p for p in promoted if _games_in_name(p) < _games_in_name(target)]
    idx = sorted({int(round(i)) for i in np.linspace(0, len(older) - 1, picks)}) if older else []
    return [RANDOM] + [str(older[i]) for i in idx]


def _games_in_name(path: Path) -> int:
    """The number at the end of az_NNNNNNNNNN / promoted_NNNNNNNNNN (games or RL steps)."""
    digits = path.stem.rsplit("_", 1)[-1]
    return int(digits) if digits.isdigit() else -1


def _is_rl(path: str | Path) -> bool:
    return Path(path).suffix == ".zip"


def _name(player: str | Path) -> str:
    """'random', the AZ model's stem, or 'rl:' + the RL model's stem."""
    if player == RANDOM:
        return RANDOM
    return ("rl:" if _is_rl(player) else "") + Path(player).stem


# ── Statistics ────────────────────────────────────────────────────────────────

def wilson(wins: int, n: int, z: float = 1.96) -> tuple[float, float]:
    """95% confidence interval of a win rate (Wilson score, sane at 0% / 100%)."""
    p = wins / n
    centre = (p + z * z / (2 * n)) / (1 + z * z / n)
    half = z * math.sqrt(p * (1 - p) / n + z * z / (4 * n * n)) / (1 + z * z / n)
    return centre - half, centre + half


def elo_diff(win_rate: float, n: int) -> float:
    """Elo difference implied by a win rate, clamped half a game from 0% / 100%."""
    p = min(max(win_rate, 0.5 / n), 1 - 0.5 / n)
    return -400 * math.log10(1 / p - 1)


# ── Worker side ───────────────────────────────────────────────────────────────

_nets: dict[str, object] = {}


def _load(path: str):
    """AZNet for a .pt file, MaskablePPO for an RL .zip file (cached per worker)."""
    net = _nets.get(path)
    if net is None:
        if _is_rl(path):
            from sb3_contrib import MaskablePPO  # only needed when an RL model plays
            net = MaskablePPO.load(path, device="cpu")
        else:
            net = AZNet.load(path).eval()
        _nets[path] = net
    return net


def _rl_action(game: sim.VirusWarGame, model) -> int:
    """The RL policy's most likely legal move; same observation as rl-bot/env.py."""
    w, h = game.width(), game.height()
    obs = board_to_obs(game.serialize(), game.moves_remaining(), w, h)
    mask = np.zeros(w * h, dtype=bool)
    for x, y in game.valid_moves():
        mask[y * w + x] = True
    action, _ = model.predict(obs, action_masks=mask, deterministic=True)
    return int(action)


def _play(game: sim.VirusWarGame, nets: list, n_sims: int,
          c_puct: float, rng: np.random.Generator) -> tuple[int, int]:
    """
    Play to the end; nets[p] is None for the random bot, an AZNet (MCTS) or a
    MaskablePPO model (policy only). An AZNet keeps its subtree within its own
    turn and starts a fresh tree each turn, like play_match.

    Returns:
        (winner, number of sub-moves played)
    """
    root = None
    sub_moves = 0
    while True:
        player = game.current_player()
        net = nets[player]
        if net is None:
            moves = game.valid_moves()
            x, y = moves[rng.integers(len(moves))]
            action = y * game.width() + x
        elif not isinstance(net, AZNet):
            action = _rl_action(game, net)
        else:
            if root is None:
                root = make_root(game.copy())
            run_mcts(root, net, n_sims, c_puct=c_puct, add_noise=False)
            action = int(np.argmax(get_action_probs(root, temperature=0)))
        sub_moves += 1
        if _apply(game, action):
            return player, sub_moves
        if isinstance(net, AZNet) and game.current_player() == player:
            root = root.children[action]
            root.prior = 1.0
        else:
            root = None


def _ladder_task(model: str, opponent: str, opening: list[int], model_player: int,
                 n_sims: int, c_puct: float, seed: int):
    net = _load(model)
    opp = None if opponent == RANDOM else _load(opponent)
    nets = [net, opp] if model_player == 0 else [opp, net]
    t0 = time.perf_counter()
    winner, sub_moves = _play(replay_opening(opening), nets, n_sims, c_puct,
                              np.random.default_rng(seed))
    return winner, sub_moves, time.perf_counter() - t0


# ── Main ──────────────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--model", default=None,
                        help="AZ .pt or RL .zip model to evaluate (default: newest promoted)")
    parser.add_argument("--opponents", nargs="*", default=None,
                        help="AZ .pt / RL .zip paths and/or 'random' "
                             "(default: random + --picks promoted from the model's kind)")
    parser.add_argument("--picks", type=int, default=5,
                        help="Promoted opponents picked automatically, seed and previous included")
    parser.add_argument("--promoted-dir", default="models/promoted")
    parser.add_argument("--games", type=int, default=200,
                        help="Games per opponent (rounded up to pairs)")
    parser.add_argument("--sims", type=int, default=int(os.getenv("AZ_N_SIMS_PIT", "100")))
    parser.add_argument("--c-puct", type=float, default=1.5)
    parser.add_argument("--opening-moves", type=int,
                        default=int(os.getenv("AZ_PIT_OPENING_MOVES", "6")))
    parser.add_argument("--workers", type=int,
                        default=int(os.getenv("AZ_WORKERS", str(max(1, (os.cpu_count() or 2) - 2)))))
    parser.add_argument("--seed", type=int, default=0)
    parser.add_argument("--log", default="models/ladder_az.log",
                        help="Log file (events, one line each)")
    parser.add_argument("--report", default="models/ladder_az.md",
                        help="Markdown report the summary table is appended to")
    args = parser.parse_args()

    promoted = promoted_models(args.promoted_dir)
    if args.model:
        target = Path(args.model)
        if _is_rl(target):
            promoted = promoted_models(target.parent, ".zip")
    elif promoted:
        target = promoted[-1]
    else:
        raise SystemExit(f"no promoted models in {args.promoted_dir}")
    opponents = args.opponents or pick_opponents(target, promoted, args.picks)

    setup_logging("ladder", args.log)
    n_pairs = (args.games + 1) // 2
    log.info(f"start model={_name(target)} opponents={','.join(_name(o) for o in opponents)}"
             f" games={2 * n_pairs} sims={args.sims} opening={args.opening_moves}"
             f" workers={args.workers} seed={args.seed}")

    # The same openings for every opponent, so their results are comparable
    rng = np.random.default_rng(args.seed)
    openings = [random_opening_actions(args.opening_moves, rng) for _ in range(n_pairs)]

    executor = make_executor(args.workers)
    results: dict[str, dict] = {}
    try:
        futures = {}
        for opponent in opponents:
            for pair, opening in enumerate(openings):
                for model_player in (0, 1):
                    future = executor.submit(_ladder_task, str(target), opponent, opening,
                                             model_player, args.sims, args.c_puct,
                                             int(rng.integers(2**32)))
                    futures[future] = (opponent, model_player)

        tally = defaultdict(lambda: {"wins": 0, "games": 0, "side": [0, 0],
                                     "sub_moves": 0, "secs": 0.0})
        total, done, t0 = len(futures), 0, time.perf_counter()
        for future in as_completed(futures):
            opponent, model_player = futures[future]
            winner, sub_moves, secs = future.result()
            t = tally[opponent]
            t["wins"] += winner == model_player
            t["games"] += 1
            t["side"][winner] += 1
            t["sub_moves"] += sub_moves
            t["secs"] += secs
            done += 1
            if done % 20 == 0:
                log.info(f"progress games={done}/{total} secs={time.perf_counter() - t0:.0f}")
            if t["games"] == 2 * n_pairs:
                results[opponent] = _report(target, opponent, t)
    except KeyboardInterrupt:
        log.info("interrupted")
        raise
    finally:
        executor.shutdown(cancel_futures=True)

    rows = [f"| {_name(o)} | {r['wr']:.1%} | {r['lo']:.0%}–{r['hi']:.0%} | {r['elo']:+.0f} |"
            for o, r in ((o, results[o]) for o in opponents if o in results)]
    secs = time.perf_counter() - t0
    append_report(args.report, f"Ladder: {_name(target)}",
                  f"{2 * n_pairs} games per opponent, {args.sims} sims, opening {args.opening_moves},"
                  f" seed {args.seed}, {secs:.0f} s\n\n"
                  "| opponent | win rate | 95% interval | Elo diff |\n|---|---|---|---|\n"
                  + "\n".join(rows))
    log.info(f"done model={_name(target)} secs={secs:.0f} report={args.report}")


def _report(target: Path, opponent: str, t: dict) -> dict:
    n = t["games"]
    wr = t["wins"] / n
    lo, hi = wilson(t["wins"], n)
    elo = elo_diff(wr, n)
    log.info(f"match model={_name(target)} opponent={_name(opponent)} win_rate={wr:.1%}"
             f" ci95={lo:.1%}-{hi:.1%} elo={elo:+.0f} games={n}"
             f" p0_wins={t['side'][0]} p1_wins={t['side'][1]}"
             f" len_mean={t['sub_moves'] / n:.0f} game_secs={t['secs'] / n:.1f}")
    return {"wr": wr, "lo": lo, "hi": hi, "elo": elo}


if __name__ == "__main__":
    main()
