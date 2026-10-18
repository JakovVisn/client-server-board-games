"""
Gymnasium environment for VirusWar (CNN version).

Observation shape: (6, H, W)

Channels:
0 — my regular cells (1)
1 — my fortress cells (3)
2 — enemy regular cells (2)
3 — enemy fortress cells (4)
4 — empty cells (0)
5 — moves_remaining (normalized to 0..1, broadcast)
"""

import logging
import re
import time

import gymnasium as gym
import numpy as np
from gymnasium import spaces

from bot_client import ServerConnection
from viruswar_sim import valid_action_mask

log = logging.getLogger(__name__)

# Bot protocol: found:GAMEID:OPPONENT:GAMENAME:W:H:role:
_RE_FOUND     = re.compile(r"found:(\d+):(\w+):\w+:(\d+):(\d+):(?:first|second):")
# Bot protocol: go:BOARD:MOVES:GAMEID:
_RE_GO        = re.compile(r"go:([0-4]+):(\d+):(\d+):")
# Bot protocol: field1:TYPE:X:Y:GAMEID:
_RE_FIELD1    = re.compile(r"field1:\w+:-?\d+:-?\d+:\d+:")
# Bot protocol: wrongstep:GAMEID:
_RE_WRONGSTEP = re.compile(r"wrongstep:(\d+):")


class VirusWarEnv(gym.Env):
    metadata = {}

    def __init__(self, host: str, port: int,
                 credentials: list[tuple[str, str]],
                 opponent: str = "",
                 board_w: int = 10, board_h: int = 10):
        super().__init__()
        self._host = host
        self._port = port
        self._credentials = credentials
        self._opponent = opponent
        self._connected = False
        self._client: ServerConnection | None = None
        self._login = "?"

        self.w = board_w
        self.h = board_h

        self._board = "0" * (self.w * self.h)
        self._moves_remaining = 3
        self._game_id = 0

        self._update_spaces()

    # =========================================================
    # Gym API
    # =========================================================

    def reset(self, *, seed=None, options=None):
        super().reset(seed=seed)
        while True:
            try:
                if not self._connected:
                    self._try_connect()
                self._board, self._moves_remaining = self._wait_for_game()
                return self._obs(), {}
            except (ConnectionError, OSError) as e:
                log.warning(f"connection_lost login={self._login} error={e!r} retry_secs=3")
                self._connected = False
                if self._client:
                    self._client.disconnect()
                    self._client = None
                time.sleep(3)

    def step(self, action: int):
        x, y = action % self.w, action // self.w
        try:
            return self._step(x, y)
        except (ConnectionError, OSError) as e:
            log.warning(f"connection_lost login={self._login} during=step error={e!r}")
            self._connected = False
            if self._client:
                self._client.disconnect()
                self._client = None
            return self._obs(), 0.0, False, True, {}

    def _step(self, x: int, y: int):
        t0 = time.perf_counter()
        self._client.send_move(x, y, self._game_id)
        gid = str(self._game_id)

        while True:
            msg = self._client.read_message()

            # Acknowledged server messages that carry no learning signal
            if (msg.startswith("field2:")
                    or _RE_FIELD1.match(msg)
                    or msg == "wrongcmd:"):
                continue

            # wrongstep: server rejected the move and immediately re-sends go: — just wait for it
            m = _RE_WRONGSTEP.match(msg)
            if m:
                if m.group(1) != gid:
                    continue
                log.warning(f"wrongstep login={self._login} move={x},{y} board={self._board}")
                continue  # server sends go: right after wrongstep; read it and return normally

            rt_ms = (time.perf_counter() - t0) * 1000

            # go: our turn (next sub-move or after opponent's full turn)
            m = _RE_GO.match(msg)
            if m:
                if m.group(3) != gid:
                    continue
                self._board = m.group(1)
                self._moves_remaining = int(m.group(2))
                return self._obs(), 0.0, False, False, {"server_rt_ms": rt_ms}

            if msg.startswith("win:"):
                if msg.split(":")[1] != gid:
                    continue
                board_list = list(self._board)
                board_list[y * self.w + x] = '3' if self._board[y * self.w + x] == '2' else '1'
                self._board = ''.join(board_list)
                captures = self._board.count('3')
                bonus = captures / (self.w * self.h) * 0.5
                return self._obs(), 1.0 + bonus, True, False, {"game_won": True, "server_rt_ms": rt_ms}

            if msg.startswith("lose:"):
                if msg.split(":")[1] != gid:
                    continue
                return self._obs(), -1.0, True, False, {"game_won": False, "server_rt_ms": rt_ms}

            if msg.startswith("abort:"):
                if msg.split(":")[1] != gid:
                    continue
                return self._obs(), 0.0, False, True, {"server_rt_ms": rt_ms}

    def action_masks(self) -> np.ndarray:
        return np.array(
            valid_action_mask(self._board, self.w, self.h),
            dtype=bool
        )

    def close(self):
        if self._client:
            self._client.disconnect()
            self._client = None
        self._connected = False

    # =========================================================
    # Connection
    # =========================================================

    def _try_connect(self):
        """Try credentials in order, skip accounts that are alreadyconnected."""
        for login, password in self._credentials:
            client = ServerConnection(self._host, self._port, login, password)
            try:
                client.connect(preferred_opponent=self._opponent)
                self._client = client
                self._connected = True
                self._login = login
                log.info(f"connected login={login}")
                return
            except ConnectionError as e:
                if "alreadyconnected" in str(e):
                    client.disconnect()
                    continue
                raise
        raise ConnectionError("No training account available (all alreadyconnected)")

    # =========================================================
    # Observation (CNN format)
    # =========================================================

    def _obs(self) -> np.ndarray:
        board = np.array([int(c) for c in self._board],
                         dtype=np.int8).reshape(self.h, self.w)

        ch_my         = (board == 1).astype(np.float32)
        ch_my_fort    = (board == 3).astype(np.float32)
        ch_enemy      = (board == 2).astype(np.float32)
        ch_enemy_fort = (board == 4).astype(np.float32)
        ch_empty      = (board == 0).astype(np.float32)

        moves_channel = np.full(
            (self.h, self.w),
            self._moves_remaining / 3.0,
            dtype=np.float32
        )

        obs = np.stack([
            ch_my,
            ch_my_fort,
            ch_enemy,
            ch_enemy_fort,
            ch_empty,
            moves_channel
        ], axis=0)  # (6, H, W)

        return obs

    # =========================================================

    def _update_spaces(self):
        self.observation_space = spaces.Box(
            low=0.0,
            high=1.0,
            shape=(6, self.h, self.w),
            dtype=np.float32,
        )
        self.action_space = spaces.Discrete(self.w * self.h)

    # =========================================================

    def _wait_for_game(self) -> tuple[str, int]:
        while True:
            msg = self._client.read_message()

            m = _RE_FOUND.match(msg)
            if m:
                self._game_id = int(m.group(1))
                w, h = int(m.group(3)), int(m.group(4))
                if w != self.w or h != self.h:
                    self.w, self.h = w, h
                    self._update_spaces()
                log.info(f"game_start login={self._login} id={self._game_id} opponent={m.group(2)}")
                continue

            # First go: after found: — game is on
            m = _RE_GO.match(msg)
            if m:
                return m.group(1), int(m.group(2))
