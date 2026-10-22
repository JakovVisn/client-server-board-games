# Client-Server Board Games

[![CI](https://github.com/JakovVisn/client-server-board-games/actions/workflows/ci.yml/badge.svg)](https://github.com/JakovVisn/client-server-board-games/actions/workflows/ci.yml)

A multiplayer board game platform: a game server, a desktop client and bots.

## Highlights

- **AlphaZero-style bot** trained only by self-play (~22k games): it wins every game against the random bot and the RL bot, and beat a human 16 games out of 17. A ladder tournament measures its progress across promotions ([results](viruswar/az-bot/README.md)).
- **An honest dead end:** the earlier PPO bot plateaued, so it was measured, frozen and documented ([why](viruswar/rl-bot/README.md)).
- **One game core** (Qt-free C++) shared by the server, the random bot and, through pybind11, the Python bots.
- **Server:** matchmaking as a pure, unit-tested function, chess-style clocks, resignation and results with a cause. C++ and Python tests run in CI.

## Components

- **Server** - runs many games and players at once over a text protocol on TCP ([docs/PROTOCOL.md](docs/PROTOCOL.md)). A new game plugs in with minimal changes to the server.
- **Client** - desktop GUI to connect, log in and play.
- **Random bot** - plays random valid moves; a training opponent for the RL bot.
- **RL bot** - reinforcement learning agent (MaskablePPO, league self-play over the server), frozen ([details](viruswar/rl-bot/README.md)).
- **AZ bot** - AlphaZero-style agent: Monte Carlo tree search guided by a policy/value network ([details](viruswar/az-bot/README.md)).

## Virus War

A two-player territory game on a grid.

- Each turn a player makes **3 moves**, placing pieces on the board.
- The first move goes in a corner. After that, a move must be **connected to your territory** (diagonals count).
- Placing on an opponent's piece **captures** it: the cell is yours for good and extends your territory.
- You win when your opponent cannot make 3 moves.

Exact rules, board encoding and a full example game: [viruswar/PROTOCOL.md](viruswar/PROTOCOL.md#rules-as-enforced-by-the-server).

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
(cd build && ctest --output-on-failure)            # C++: game rules, time controls, matchmaking, accounts, limits, a real server over sockets, client
cd viruswar && .venv/bin/python -m pytest tests   # Python: log format, server connection, AZ bot internals
```

The Python tests need the built `viruswar_sim`. The C++ network tests start their own server on a free local port; no test needs a running server or bot. GitHub Actions runs both suites on every push (`.github/workflows/ci.yml`).
