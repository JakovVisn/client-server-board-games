"""
League inference script for the AlphaZero bot.

Each bot slot maintains a persistent TCP connection and plays games
using MCTS guided by AZNet. State (game + MCTS root) is tracked
per game_id so tree reuse works across sub-moves within a turn.

Usage:
    cd viruswar/az-bot
    ../.venv/bin/python play.py \
        --bot azbot:azbot:latest \
        --bot azbot_prev:azbot_prev:prev:1

Roles:
    latest  — always the newest promoted model
    prev:N  — N-th newest (0=latest, 1=second, ...)
    random  — picks randomly on each reload
"""

import argparse
import os
import random
import re
import signal
import sys
import threading
import time
from datetime import datetime
from pathlib import Path

import numpy as np
from dotenv import load_dotenv

load_dotenv()

import viruswar_sim as sim
from bot_client import ServerConnection
from mcts import make_root, run_mcts, get_action_probs, MCTSNode
from net import AZNet, board_to_obs
from training_logger import TrainingLogger

N_SIMS = int(os.getenv("AZ_N_SIMS_PLAY", "400"))

_RE_FOUND = re.compile(r"found:(\d+):(\w+):\w+:(\d+):(\d+):(?:first|second):")
_RE_GO    = re.compile(r"go:([0-4]+):(\d+):(\d+):")
_RE_WIN   = re.compile(r"win:(\d+):")
_RE_LOSE  = re.compile(r"lose:(\d+):")

_shutdown_requested = False
_shutdown_count     = 0


def _ts() -> str:
    return datetime.now().strftime("%H:%M:%S")


def _handle_signal(sig, frame):
    global _shutdown_count, _shutdown_requested
    _shutdown_count += 1
    if _shutdown_count == 1:
        _shutdown_requested = True
        print(f"\n[{_ts()}][play] Graceful shutdown — finishing active games...")
    else:
        print(f"\n[{_ts()}][play] Forced shutdown.")
        sys.exit(0)


def get_promoted_models(promoted_dir: str) -> list[Path]:
    d = Path(promoted_dir)
    if not d.exists():
        return []
    return sorted(d.glob("promoted_*.pt"), key=lambda p: p.stem, reverse=True)


# ── Per-game state ────────────────────────────────────────────────────────────

class GameState:
    __slots__ = ("game", "root", "w", "h")

    def __init__(self, w: int, h: int):
        self.w    = w
        self.h    = h
        self.game = sim.VirusWarGame(w, h)
        self.root: MCTSNode | None = None


# ── Bot slot ──────────────────────────────────────────────────────────────────

