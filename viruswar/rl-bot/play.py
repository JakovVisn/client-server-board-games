"""
Inference script — loads a trained CNN model and plays continuously on the server.

Supports multiple simultaneous games via GAMEID multiplexing.
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
from training_logger import TrainingLogger


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


# ─────────────────────────────────────────────────────────────
# 🔥 CNN observation builder
# ─────────────────────────────────────────────────────────────
def build_obs(board: str, moves: int, w: int, h: int) -> np.ndarray:
    """
    Returns observation of shape (6, h, w), float32
    Channels:
      0–4: one-hot for board cell values 0–4
      5  : moves_remaining normalized (moves / 3.0)
    """
    board_arr = np.array([int(c) for c in board], dtype=np.int8).reshape(h, w)

    obs = np.zeros((6, h, w), dtype=np.float32)

    # channel order matches env.py _obs()
    obs[0] = (board_arr == 1).astype(np.float32)  # my regular cells
    obs[1] = (board_arr == 3).astype(np.float32)  # my fortress cells
    obs[2] = (board_arr == 2).astype(np.float32)  # enemy regular cells
    obs[3] = (board_arr == 4).astype(np.float32)  # enemy fortress cells
    obs[4] = (board_arr == 0).astype(np.float32)  # empty cells
    obs[5].fill(moves / 3.0)

    return obs


def main():
    parser = argparse.ArgumentParser(description="Run trained VirusWar CNN bot")
    parser.add_argument("--host",     default=os.getenv("VIRUSWAR_HOST", "127.0.0.1"))
    parser.add_argument("--port",     type=int, default=int(os.getenv("VIRUSWAR_PORT", "1234")))
    parser.add_argument("--login",    default=os.getenv("VIRUSWAR_PLAY_LOGIN"))
    parser.add_argument("--password", default=os.getenv("VIRUSWAR_PLAY_PASSWORD"))
    parser.add_argument("--opponent", default=os.getenv("VIRUSWAR_PLAY_OPPONENT", ""))
    parser.add_argument("--model",    required=True)
    parser.add_argument("--promote",  default="")
    parser.add_argument("--log",      default="models/training_log.md")
    args = parser.parse_args()

    if args.login is None or args.password is None:
        parser.error("--login and --password required")

    tlog = TrainingLogger(args.log)
    tlog.log_note(f"play.py started: model=`{args.model}` opponent=`{args.opponent or 'any'}`")

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
            print(f"[{datetime.now().strftime('%H:%M:%S')}][play] Reloaded promoted model")
            tlog.log_note(f"play.py reloaded promoted model: `{promote_path}`")
            games_played = 0
            wins = 0

    signal.signal(signal.SIGINT,  _handle_signal)
    signal.signal(signal.SIGTERM, _handle_signal)

    conn = ServerConnection(args.host, args.port, args.login, args.password)

    active: set[int] = set()
    game_sizes: dict[int, tuple[int, int]] = {}
    games_played = 0
    wins = 0

    try:
        while True:  # reconnect loop
            if _shutdown_requested:
                break

            try:
                conn.connect(preferred_opponent=args.opponent)
            except (ConnectionError, OSError) as e:
                print(f"[{datetime.now().strftime('%H:%M:%S')}][play] Connection failed: {e}. Retrying in 3s...")
                time.sleep(3)
                continue

            active.clear()
            game_sizes.clear()

            try:
                while True:  # message loop
                    msg = conn.read_message()

                    # game start
                    m = _RE_FOUND.match(msg)
                    if m:
                        game_id, opponent = int(m.group(1)), m.group(2)
                        game_sizes[game_id] = (int(m.group(3)), int(m.group(4)))
                        if not _shutdown_requested:
                            active.add(game_id)
                            print(f"[play] game {game_id} started vs {opponent}")
                        continue

                    m = _RE_GO.match(msg)
                    if m:
                        board, moves, game_id = m.group(1), int(m.group(2)), int(m.group(3))
                        w, h = game_sizes.get(game_id, (10, 10))

                        obs = build_obs(board, moves, w, h)
                        mask = np.array(valid_action_mask(board, w, h), dtype=bool)
                        action, _ = model.predict(obs, action_masks=mask, deterministic=True)
                        conn.send_move(int(action) % w, int(action) // w, game_id)
                        continue

                    # win
                    m = _RE_WIN.match(msg)
                    if m:
                        active.discard(int(m.group(1)))
                        games_played += 1
                        wins += 1
                        print(f"[play] WIN  winrate={wins/games_played:.1%}")
                        check_promote()
                        if _shutdown_requested and not active:
                            break
                        continue

                    # lose
                    m = _RE_LOSE.match(msg)
                    if m:
                        active.discard(int(m.group(1)))
                        games_played += 1
                        print(f"[play] LOSS winrate={wins/games_played:.1%}")
                        check_promote()
                        if _shutdown_requested and not active:
                            break
                        continue

            except (ConnectionError, OSError) as e:
                if _shutdown_requested:
                    break
                print(f"[{datetime.now().strftime('%H:%M:%S')}][play] Connection lost: {e}. Reconnecting in 3s...")
                conn.disconnect()
                time.sleep(3)
            else:
                break  # clean exit via shutdown

    finally:
        conn.disconnect()


if __name__ == "__main__":
    main()
