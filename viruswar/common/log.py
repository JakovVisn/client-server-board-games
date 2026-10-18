"""
One log format for the Python bots, the same as the C++ programs (shared/logging.h):

    2026-10-18 14:03:22.417 INFO  azbot   game_start id=7 opponent=alice board=10x10
    <date> <time.ms>        <LEVEL> <component> <event> <key=value ...>

Levels: DEBUG (protocol traffic), INFO (what happened), WARN (unexpected but handled),
ERROR (an operation failed). Events are snake_case, details are key=value pairs.

Each script calls setup() once; modules log through logging.getLogger(__name__).
Level: VIRUSWAR_LOG_LEVEL (default info). Markdown reports go through append_report().
"""

import logging
import os
import re
import sys
from datetime import datetime

_LEVEL_NAMES = {"DEBUG": "DEBUG", "INFO": "INFO ", "WARNING": "WARN ",
                "ERROR": "ERROR", "CRITICAL": "FATAL"}


class _Formatter(logging.Formatter):
    def __init__(self, component: str):
        super().__init__()
        self.component = component

    def format(self, record: logging.LogRecord) -> str:
        ts = datetime.fromtimestamp(record.created).strftime("%Y-%m-%d %H:%M:%S.%f")[:-3]
        level = _LEVEL_NAMES.get(record.levelname, record.levelname)
        line = f"{ts} {level} {self.component:<7} {record.getMessage()}"
        if record.exc_info:
            line += "\n" + self.formatException(record.exc_info)
        return line


def setup(component: str, file: str | None = None, level: str | None = None) -> logging.Logger:
    """
    Send every log line to stderr and, if given, append it to `file`.

    Safe to call again (e.g. in a worker process): earlier handlers are replaced.
    """
    name = (level or os.getenv("VIRUSWAR_LOG_LEVEL", "info")).upper()
    name = {"WARN": "WARNING"}.get(name, name)
    if name not in ("DEBUG", "INFO", "WARNING", "ERROR"):
        raise ValueError(f"invalid log level {name!r} (debug, info, warn, error)")

    formatter = _Formatter(component)
    handlers: list[logging.Handler] = [logging.StreamHandler(sys.stderr)]
    if file:
        os.makedirs(os.path.dirname(os.path.abspath(file)), exist_ok=True)
        handlers.append(logging.FileHandler(file, encoding="utf-8"))

    root = logging.getLogger()
    for h in list(root.handlers):
        root.removeHandler(h)
        h.close()
    for h in handlers:
        h.setFormatter(formatter)
        root.addHandler(h)
    root.setLevel(name)
    return logging.getLogger(component)


def quoted(value) -> str:
    """The value itself, or "value" in quotes when it is empty or contains spaces."""
    text = str(value)
    if text and " " not in text:
        return text
    return '"' + text.replace('"', '\\"') + '"'


_RE_PASSWORD = re.compile(r"^(\s*mbclient:[^:]*:[^:]*:)[^:]*")


def mask_password(line: str) -> str:
    """A protocol line safe to log: the password in mbclient: is replaced by ***."""
    return _RE_PASSWORD.sub(r"\1***", line)


def append_report(path: str, title: str, body: str = "") -> None:
    """Append a Markdown section (### title, timestamp, body) to a human-readable report."""
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    new_file = not os.path.exists(path)
    with open(path, "a", encoding="utf-8") as f:
        if new_file:
            f.write("# VirusWar Report\n")
        f.write(f"\n### {title}\n\n`{datetime.now():%Y-%m-%d %H:%M}`\n")
        if body:
            f.write(f"\n{body.rstrip()}\n")
