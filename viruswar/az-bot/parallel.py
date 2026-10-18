"""
Parallel self-play and pit on a ProcessPoolExecutor.

~70% of MCTS time is single-position inference, so each process plays one game with
single-threaded torch. Nets reach workers as files (a new path per version), cached
by path, so each worker loads a net once instead of unpickling it with every task.
"""

import multiprocessing as mp
import os
import shutil
import signal
import tempfile
import time
from concurrent.futures import Future, ProcessPoolExecutor, as_completed
from typing import Callable

import numpy as np
import torch

from net import AZNet
from self_play import play_game, play_match, random_opening_actions, replay_opening


# ── Main-process side ─────────────────────────────────────────────────────────

class NetStore:
    """Temporary files holding the net versions workers should use."""

    def __init__(self):
        self._dir = tempfile.mkdtemp(prefix="az-nets-")
        self._counter = 0
        self._current: dict[str, str] = {}  # name → path of its latest version

    def save(self, net: AZNet, name: str) -> str:
        """Write a new version of `name`; the previous version's file is removed."""
        self._counter += 1
        path = os.path.join(self._dir, f"{name}_{self._counter:06d}.pt")
        net.save(path)
        old = self._current.get(name)
        if old:
            os.remove(old)
        self._current[name] = path
        return path

    def cleanup(self) -> None:
        shutil.rmtree(self._dir, ignore_errors=True)


def make_executor(n_workers: int) -> ProcessPoolExecutor:
    return ProcessPoolExecutor(max_workers=n_workers,
                               mp_context=mp.get_context("spawn"),
                               initializer=_init_worker)


def run_self_play(
    executor: ProcessPoolExecutor,
    net_path: str,
    n_games: int,
    n_sims: int,
    temperature_moves: int,
    on_game: Callable[[int, int, int, float], None] | None = None,
) -> list[tuple[list, int]]:
    """
    Play n_games self-play games in parallel.

    on_game(finished_index, n_positions, winner, seconds) is called as games finish.

    Returns:
        List of (examples, winner) per game, in completion order.
    """
    futures = [executor.submit(_self_play_task, net_path, n_sims, temperature_moves)
               for _ in range(n_games)]
    results = []
    for i, future in enumerate(as_completed(futures)):
        examples, winner, secs = future.result()
        results.append((examples, winner))
        if on_game:
            on_game(i, len(examples), winner, secs)
    return results


def run_pit(
    executor: ProcessPoolExecutor,
    path_a: str,
    path_b: str,
    n_games: int,
    n_sims: int,
    c_puct: float = 1.5,
    opening_moves: int = 6,
    seed: int | None = None,
    on_game: Callable[[int, int, int, int, float], None] | None = None,
) -> float:
    """
    Evaluate net A vs net B in parallel.

    Greedy MCTS is deterministic, so games are diversified by a random opening
    of `opening_moves` sub-moves. Games come in pairs: both nets play each
    opening once from each side, which cancels out unbalanced openings and
    first-mover advantage. An odd n_games is rounded up to the next pair.

    on_game(pair_index, a_player, winner, sub_moves, seconds) is called as games finish.

    Returns:
        Win rate of net A (0.0 – 1.0).
    """
    rng = np.random.default_rng(seed)
    n_pairs = (n_games + 1) // 2
    futures: dict[Future, tuple[int, int]] = {}
    for pair in range(n_pairs):
        opening = random_opening_actions(opening_moves, rng)
        for a_player in (0, 1):
            future = executor.submit(_pit_task, path_a, path_b, opening, a_player, n_sims, c_puct)
            futures[future] = (pair, a_player)

    wins = 0
    for future in as_completed(futures):
        pair, a_player = futures[future]
        winner, sub_moves, secs = future.result()
        wins += winner == a_player
        if on_game:
            on_game(pair, a_player, winner, sub_moves, secs)
    return wins / (2 * n_pairs)


# ── Worker side ───────────────────────────────────────────────────────────────

_nets: dict[str, AZNet] = {}


def _init_worker() -> None:
    signal.signal(signal.SIGINT, signal.SIG_IGN)  # Ctrl+C is handled by the main process
    torch.set_num_threads(1)
    np.random.seed((os.getpid() * 1_000_003 + time.time_ns()) % 2**32)


def _net(path: str) -> AZNet:
    # A path is never rewritten (NetStore uses a new one per version), so caching is safe.
    net = _nets.get(path)
    if net is None:
        if len(_nets) >= 4:
            _nets.clear()
        net = AZNet.load(path).eval()
        _nets[path] = net
    return net


def _self_play_task(net_path: str, n_sims: int, temperature_moves: int):
    t0 = time.perf_counter()
    examples, winner = play_game(_net(net_path), n_sims=n_sims,
                                 temperature_moves=temperature_moves)
    return examples, winner, time.perf_counter() - t0


def _pit_task(path_a: str, path_b: str, opening: list[int], a_player: int,
              n_sims: int, c_puct: float):
    net_a, net_b = _net(path_a), _net(path_b)
    nets = [net_a, net_b] if a_player == 0 else [net_b, net_a]
    t0 = time.perf_counter()
    winner, sub_moves = play_match(replay_opening(opening), nets, n_sims, c_puct)
    return winner, sub_moves, time.perf_counter() - t0
