"""
League inference script — runs multiple bot roles concurrently.
Each bot loads its model from the promoted folder based on its role.

Usage:
    cd viruswar/rl-bot
    ../.venv/bin/python play.py \
        --bot rlbot:rlbot:latest \
        --bot rlbot_prev_1:rlbot_prev_1:prev:1 \
        --bot rlbot_prev_2:rlbot_prev_2:prev:2 \
        --bot rlbot_prev_3:rlbot_prev_3:prev:3 \
        --bot rlbot_rand:rlbot_rand:random

Roles:
    latest   — always uses the newest promoted model
    prev:N   — uses the N-th newest model (0=latest, 1=second, ...)
    random   — picks a random model from the folder on each reload
"""

import argparse
import logging
import os
import random
import re
import signal
import sys
import threading
import time
from pathlib import Path

import numpy as np
from dotenv import load_dotenv
from sb3_contrib import MaskablePPO

load_dotenv()

from bot_client import ServerConnection
from log import setup as setup_logging
from viruswar_sim import valid_action_mask

log = logging.getLogger(__name__)


_RE_FOUND = re.compile(r"found:(\d+):(\w+):\w+:(\d+):(\d+):(?:first|second):")
_RE_GO    = re.compile(r"go:([0-4]+):(\d+):(\d+):")
_RE_WIN   = re.compile(r"win:(\d+):")
_RE_LOSE  = re.compile(r"lose:(\d+):")
_RE_ABORT = re.compile(r"abort:(\d+):")

_shutdown_requested = False
_shutdown_count = 0


def _handle_signal(sig, frame):
    global _shutdown_count, _shutdown_requested
    _shutdown_count += 1
    if _shutdown_count == 1:
        _shutdown_requested = True
        log.info("shutdown_requested")
    else:
        log.info("shutdown_forced")
        sys.exit(0)


def build_obs(board: str, moves: int, w: int, h: int) -> np.ndarray:
    board_arr = np.array([int(c) for c in board], dtype=np.int8).reshape(h, w)
    obs = np.zeros((6, h, w), dtype=np.float32)
    obs[0] = (board_arr == 1).astype(np.float32)
    obs[1] = (board_arr == 3).astype(np.float32)
    obs[2] = (board_arr == 2).astype(np.float32)
    obs[3] = (board_arr == 4).astype(np.float32)
    obs[4] = (board_arr == 0).astype(np.float32)
    obs[5].fill(moves / 3.0)
    return obs


def get_promoted_models(promoted_dir: str) -> list[Path]:
    """Returns promoted model paths sorted newest first (by filename)."""
    d = Path(promoted_dir)
    if not d.exists():
        return []
    return sorted(d.glob("promoted_*.zip"), key=lambda p: p.stem, reverse=True)


