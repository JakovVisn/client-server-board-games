# Client-Server Board Games

[![CI](https://github.com/JakovVisn/client-server-board-games/actions/workflows/ci.yml/badge.svg)](https://github.com/JakovVisn/client-server-board-games/actions/workflows/ci.yml)

A multiplayer board game platform: a game server, a desktop client and bots.

## Components

- **Server** - runs many games and players at once. A new game plugs in with minimal changes to the server.
- **Client** - desktop GUI to connect, log in and play.
- **Random bot** - plays random valid moves; a training opponent for the RL bot.
- **RL bot** - reinforcement learning agent (MaskablePPO, self-play). Frozen: it plateaued far below the AZ bot, see [viruswar/rl-bot/README.md](viruswar/rl-bot/README.md).
- **AZ bot** - AlphaZero-style agent: tree search guided by a neural network, trained by pure self-play. The strongest player, see [viruswar/az-bot/README.md](viruswar/az-bot/README.md).

## Virus War

A two-player territory game on a grid.

- Each turn a player makes **3 moves**, placing pieces on the board.
- The first move goes in a corner. After that, a move must be **connected to your territory** (diagonals count).
- Placing on an opponent's piece **captures** it: the cell is yours for good and extends your territory.
- You win when your opponent cannot make 3 moves.

Exact rules: [viruswar/PROTOCOL.md](viruswar/PROTOCOL.md#rules-as-enforced-by-the-server).

## Protocol

Clients and bots talk to the server in a line-based text protocol over TCP, port 1234.

- [docs/PROTOCOL.md](docs/PROTOCOL.md): messages, session lifecycle, matchmaking and accounts, common to every game.
- [viruswar/PROTOCOL.md](viruswar/PROTOCOL.md): Virus War board encoding, rules and a full example session.

## Getting started

You need CMake 3.16+, a C++17 compiler and Qt 6 (Core, Network, Gui, Widgets). The Python bots also need Python 3.

```bash
cmake -B build
cmake --build build
```

This builds:

| Program | Path | What it is |
|---|---|---|
| `GameServer` | `build/server/` | the server, TCP port 1234 |
| `VirusWarClient` | `build/viruswar/client/` | desktop client for playing |
| `VirusWarRandomBot` | `build/viruswar/random-bot/` | bot that plays random moves |
| `viruswar_sim.*.so` | `build/server/` | game logic for the Python bots; built only if CMake finds pybind11 |

### Play a game

1. Start the server from its folder. It keeps `auth.txt` (accounts) and `stats.txt` (results) in its working directory.
   ```bash
   cd build/server && ./GameServer
   ```
2. Open two clients and connect each with its own login and password. A new login is registered on first connect.
3. The server pairs the two players automatically.

The Connect dialog also sets the board size. A game allows 60 s per turn; tick **Custom time control** to pick another limit, such as 5 minutes per game.

### Play against a bot

Bots accept any time control. The random bot plays any board size, the AZ bot only 10×10.

- Random bot, told to look for you: `./VirusWarRandomBot --login randombot --password <any> --preferred <your login>`
- Strong opponent: run the AZ bot's `play.py`, see [viruswar/az-bot/README.md](viruswar/az-bot/README.md).

### Python bots

Both bots share one venv and use the built `viruswar_sim` module:

```bash
cd viruswar
python3 -m venv .venv
.venv/bin/pip install -r requirements.txt
ln -s ../../build/server/viruswar_sim.*.so az-bot/    # the same for rl-bot/
```

The venv installs pybind11. Once `viruswar/.venv` exists, CMake builds `viruswar_sim` for the venv's Python with the venv's pybind11, so configure and build again (`cmake -B build && cmake --build build`). Without pybind11, CMake prints a warning and skips the module.

### Tests

```bash
(cd build && ctest --output-on-failure)            # C++: game rules, time controls, matchmaking, client protocol and clock
cd viruswar && .venv/bin/python -m pytest tests   # Python: log format, server connection, AZ bot internals
```

The Python tests need the built `viruswar_sim`. None of the tests start a server or a bot. GitHub Actions runs both suites on every push (`.github/workflows/ci.yml`).

## Logging

All programs log in one format, so their logs read and grep the same way:

```
2026-10-18 14:03:22.417 INFO  server  game_start id=7 first=alice second=bob board=10x10 time=turn60
2026-10-18 14:04:10.902 INFO  server  game_over id=7 winner=bob loser=alice cause=timeout
2026-10-18 14:04:11.120 WARN  azbot   slow_move login=azbot move=12 secs=11.2 sims=400
```

Each line: date and time, level, program, event name, `key=value` details. Levels: `DEBUG` (every protocol line, passwords masked), `INFO` (what happened), `WARN` (unexpected but handled), `ERROR`.

| Program | Name in the log | Level | Log file |
|---|---|---|---|
| `GameServer` | `server` | `--log-level` (default `info`) | `--log-file` (none by default) |
| `VirusWarRandomBot` | `randbot` | `--log-level` | `--log-file` |
| `VirusWarClient` | `client` | `debug`, shown in the window's log panel | none |
| AZ bot | `azbot`, `aztrain`, `ladder`, `azeval` | `VIRUSWAR_LOG_LEVEL` (default `info`) | `models/play_az.log`, `train_az.log`, `ladder_az.log`, `eval_az.log` |
| RL bot | `rlbot`, `rltrain`, `rlenv` | `VIRUSWAR_LOG_LEVEL` | `models/play_rl.log`, `train_rl.log` |

Reports meant for reading, such as the ladder's result table (`models/ladder_az.md`), stay in Markdown. Code: `shared/logging.h` (C++), `viruswar/common/log.py` (Python).

## TODO

- Client: hand-drawn art style for the board and pieces
