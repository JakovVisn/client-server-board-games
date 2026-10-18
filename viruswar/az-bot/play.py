"""
League inference bot for AZ models.

Each slot keeps one server connection and plays its games with MCTS; game +
MCTS root are kept per game_id so the tree is reused within a turn.

Usage:
    cd viruswar/az-bot
    ../.venv/bin/python play.py \
        --bot azbot:azbot:latest \
        --bot azbot_prev:azbot_prev:prev:1

Roles:
    latest  — always the newest promoted model
    prev:N  — N-th newest (0=latest, 1=second, ...)
    random  — picks randomly on each reload

Ctrl+C finishes the running games, then exits (games offered meanwhile are
resigned); a second Ctrl+C exits at once.
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

load_dotenv()

import viruswar_sim as sim
from bot_client import ServerConnection
from log import setup as setup_logging
from mcts import make_root, run_mcts, get_action_probs, MCTSNode
from net import AZNet

log = logging.getLogger(__name__)

N_SIMS = int(os.getenv("AZ_N_SIMS_PLAY", "400"))
# A single sub-move slower than this is logged as a warning
SLOW_MOVE_SECS = float(os.getenv("AZ_SLOW_MOVE_SECS", "10"))
RECONNECT_SECS = 3

_RE_FOUND  = re.compile(r"^found:(\d+):(\w+):\w+:(\d+):(\d+):(?:first|second):")
_RE_GO     = re.compile(r"^go:([0-4]+):(\d+):(\d+):")
_RE_RESULT = re.compile(r"^(win|lose):(\d+):(?:(\w+):)?")
_RE_ABORT  = re.compile(r"^abort:(\d+):")
_RE_WRONG  = re.compile(r"^wrongstep:(\d+):")

_shutdown_requested = False
_shutdown_count     = 0


def _handle_signal(sig, frame):
    global _shutdown_count, _shutdown_requested
    _shutdown_count += 1
    if _shutdown_count == 1:
        _shutdown_requested = True
        log.info("shutdown_requested mode=finish_active_games")
    else:
        log.info("shutdown_forced")
        sys.exit(0)


def get_promoted_models(promoted_dir: str) -> list[Path]:
    d = Path(promoted_dir)
    if not d.exists():
        return []
    return sorted(d.glob("promoted_*.pt"), key=lambda p: p.stem, reverse=True)


# ── Per-game state ────────────────────────────────────────────────────────────

class GameState:
    __slots__ = ("game", "root", "w", "h", "opponent", "started", "moves", "think_secs", "models")

    def __init__(self, w: int, h: int, opponent: str):
        self.w    = w
        self.h    = h
        self.game = sim.VirusWarGame(w, h)
        self.root: MCTSNode | None = None
        self.opponent   = opponent
        self.started    = time.monotonic()
        self.moves      = 0      # sub-moves we played
        self.think_secs = 0.0    # total MCTS time spent on this game
        self.models: list[str] = []  # models that played it, in order (hot reload mid-game)


# ── Bot slot ──────────────────────────────────────────────────────────────────

class BotSlot:
    _LOG_EVERY = 20

    def __init__(self, login: str, password: str, role: str,
                 host: str, port: int, promoted_dir: str):
        self.login        = login
        self.password     = password
        self.role         = role
        self.host         = host
        self.port         = port
        self.promoted_dir = promoted_dir

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
        self._active: dict[int, GameState] = {}

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
                self._net        = new_net
                self._net_path   = str(path)
                self._slot_model = path.stem
            self._slot_wins  = 0
            self._slot_games = 0
            log.info(f"model_loaded login={self.login} role={self.role} model={path.name}")
            return True
        except Exception as e:
            log.error(f"model_load_failed login={self.login} role={self.role} path={path} error={e!r}")
            return False

    def check_reload(self):
        self._try_load()

    def is_idle(self) -> bool:
        """No game in progress (read by the main thread during shutdown)."""
        return not self._active

    def interrupt(self):
        with self._conn_lock:
            if self._conn:
                try:
                    self._conn.disconnect()
                except Exception:
                    pass

    # ── inference ─────────────────────────────────────────────────────────

    def _pick_move(self, gs: GameState, board: str, moves_remaining: int) -> int:
        in_sync = board == gs.game.serialize() and moves_remaining == gs.game.moves_remaining()
        if moves_remaining == 3 or not in_sync:
            # Rebuild from the server's board at each turn start, or mid-turn when out of
            # sync (e.g. after wrongstep:), rather than search a wrong tree
            if moves_remaining != 3:
                log.warning(f"desync login={self.login} moves_remaining={moves_remaining}"
                            f" local_moves_remaining={gs.game.moves_remaining()}")
            gs.game = sim.VirusWarGame(gs.w, gs.h)
            gs.game.init_from_board(board, moves_remaining)
            gs.root = make_root(gs.game)

        with self._net_lock:
            net, model = self._net, self._slot_model
        if not gs.models or gs.models[-1] != model:
            gs.models.append(model)

        t0 = time.perf_counter()
        run_mcts(gs.root, net, N_SIMS, add_noise=False)
        pi     = get_action_probs(gs.root, temperature=0)
        action = int(np.argmax(pi))

        gs.game.apply_move(action % gs.w, action // gs.w)
        gs.root = gs.root.children[action]
        gs.root.prior = 1.0

        secs = time.perf_counter() - t0
        gs.moves      += 1
        gs.think_secs += secs
        if secs > SLOW_MOVE_SECS:
            log.warning(f"slow_move login={self.login} move={gs.moves} secs={secs:.1f} sims={N_SIMS}")
        return action

    def _log_result(self, game_id: int, gs: GameState | None, result: str, cause: str) -> None:
        if gs is None:
            log.info(f"game_over login={self.login} id={game_id} result={result}"
                     f" cause={cause} tracked=no")
            return
        avg_ms = gs.think_secs / gs.moves * 1000 if gs.moves else 0.0
        log.info(f"game_over login={self.login} id={game_id} result={result} cause={cause}"
                 f" opponent={gs.opponent} my_moves={gs.moves}"
                 f" secs={time.monotonic() - gs.started:.0f} think_avg_ms={avg_ms:.0f}"
                 f" model={'->'.join(gs.models) or self._slot_model}")

    def _count_result(self, won: bool) -> None:
        self._games_played += 1
        self._slot_games   += 1
        if won:
            self._wins      += 1
            self._slot_wins += 1
        if self._slot_games % self._LOG_EVERY == 0:
            log.info(f"winrate login={self.login} role={self.role}"
                     f" win_rate={self._slot_wins / self._slot_games:.1%}"
                     f" games={self._slot_games} model={self._slot_model}")

    # ── game loop ─────────────────────────────────────────────────────────

    def run(self):
        waiting_logged = False
        while not _shutdown_requested:
            if self._try_load():
                break
            if not waiting_logged:
                log.info(f"waiting_for_models login={self.login} dir={self.promoted_dir}")
                waiting_logged = True
            time.sleep(5)

        if _shutdown_requested:
            return

        log.info(f"slot_start login={self.login} role={self.role} model={self._slot_model}"
                 f" sims={N_SIMS} server={self.host}:{self.port}")

        with self._conn_lock:
            self._conn = ServerConnection(self.host, self.port, self.login, self.password)
            conn = self._conn

        active = self._active

        while not _shutdown_requested:
            try:
                conn.connect()
            except (ConnectionError, OSError) as e:
                log.warning(f"connect_failed login={self.login} error={e!r} retry_secs={RECONNECT_SECS}")
                time.sleep(RECONNECT_SECS)
                continue

            if active:
                log.warning(f"dropped_games login={self.login} count={len(active)}"
                            f" ids={','.join(map(str, sorted(active)))} reason=connection_lost")
            active.clear()
            nomatch_sent = False

            try:
                while True:
                    msg = conn.read_message()
                    if _shutdown_requested and not nomatch_sent:
                        # Sent from this thread, which owns the socket; a found: already
                        # on its way is resigned below
                        conn.stop_matching()
                        nomatch_sent = True

                    m = _RE_FOUND.match(msg)
                    if m:
                        game_id = int(m.group(1))
                        w, h    = int(m.group(3)), int(m.group(4))
                        reason  = ("shutdown" if _shutdown_requested
                                   else "board_size" if (w, h) != (self._net.w, self._net.h)
                                   else None)
                        if reason:
                            # Don't leave the opponent waiting on a game we won't play
                            conn.resign(game_id)
                            log.info(f"resign login={self.login} id={game_id} reason={reason}"
                                     f" board={w}x{h}")
                            continue
                        active[game_id] = GameState(w, h, m.group(2))
                        log.info(f"game_start login={self.login} id={game_id}"
                                 f" opponent={m.group(2)} board={w}x{h}")
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

                    m = _RE_RESULT.match(msg)
                    if m:
                        result, game_id = m.group(1), int(m.group(2))
                        gs = active.pop(game_id, None)
                        self._log_result(game_id, gs, result, m.group(3) or "-")
                        if gs is not None:  # not for a game resigned unplayed
                            self._count_result(result == "win")
                        if _shutdown_requested and not active:
                            break
                        continue

                    m = _RE_ABORT.match(msg)
                    if m:
                        active.pop(int(m.group(1)), None)
                        log.info(f"abort login={self.login} id={m.group(1)}")
                        if _shutdown_requested and not active:
                            break
                        continue

                    m = _RE_WRONG.match(msg)
                    if m:
                        log.warning(f"wrongstep login={self.login} id={m.group(1)}")
                        continue

            except (ConnectionError, OSError) as e:
                if _shutdown_requested:
                    break
                log.warning(f"connection_lost login={self.login} error={e!r}"
                            f" retry_secs={RECONNECT_SECS}")
                conn.disconnect()
                time.sleep(RECONNECT_SECS)
            except Exception:
                # Bug in move selection etc.: record it instead of silently killing the thread
                log.exception(f"error login={self.login} retry_secs={RECONNECT_SECS}")
                conn.disconnect()
                time.sleep(RECONNECT_SECS)
            else:
                break

        conn.disconnect()
        active.clear()
        with self._conn_lock:
            self._conn = None
        log.info(f"slot_stop login={self.login} role={self.role}"
                 f" games={self._games_played} wins={self._wins}")


# ── Main ──────────────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(description="AZ-bot league inference")
    parser.add_argument("--host", default=os.getenv("VIRUSWAR_HOST", "127.0.0.1"))
    parser.add_argument("--port", type=int, default=int(os.getenv("VIRUSWAR_PORT", "1234")))
    parser.add_argument("--promoted-dir", default="models/promoted")
    parser.add_argument("--log", default="models/play_az.log",
                        help="Log file (events, one line each)")
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

    setup_logging("azbot", args.log)

    slots: list[BotSlot] = []
    for spec in args.bots:
        parts = spec.split(":", 3)
        if len(parts) < 3:
            parser.error(f"Invalid bot spec: {spec!r}")
        login, password = parts[0], parts[1]
        role = ":".join(parts[2:])
        slots.append(BotSlot(login, password, role, args.host, args.port, args.promoted_dir))

    os.makedirs(args.promoted_dir, exist_ok=True)
    log.info(f"start bots={','.join(s.login + '/' + s.role for s in slots)}"
             f" promoted_dir={args.promoted_dir} sims={N_SIMS}")

    signal.signal(signal.SIGINT,  _handle_signal)
    signal.signal(signal.SIGTERM, _handle_signal)

    threads = [threading.Thread(target=s.run, name=s.login, daemon=True) for s in slots]
    for t in threads:
        t.start()

    # Slots load the newest model on start, so only later changes trigger a reload
    last_snapshot = [str(p) for p in get_promoted_models(args.promoted_dir)]
    next_check = time.monotonic() + 5
    while not _shutdown_requested:
        time.sleep(0.5)
        if time.monotonic() < next_check:
            continue
        next_check = time.monotonic() + 5
        current = [str(p) for p in get_promoted_models(args.promoted_dir)]
        if current != last_snapshot:
            last_snapshot = current
            log.info(f"promoted_changed models={len(current)}"
                     f" newest={Path(current[0]).name if current else '-'}")
            for s in slots:
                s.check_reload()

    # Graceful shutdown: a slot leaves on its own after its last game ends; an idle
    # slot is blocked reading its socket, so close that to let it exit
    while any(t.is_alive() for t in threads):
        for s in slots:
            if s.is_idle():
                s.interrupt()
        time.sleep(0.5)
    log.info("stopped")


if __name__ == "__main__":
    main()