class BotSlot:
    """One bot instance: owns a connection, a model, and a game loop thread."""

    _LOG_EVERY = 20  # log win rate every N games per slot

    def __init__(self, login: str, password: str, role: str,
                 host: str, port: int, promoted_dir: str):
        self.login = login
        self.password = password
        self.role = role          # "latest" | "prev:N" | "random"
        self.host = host
        self.port = port
        self.promoted_dir = promoted_dir

        self._model = None
        self._model_path: str | None = None
        self._model_lock = threading.Lock()

        self._games_played = 0
        self._wins = 0
        self._infer_times: list[float] = []

        self._slot_wins = 0
        self._slot_games = 0
        self._slot_model = ""

        self._conn: ServerConnection | None = None
        self._conn_lock = threading.Lock()

    # ── model management ──────────────────────────────────────────────────

    def _pick_path(self) -> Path | None:
        models = get_promoted_models(self.promoted_dir)
        if not models:
            return None
        if self.role == "latest":
            return models[0]
        if self.role.startswith("prev:"):
            idx = int(self.role.split(":")[1])
            return models[min(idx, len(models) - 1)]
        if self.role == "random":
            return random.choice(models)
        return models[0]

    def _try_load(self) -> bool:
        """Load model if target path changed. Returns True if reloaded."""
        path = self._pick_path()
        if path is None or str(path) == self._model_path:
            return False
        try:
            new_model = MaskablePPO.load(str(path))
            with self._model_lock:
                self._model = new_model
                self._model_path = str(path)
            self._slot_wins = 0
            self._slot_games = 0
            self._slot_model = path.stem
            log.info(f"model_loaded login={self.login} role={self.role} model={path.name}")
            return True
        except Exception as e:
            log.error(f"model_load_failed login={self.login} path={path} error={e!r}")
            return False

    def check_reload(self):
        """Called by main thread when promoted folder changes."""
        self._try_load()

    def interrupt(self):
        """Close connection to unblock read_message() during shutdown."""
        with self._conn_lock:
            if self._conn:
                try:
                    self._conn.disconnect()
                except Exception:
                    pass

    # ── game loop ─────────────────────────────────────────────────────────

    def run(self):
        # Wait until at least one model is available
        while not _shutdown_requested:
            if self._try_load():
                break
            log.info(f"waiting_for_models login={self.login} dir={self.promoted_dir}")
            time.sleep(5)

        if _shutdown_requested:
            return

        with self._conn_lock:
            self._conn = ServerConnection(self.host, self.port, self.login, self.password)
            conn = self._conn
        active: set[int] = set()
        game_sizes: dict[int, tuple[int, int]] = {}

        while not _shutdown_requested:
            try:
                conn.connect()
            except (ConnectionError, OSError) as e:
                log.warning(f"connect_failed login={self.login} error={e!r} retry_secs=3")
                time.sleep(3)
                continue

            active.clear()
            game_sizes.clear()

            try:
                while True:
                    msg = conn.read_message()

                    m = _RE_FOUND.match(msg)
                    if m:
                        game_id = int(m.group(1))
                        game_sizes[game_id] = (int(m.group(3)), int(m.group(4)))
                        if not _shutdown_requested:
                            active.add(game_id)
                            log.info(f"game_start login={self.login} id={game_id} opponent={m.group(2)}")
                        continue

                    m = _RE_GO.match(msg)
                    if m:
                        board, moves, game_id = m.group(1), int(m.group(2)), int(m.group(3))
                        w, h = game_sizes.get(game_id, (10, 10))
                        obs  = build_obs(board, moves, w, h)
                        mask = np.array(valid_action_mask(board, w, h), dtype=bool)

                        t0 = time.perf_counter()
                        with self._model_lock:
                            action, _ = self._model.predict(
                                obs, action_masks=mask, deterministic=True)
                        self._infer_times.append((time.perf_counter() - t0) * 1000)
                        if len(self._infer_times) >= 200:
                            avg = sum(self._infer_times) / len(self._infer_times)
                            log.info(f"inference login={self.login} avg_ms={avg:.1f}")
                            self._infer_times.clear()

                        conn.send_move(int(action) % w, int(action) // w, game_id)
                        continue

                    m = _RE_ABORT.match(msg)
                    if m:
                        active.discard(int(m.group(1)))
                        log.info(f"abort login={self.login} id={m.group(1)}")
                        if _shutdown_requested and not active:
                            break
                        continue

                    m = _RE_WIN.match(msg)
                    if m:
                        active.discard(int(m.group(1)))
                        self._games_played += 1
                        self._wins += 1
                        self._slot_games += 1
                        self._slot_wins += 1
                        log.info(f"game_over login={self.login} result=win"
                                 f" win_rate={self._wins / self._games_played:.1%}"
                                 f" games={self._games_played}")
                        if self._slot_games % self._LOG_EVERY == 0:
                            log.info(f"winrate login={self.login} role={self.role}"
                                     f" win_rate={self._slot_wins / self._slot_games:.1%}"
                                     f" games={self._slot_games} model={self._slot_model}")
                        if _shutdown_requested and not active:
                            break
                        continue

                    m = _RE_LOSE.match(msg)
                    if m:
                        active.discard(int(m.group(1)))
                        self._games_played += 1
                        self._slot_games += 1
                        log.info(f"game_over login={self.login} result=lose"
                                 f" win_rate={self._wins / self._games_played:.1%}"
                                 f" games={self._games_played}")
                        if self._slot_games % self._LOG_EVERY == 0:
                            log.info(f"winrate login={self.login} role={self.role}"
                                     f" win_rate={self._slot_wins / self._slot_games:.1%}"
                                     f" games={self._slot_games} model={self._slot_model}")
                        if _shutdown_requested and not active:
                            break
                        continue

            except (ConnectionError, OSError) as e:
                if _shutdown_requested:
                    break
                log.warning(f"connection_lost login={self.login} error={e!r} retry_secs=3")
                conn.disconnect()
                time.sleep(3)
            except Exception:
                log.exception(f"error login={self.login} retry_secs=3")
                conn.disconnect()
                time.sleep(3)
            else:
                break  # clean shutdown

        conn.disconnect()
        with self._conn_lock:
            self._conn = None


def main():
    parser = argparse.ArgumentParser(description="League inference — multiple bot roles")
    parser.add_argument("--host", default=os.getenv("VIRUSWAR_HOST", "127.0.0.1"))
    parser.add_argument("--port", type=int, default=int(os.getenv("VIRUSWAR_PORT", "1234")))
    parser.add_argument("--promoted-dir", default="models/promoted",
                        help="Folder containing promoted_*.zip files")
    parser.add_argument("--log", default="models/play_rl.log",
                        help="Log file (events, one line each)")
    parser.add_argument("--bot", action="append", dest="bots", default=[],
                        metavar="LOGIN:PASSWORD:ROLE",
                        help="Bot spec: login:password:latest | login:password:prev:N "
                             "| login:password:random  (repeatable)")
    args = parser.parse_args()

    setup_logging("rlbot", args.log)

    if not args.bots:
        parser.error(
            "Specify at least one --bot, e.g.:\n"
            "  --bot rlbot:rlbot:latest\n"
            "  --bot rlbot_prev_1:rlbot_prev_1:prev:1\n"
            "  --bot rlbot_rand:rlbot_rand:random"
        )

    slots: list[BotSlot] = []
    for spec in args.bots:
        parts = spec.split(":", 3)
        if len(parts) < 3:
            parser.error(f"Invalid bot spec: {spec!r}")
        login, password = parts[0], parts[1]
        role = ":".join(parts[2:])
        slots.append(BotSlot(login, password, role,
                              args.host, args.port, args.promoted_dir))

    os.makedirs(args.promoted_dir, exist_ok=True)

    signal.signal(signal.SIGINT,  _handle_signal)
    signal.signal(signal.SIGTERM, _handle_signal)

    threads = [threading.Thread(target=s.run, name=s.login, daemon=True) for s in slots]
    for t in threads:
        t.start()

    # Poll promoted-dir; slots reload when it changes
    last_snapshot: list[str] = []
    try:
        while True:
            time.sleep(5)
            if _shutdown_requested:
                break
            current = [str(p) for p in get_promoted_models(args.promoted_dir)]
            if current != last_snapshot:
                last_snapshot = current
                log.info(f"promoted_changed models={len(current)}")
                for s in slots:
                    s.check_reload()
    except KeyboardInterrupt:
        pass

    for s in slots:
        s.interrupt()
    for t in threads:
        t.join(timeout=10)
    log.info("stopped")


if __name__ == "__main__":
    main()
