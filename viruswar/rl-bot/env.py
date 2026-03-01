"""
Gymnasium environment for VirusWar.

Connects as a regular player and requests a specific opponent.
Each call to step() corresponds to one go: message — one move within a turn.

Observation: flat array of length w*h+1
  - indices 0..w*h-1: board cell values (0–4, from current player's perspective)
  - index  w*h:       moves_remaining in current turn (1–3)

Action: integer in [0, w*h), mapped as  x = action % w,  y = action // w

Reward: +1 on win, -1 on loss; shaped per step:
  regular cell gain/loss: ±0.01 each  ('1' cells can be recaptured by opponent)
  fortress gain:          +0.03 each  ('3'/CatchX cells are permanent, never recaptured)
"""

import re
import time
from datetime import datetime

import gymnasium as gym
import numpy as np
from gymnasium import spaces

from bot_client import ServerConnection
from game_logic import valid_action_mask

# Bot protocol: found:GAMEID:OPPONENT:GAMENAME:W:H:role:
_RE_FOUND      = re.compile(r"found:(\d+):(\w+):\w+:(\d+):(\d+):(?:first|second):")
# Bot protocol: go:BOARD:MOVES:GAMEID:
_RE_GO         = re.compile(r"go:([0-4]+):(\d+):(\d+):")
# Bot protocol: field1:TYPE:X:Y:GAMEID:
_RE_FIELD1     = re.compile(r"field1:\w+:-?\d+:-?\d+:\d+:")
# Bot protocol: field2:TYPE:X:Y:GAMEID:
_RE_FIELD2     = re.compile(r"field2:(\w+):-?\d+:-?\d+:\d+:")
# Bot protocol: wrongstep:GAMEID:
_RE_WRONGSTEP  = re.compile(r"wrongstep:\d+:")


class VirusWarEnv(gym.Env):
    metadata = {}

    def __init__(self, host: str, port: int, login: str, password: str,
                 opponent: str = "",
                 board_w: int = 10, board_h: int = 10):
        super().__init__()
        self._client = ServerConnection(host, port, login, password)
        self._opponent = opponent
        self._connected = False

        self.w = board_w
        self.h = board_h
        self._update_spaces()

        self._board = "0" * (self.w * self.h)
        self._moves_remaining = 3
        self._game_id = 0

    # ── Gymnasium interface ───────────────────────────────────────────────────

    def reset(self, *, seed=None, options=None):
        super().reset(seed=seed)
        while True:
            try:
                if not self._connected:
                    self._client.connect(preferred_opponent=self._opponent)
                    self._connected = True
                self._board, self._moves_remaining = self._wait_for_game()
                return self._obs(), {}
            except (ConnectionError, OSError) as e:
                print(f"[{datetime.now().strftime('%H:%M:%S')}][env] Connection error: {e}. Retrying in 3s...")
                self._connected = False
                self._client.disconnect()
                time.sleep(3)

    def step(self, action: int):
        x, y = action % self.w, action // self.w
        try:
            return self._step(x, y)
        except (ConnectionError, OSError) as e:
            print(f"[{datetime.now().strftime('%H:%M:%S')}][env] Connection lost during step: {e}. Reconnecting on next reset.")
            self._connected = False
            self._client.disconnect()
            return self._obs(), 0.0, False, True, {}

    def _step(self, x: int, y: int):
        board_before_move = self._board
        is_last_submove = self._moves_remaining == 1
        move_type = None
        self._client.send_move(x, y, self._game_id)

        while True:
            msg = self._client.read_message()

            # field2: our move confirmed — capture type for last-submove reward reconstruction
            if msg.startswith("field2:"):
                if is_last_submove:
                    m2 = _RE_FIELD2.match(msg)
                    if m2:
                        move_type = m2.group(1)
                continue

            # wrongstep: mask mismatch between game_logic.py and server — log for cross-validation
            if _RE_WRONGSTEP.match(msg):
                print(f"[{datetime.now().strftime('%H:%M:%S')}][env] WARNING: wrongstep at board={self._board[:20]}... "
                      f"action=({x},{y}) — mask logic diverges from server")
                continue

            # wrongcmd: sent when step: arrives with no active session (timing issue)
            if msg == "wrongcmd:":
                print(f"[{datetime.now().strftime('%H:%M:%S')}][env] WARNING: wrongcmd — step sent outside of active session")
                continue

            # field1: opponent made a move — board comes in the next go:
            if _RE_FIELD1.match(msg):
                continue

            # go: our turn (next sub-move or after opponent's full turn)
            m = _RE_GO.match(msg)
            if m:
                if is_last_submove:
                    shaped = self._shaped_reward(board_before_move, self._apply_move(board_before_move, x, y, move_type))
                else:
                    shaped = self._shaped_reward(board_before_move, m.group(1))
                self._board = m.group(1)
                self._moves_remaining = int(m.group(2))
                return self._obs(), shaped, False, False, {}

            if msg.startswith("win:"):
                return self._obs(), 1.0, True, False, {"game_won": True}

            if msg.startswith("lose:"):
                return self._obs(), -1.0, True, False, {"game_won": False}

            if msg.startswith("abort:"):
                return self._obs(), 0.0, False, True, {}

    def action_masks(self) -> np.ndarray:
        return np.array(valid_action_mask(self._board, self.w, self.h), dtype=bool)

    def _apply_move(self, board: str, x: int, y: int, move_type: str | None) -> str:
        """Reconstruct board after our move using field2: type, without opponent response."""
        if move_type is None:
            return board
        cells = list(board)
        cells[y * self.w + x] = '1' if move_type == 'standard' else '3'
        return ''.join(cells)

    def _shaped_reward(self, board_before: str, board_after: str) -> float:
        # '1' (X) — regular cells: can be lost if opponent captures them
        # '3' (CatchX) — fortress cells: permanent, opponent can never recapture
        x_delta     = board_after.count('1') - board_before.count('1')
        catch_delta = board_after.count('3') - board_before.count('3')
        return x_delta * 0.01 + catch_delta * 0.03

    def close(self):
        self._client.disconnect()
        self._connected = False

    # ── Internal helpers ──────────────────────────────────────────────────────

    def _obs(self) -> np.ndarray:
        board_vals = [int(c) for c in self._board]
        return np.array(board_vals + [self._moves_remaining], dtype=np.int8)

    def _update_spaces(self):
        n = self.w * self.h
        self.observation_space = spaces.Box(
            low=np.array([0] * n + [1], dtype=np.int8),
            high=np.array([4] * n + [3], dtype=np.int8),
            dtype=np.int8,
        )
        self.action_space = spaces.Discrete(n)

    def _wait_for_game(self) -> tuple[str, int]:
        """Block until a new game starts; return (board, moves_remaining)."""
        while True:
            msg = self._client.read_message()

            m = _RE_FOUND.match(msg)
            if m:
                self._game_id = int(m.group(1))
                w, h = int(m.group(3)), int(m.group(4))
                if w != self.w or h != self.h:
                    self.w, self.h = w, h
                    self._update_spaces()
                print(f"[{datetime.now().strftime('%H:%M:%S')}][env] Game started vs {m.group(2)}")
                continue

            # First go: after found: — game is on
            m = _RE_GO.match(msg)
            if m:
                return m.group(1), int(m.group(2))
