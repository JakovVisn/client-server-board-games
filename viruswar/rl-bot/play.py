"""
Inference script — loads a trained model and plays continuously on the server.

Single TCP connection, supports multiple simultaneous games via GAMEID multiplexing.
Requires login to be registered as a bot in auth.txt (login:password:bot).

Credentials are read from .env (see .env.example). CLI args override env vars.

Usage:
  python play.py --model models/promote.zip
  python play.py --model models/promote.zip --opponent <login>
"""

import argparse
import os
import re
import signal
import sys
import time
from datetime import datetime

from dotenv import load_dotenv
load_dotenv()

import numpy as np
from sb3_contrib import MaskablePPO

from bot_client import ServerConnection
from game_logic import valid_action_mask


_RE_FOUND     = re.compile(r"found:(\d+):(\w+):\w+:(\d+):(\d+):(?:first|second):")
_RE_GO        = re.compile(r"go:([0-4]+):(\d+):(\d+):")
_RE_WRONGSTEP = re.compile(r"wrongstep:(\d+):")
_RE_WIN       = re.compile(r"win:(\d+):")
_RE_LOSE      = re.compile(r"lose:(\d+):")


_shutdown_count = 0
_shutdown_requested = False


def _handle_signal(sig, frame):
    global _shutdown_count, _shutdown_requested
    _shutdown_count += 1
    if _shutdown_count == 1:
        _shutdown_requested = True
        print(f"\n[{datetime.now().strftime('%H:%M:%S')}][play] Graceful shutdown — finishing active games...")
    else:
        print(f"\n[{datetime.now().strftime('%H:%M:%S')}][play] Forced shutdown.")
        sys.exit(0)


def main():
    parser = argparse.ArgumentParser(description="Run the trained VirusWar RL bot (multi-game)")
    parser.add_argument("--host",     default=os.getenv("VIRUSWAR_HOST", "127.0.0.1"))
    parser.add_argument("--port",     type=int, default=int(os.getenv("VIRUSWAR_PORT", "1234")))
    parser.add_argument("--login",    default=os.getenv("VIRUSWAR_PLAY_LOGIN"))
    parser.add_argument("--password", default=os.getenv("VIRUSWAR_PLAY_PASSWORD"))
    parser.add_argument("--opponent", default=os.getenv("VIRUSWAR_PLAY_OPPONENT", ""),
                        help="Login of opponent to request (empty = anyone)")
    parser.add_argument("--model",    required=True,
                        help="Path to saved model (.zip)")
    parser.add_argument("--promote",  default="",
                        help="Watch this file and reload model when it changes")
    args = parser.parse_args()

    if args.login is None or args.password is None:
        parser.error("--login and --password are required (or set VIRUSWAR_PLAY_LOGIN / VIRUSWAR_PLAY_PASSWORD in .env)")

    model = MaskablePPO.load(args.model)
    print(f"[{datetime.now().strftime('%H:%M:%S')}][play] Loaded model from {args.model}")

    promote_path = args.promote
    last_mtime = os.path.getmtime(promote_path) if promote_path and os.path.exists(promote_path) else 0.0

    def check_promote():
        nonlocal model, last_mtime, games_played, wins
        if not promote_path or not os.path.exists(promote_path):
            return
        mtime = os.path.getmtime(promote_path)
        if mtime > last_mtime:
            last_mtime = mtime
            model = MaskablePPO.load(promote_path)
            print(f"[{datetime.now().strftime('%H:%M:%S')}][play] Reloaded model — previous: {wins}/{games_played} wins"
                  f" ({wins/games_played:.1%})" if games_played else
                  f"[{datetime.now().strftime('%H:%M:%S')}][play] Reloaded model from {promote_path}")
            games_played = 0
            wins = 0

    signal.signal(signal.SIGINT,  _handle_signal)
    signal.signal(signal.SIGTERM, _handle_signal)

    conn = ServerConnection(args.host, args.port, args.login, args.password)
    while True:
        try:
            conn.connect(preferred_opponent=args.opponent)
            break
        except (ConnectionError, OSError) as e:
            print(f"[{datetime.now().strftime('%H:%M:%S')}][play] Connection failed: {e}. Retrying in 3s...")
            time.sleep(3)

    active: set[int] = set()
    game_sizes: dict[int, tuple[int, int]] = {}
    games_played = 0
    wins = 0

    try:
        while True:
            try:
                msg = conn.read_message()
            except (ConnectionError, OSError) as e:
                if _shutdown_requested:
                    break
                print(f"[{datetime.now().strftime('%H:%M:%S')}][play] Connection lost: {e}. Reconnecting...")
                active.clear()
                while not _shutdown_requested:
                    time.sleep(3)
                    try:
                        conn.connect(preferred_opponent=args.opponent)
                        break
                    except (ConnectionError, OSError) as e:
                        print(f"[{datetime.now().strftime('%H:%M:%S')}][play] Reconnect failed: {e}. Retrying in 3s...")
                if _shutdown_requested:
                    break
                continue

            m = _RE_FOUND.match(msg)
            if m:
                game_id, opponent = int(m.group(1)), m.group(2)
                game_sizes[game_id] = (int(m.group(3)), int(m.group(4)))
                active.add(game_id)
                print(f"[{datetime.now().strftime('%H:%M:%S')}][play] game {game_id:5d} started vs {opponent}  active={len(active)}")
                continue

            m = _RE_GO.match(msg)
            if m:
                board, moves, game_id = m.group(1), int(m.group(2)), int(m.group(3))
                w, h = game_sizes.get(game_id, (10, 10))
                obs = np.array([int(c) for c in board] + [moves], dtype=np.int8)
                mask = np.array(valid_action_mask(board, w, h), dtype=bool)
                action, _ = model.predict(obs, action_masks=mask, deterministic=True)
                conn.send_move(int(action) % w, int(action) // w, game_id)
                continue

            if _RE_WRONGSTEP.match(msg):
                print(f"[{datetime.now().strftime('%H:%M:%S')}][play] WARNING: wrongstep in game {_RE_WRONGSTEP.match(msg).group(1)}")
                continue

            if msg.startswith("abort:"):
                game_id = int(msg.split(":")[1])
                active.discard(game_id)
                if _shutdown_requested and not active:
                    break
                continue

            m = _RE_WIN.match(msg)
            if m:
                active.discard(int(m.group(1)))
                games_played += 1
                wins += 1
                print(f"[{datetime.now().strftime('%H:%M:%S')}][play] game {games_played:5d}  WIN   win_rate={wins/games_played:.1%}"
                      f"  ({wins}/{games_played})  active={len(active)}")
                check_promote()
                if _shutdown_requested and not active:
                    break
                continue

            m = _RE_LOSE.match(msg)
            if m:
                active.discard(int(m.group(1)))
                games_played += 1
                print(f"[{datetime.now().strftime('%H:%M:%S')}][play] game {games_played:5d}  LOSS  win_rate={wins/games_played:.1%}"
                      f"  ({wins}/{games_played})  active={len(active)}")
                check_promote()
                if _shutdown_requested and not active:
                    break
                continue

    except KeyboardInterrupt:
        pass
    finally:
        summary = (f"{wins}/{games_played} wins ({wins/games_played:.1%})"
                   if games_played else "no games played")
        print(f"[{datetime.now().strftime('%H:%M:%S')}][play] Stopped. {summary}")
        conn.disconnect()


if __name__ == "__main__":
    main()
