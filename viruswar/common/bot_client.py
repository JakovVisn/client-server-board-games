"""TCP connection to the VirusWar server using the bot protocol (GAMEID multiplexing)."""

import logging
import socket
from typing import Optional

from log import mask_password

log = logging.getLogger(__name__)

CONNECT_TIMEOUT_SECS = 10
_LOGIN_ERRORS = ("wronguser:", "wrongver:", "alreadyconnected:", "servererror:", "badgame:")


class ServerConnection:
    """Synchronous blocking TCP connection. Handles ping/pong transparently."""

    def __init__(self, host: str, port: int, login: str, password: str):
        self.host = host
        self.port = port
        self.login = login
        self.password = password
        self._sock: Optional[socket.socket] = None
        self._buf = b""

    def connect(self, preferred_opponent: str = ""):
        # Connect and log in with a timeout; afterwards reads block, since a bot waits
        # indefinitely for games
        sock = socket.create_connection((self.host, self.port), timeout=CONNECT_TIMEOUT_SECS)
        self._sock = sock
        self._buf = b""

        if preferred_opponent:
            msg = f"mbclient:2:{self.login}:{self.password}:viruswar:10:10:{preferred_opponent}:"
        else:
            msg = f"mbclient:2:{self.login}:{self.password}:"
        try:
            self._send(msg)
            while True:
                line = self._readline()
                if line.startswith("mbserver:"):
                    break
                if line in _LOGIN_ERRORS:
                    raise ConnectionError(f"Auth failed: {line}")
        except BaseException:
            self.disconnect()
            raise
        sock.settimeout(None)
        log.info(f"login_ok login={self.login} server={self.host}:{self.port}")

    def disconnect(self):
        sock, self._sock = self._sock, None
        if sock:
            try:
                sock.shutdown(socket.SHUT_RDWR)  # wakes a recv() blocked on another thread
            except OSError:
                pass  # already closed by the peer
            sock.close()

    def send_move(self, x: int, y: int, game_id: int):
        self._send(f"step:{x}:{y}:{game_id}:")

    def resign(self, game_id: int):
        """Give up a game; the server answers lose:GAMEID:resign:."""
        self._send(f"resign:{game_id}:")

    def stop_matching(self):
        """Ask for no new games (nomatch:); the ones in progress go on."""
        self._send("nomatch:")

    def read_message(self) -> str:
        """Read the next message, auto-responding to ping."""
        while True:
            line = self._readline()
            if line == "ping:":
                self._send("pong:")
                continue
            return line

    def _socket(self) -> socket.socket:
        # disconnect() may run on another thread (shutdown) while this one reads
        sock = self._sock
        if sock is None:
            raise ConnectionError("not connected")
        return sock

    def _send(self, msg: str):
        log.debug(f"send login={self.login} line={mask_password(msg)}")
        self._socket().sendall((msg + "\n").encode("utf-8"))

    def _readline(self) -> str:
        # Buffer bytes and decode whole lines: a chunk may end inside a UTF-8 character
        while b"\n" not in self._buf:
            chunk = self._socket().recv(4096)
            if not chunk:
                raise ConnectionError("Server closed connection")
            self._buf += chunk
        raw, self._buf = self._buf.split(b"\n", 1)
        line = raw.decode("utf-8", errors="replace").strip()
        log.debug(f"recv login={self.login} line={line}")
        return line