class BotSlot:
    _LOG_EVERY = 20

    def __init__(self, login: str, password: str, role: str,
                 host: str, port: int, promoted_dir: str, log_path: str = ""):
        self.login        = login
        self.password     = password
        self.role         = role
        self.host         = host
        self.port         = port
        self.promoted_dir = promoted_dir
        self._log_path    = log_path

        self._net: AZNet | None = None
        self._net_path: str | None = None
        self._net_lock  = threading.Lock()

        self._games_played = 0
        self._wins         = 0
        self._slot_wins    = 0
        self._slot_games   = 0
        self._slot_model   = ""

        self._conn: ServerConnection | None = None
        self._conn_lock = threading.Lock()

    # ── logging ───────────────────────────────────────────────────────────

    def _log(self, msg: str) -> None:
        if not self._log_path:
            return
        ts = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        try:
            with open(self._log_path, "a", encoding="utf-8") as f:
                f.write(f"- `{ts}` {msg}\n")
        except OSError:
            pass

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
        path = self._pick_path()
        if path is None or str(path) == self._net_path:
            return False
        try:
            new_net = AZNet.load(str(path))
            new_net.eval()
            with self._net_lock:
                self._net      = new_net
                self._net_path = str(path)
            self._slot_wins  = 0
            self._slot_games = 0
            self._slot_model = path.stem
            print(f"[{_ts()}][{self.login}] loaded {path.name}")
            return True
        except Exception as e:
            print(f"[{_ts()}][{self.login}] load failed: {e}")
            return False

    def check_reload(self):
        self._try_load()

    def interrupt(self):
        with self._conn_lock:
            if self._conn:
                try:
                    self._conn.disconnect()
                except Exception:
                    pass

    # ── inference ─────────────────────────────────────────────────────────

    def _pick_move(self, gs: GameState, board: str, moves_remaining: int) -> int:
        if moves_remaining == 3:
            # New turn: sync game state with server's authoritative board
            gs.game.init_from_board(board)
            gs.root = make_root(gs.game)

        with self._net_lock:
            net = self._net

        run_mcts(gs.root, net, N_SIMS, add_noise=False)
        pi     = get_action_probs(gs.root, temperature=0)
        action = int(np.argmax(pi))

        # Apply move to local game and reuse subtree
        gs.game.apply_move(action % gs.w, action // gs.w)
        gs.root = gs.root.children[action]
        gs.root.prior = 1.0

        return action

    # ── game loop ─────────────────────────────────────────────────────────

    def run(self):
        while not _shutdown_requested:
            if self._try_load():
                break
            print(f"[{_ts()}][{self.login}] waiting for models in {self.promoted_dir}...")
            time.sleep(5)

        if _shutdown_requested:
            return

        with self._conn_lock:
            self._conn = ServerConnection(self.host, self.port, self.login, self.password)
            conn = self._conn

        active: dict[int, GameState] = {}

        while not _shutdown_requested:
            try:
                conn.connect()
            except (ConnectionError, OSError) as e:
                msg = f"connect failed: {e}"
                print(f"[{_ts()}][{self.login}] {msg}. Retry in 3s...")
                self._log(f"play [{self.login}] reconnect  {msg}")
                time.sleep(3)
                continue

            active.clear()

            try:
                while True:
                    msg = conn.read_message()

                    m = _RE_FOUND.match(msg)
                    if m:
                        game_id = int(m.group(1))
                        w, h    = int(m.group(3)), int(m.group(4))
                        if not _shutdown_requested:
                            active[game_id] = GameState(w, h)
                            print(f"[{_ts()}][{self.login}] game {game_id} vs {m.group(2)}")
                        continue

                    m = _RE_GO.match(msg)
                    if m:
                        board, moves_remaining, game_id = (
                            m.group(1), int(m.group(2)), int(m.group(3)))
                        gs = active.get(game_id)
                        if gs is None:
                            continue

                        action = self._pick_move(gs, board, moves_remaining)
                        conn.send_move(action % gs.w, action // gs.w, game_id)
                        continue

                    if msg.startswith("abort:"):
                        game_id_str = msg.split(":")[1]
                        active.pop(int(game_id_str), None)
                        print(f"[{_ts()}][{self.login}] abort game {game_id_str}")
                        if _shutdown_requested and not active:
                            break
                        continue

                    m = _RE_WIN.match(msg)
                    if m:
                        active.pop(int(m.group(1)), None)
                        self._games_played += 1
                        self._wins         += 1
                        self._slot_games   += 1
                        self._slot_wins    += 1
                        print(f"[{_ts()}][{self.login}] WIN  "
                              f"wr={self._wins/self._games_played:.1%} "
                              f"({self._wins}/{self._games_played})")
                        if self._slot_games % self._LOG_EVERY == 0:
                            self._log(f"play [{self.login}/{self.role}] winrate"
                                      f"  wr={self._slot_wins/self._slot_games:.1%}"
                                      f"  games={self._slot_games}"
                                      f"  model={self._slot_model}")
                        if _shutdown_requested and not active:
                            break
                        continue

                    m = _RE_LOSE.match(msg)
                    if m:
                        active.pop(int(m.group(1)), None)
                        self._games_played += 1
                        self._slot_games   += 1
                        print(f"[{_ts()}][{self.login}] LOSS "
                              f"wr={self._wins/self._games_played:.1%} "
                              f"({self._wins}/{self._games_played})")
                        if self._slot_games % self._LOG_EVERY == 0:
                            self._log(f"play [{self.login}/{self.role}] winrate"
                                      f"  wr={self._slot_wins/self._slot_games:.1%}"
                                      f"  games={self._slot_games}"
                                      f"  model={self._slot_model}")
                        if _shutdown_requested and not active:
                            break
                        continue

            except (ConnectionError, OSError) as e:
                if _shutdown_requested:
                    break
                msg = f"connection lost: {e}"
                print(f"[{_ts()}][{self.login}] {msg}. Reconnecting in 3s...")
                self._log(f"play [{self.login}] reconnect  {msg}")
                conn.disconnect()
                time.sleep(3)
            else:
                break

        conn.disconnect()
        with self._conn_lock:
            self._conn = None


# ── Main ──────────────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(description="AZ-bot league inference")
    parser.add_argument("--host", default=os.getenv("VIRUSWAR_HOST", "127.0.0.1"))
    parser.add_argument("--port", type=int, default=int(os.getenv("VIRUSWAR_PORT", "1234")))
    parser.add_argument("--promoted-dir", default="models/promoted")
    parser.add_argument("--log", default="models/training_log_az.md")
    parser.add_argument("--bot", action="append", dest="bots", default=[],
                        metavar="LOGIN:PASSWORD:ROLE",
                        help="Bot spec: login:password:latest | login:password:prev:N "
                             "| login:password:random  (repeatable)")
    args = parser.parse_args()

    if not args.bots:
        parser.error(
            "Specify at least one --bot, e.g.:\n"
            "  --bot azbot:azbot:latest\n"
            "  --bot azbot_prev:azbot_prev:prev:1"
        )

    slots: list[BotSlot] = []
    for spec in args.bots:
        parts = spec.split(":", 3)
        if len(parts) < 3:
            parser.error(f"Invalid bot spec: {spec!r}")
        login, password = parts[0], parts[1]
        role = ":".join(parts[2:])
        slots.append(BotSlot(login, password, role,
                             args.host, args.port, args.promoted_dir, args.log))

    os.makedirs(args.promoted_dir, exist_ok=True)

    signal.signal(signal.SIGINT,  _handle_signal)
    signal.signal(signal.SIGTERM, _handle_signal)

    threads = [threading.Thread(target=s.run, name=s.login, daemon=True) for s in slots]
    for t in threads:
        t.start()

    last_snapshot: list[str] = []
    try:
        while True:
            time.sleep(5)
            if _shutdown_requested:
                break
            current = [str(p) for p in get_promoted_models(args.promoted_dir)]
            if current != last_snapshot:
                last_snapshot = current
                print(f"[{_ts()}][play] promoted folder: {len(current)} models — reloading bots")
                for s in slots:
                    s.check_reload()
    except KeyboardInterrupt:
        pass

    for s in slots:
        s.interrupt()
    for t in threads:
        t.join(timeout=10)
    print(f"[{_ts()}][play] stopped.")


if __name__ == "__main__":
    main()
