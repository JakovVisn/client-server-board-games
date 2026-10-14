"""TCP connection to the VirusWar server using the bot protocol (GAMEID multiplexing)."""

import socket
from typing import Optional


class ServerConnection:
    """Synchronous blocking TCP connection. Handles ping/pong transparently."""

    def __init__(self, host: str, port: int, login: str, password: str):
        self.host = host
        self.port = port
        self.login = login
        self.password = password
        self._sock: Optional[socket.socket] = None
        self._buf = ""

    def connect(self, preferred_opponent: str = ""):
        self._sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self._sock.connect((self.host, self.port))
        self._buf = ""

        if preferred_opponent:
            msg = f"mbclient:2:{self.login}:{self.password}:viruswar:10:10:{preferred_opponent}:"
        else:
            msg = f"mbclient:2:{self.login}:{self.password}:"
        self._send(msg)

        while True:
            line = self._readline()
            if line.startswith("mbserver:"):
                print(f"[client] Authenticated as {self.login}")
                return
            if line in ("wronguser:", "wrongver:", "alreadyconnected:", "servererror:", "badgame:"):
                raise ConnectionError(f"Auth failed: {line}")

    def disconnect(self):
        if self._sock:
            self._sock.close()
            self._sock = None

    def send_move(self, x: int, y: int, game_id: int):
        self._send(f"step:{x}:{y}:{game_id}:")

    def read_message(self) -> str:
        """Read the next message, auto-responding to ping."""
        while True:
            line = self._readline()
            if line == "ping:":
                self._send("pong:")
                continue
            return line

    def _send(self, msg: str):
        self._sock.sendall((msg + "\n").encode("utf-8"))

    def _readline(self) -> str:
        while "\n" not in self._buf:
            chunk = self._sock.recv(4096).decode("utf-8")
            if not chunk:
                raise ConnectionError("Server closed connection")
            self._buf += chunk
        line, self._buf = self._buf.split("\n", 1)
        return line.strip()
