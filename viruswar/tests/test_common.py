"""Shared modules: the log format and the server connection."""

import logging
import re
import socket

import log as vlog
from bot_client import ServerConnection

LINE = re.compile(r"^\d{4}-\d\d-\d\d \d\d:\d\d:\d\d\.\d{3} (DEBUG|INFO |WARN |ERROR) \S+\s+\S")


def test_log_format_matches_the_cpp_programs(tmp_path):
    path = tmp_path / "test.log"
    vlog.setup("tester", str(path), level="debug")
    logging.getLogger("x").info("game_start id=7")
    logging.getLogger("x").warning("slow_move secs=11.2")
    lines = path.read_text().splitlines()
    assert len(lines) == 2
    assert all(LINE.match(line) for line in lines)
    assert lines[0].endswith("INFO  tester  game_start id=7")
    assert " WARN  tester  slow_move" in lines[1]


def test_log_level_filters(tmp_path):
    path = tmp_path / "test.log"
    vlog.setup("tester", str(path), level="warn")
    logging.getLogger("x").info("hidden")
    logging.getLogger("x").error("shown")
    assert path.read_text().count("\n") == 1


def test_mask_password_and_quoting():
    assert vlog.mask_password("mbclient:2:alice:secret:viruswar:10:10:") == \
        "mbclient:2:alice:***:viruswar:10:10:"
    assert vlog.mask_password("step:1:2:3:") == "step:1:2:3:"
    assert vlog.quoted("ab") == "ab"
    assert vlog.quoted("a b") == '"a b"'
    assert vlog.quoted("") == '""'


def test_append_report(tmp_path):
    path = tmp_path / "report.md"
    vlog.append_report(str(path), "Ladder: x", "| a | b |")
    vlog.append_report(str(path), "Ladder: y")
    text = path.read_text()
    assert text.startswith("# VirusWar Report")
    assert "### Ladder: x" in text and "| a | b |" in text and "### Ladder: y" in text


def _connected_pair() -> tuple[ServerConnection, socket.socket]:
    ours, theirs = socket.socketpair()
    conn = ServerConnection("localhost", 0, "alice", "secret")
    conn._sock = ours
    return conn, theirs


def test_read_message_answers_ping_and_joins_split_utf8():
    conn, server = _connected_pair()
    data = "ping:\nfound:1:bob:viruswar:10:10:first:\nx:é:\n".encode()
    split = data.index("é".encode()) + 1          # inside the two-byte character
    server.sendall(data[:split])
    server.sendall(data[split:])
    assert conn.read_message() == "found:1:bob:viruswar:10:10:first:"
    assert conn.read_message() == "x:é:"
    assert server.recv(100) == b"pong:\n"


def test_disconnect_wakes_a_blocked_reader_cleanly():
    import threading
    conn, _server = _connected_pair()
    errors = []

    def reader():
        try:
            conn.read_message()
        except Exception as e:  # noqa: BLE001 — the type is what we check
            errors.append(e)

    t = threading.Thread(target=reader)
    t.start()
    conn.disconnect()
    t.join(timeout=2)
    assert not t.is_alive()
    assert errors and isinstance(errors[0], (ConnectionError, OSError))


def test_resign_and_stop_matching_lines():
    conn, server = _connected_pair()
    conn.resign(7)
    conn.stop_matching()
    assert server.recv(100) == b"resign:7:\nnomatch:\n"
