# Client-Server Board Games

A multiplayer board game platform with a dedicated server and desktop client.

---

## Virus War

A two-player territory game on a grid.

- Each turn a player makes **3 moves**, placing pieces on the board
- The first move goes in a corner; after that a move must be **connected to your territory** (including diagonally)
- Placing on an opponent's piece **captures** it: the cell becomes yours permanently and extends your territory
- The player who leaves their opponent unable to make 3 moves wins

Exact rules: [viruswar/PROTOCOL.md](viruswar/PROTOCOL.md#rules-as-enforced-by-the-server).

---

## Protocol

Clients and bots talk to the server over a line-based text protocol on TCP port 1234. [docs/PROTOCOL.md](docs/PROTOCOL.md) describes the messages, session lifecycle, matchmaking and accounts common to every game. Each game documents its board encoding, rules and a full example session separately: [Virus War](viruswar/PROTOCOL.md).

---

## Getting started

You need CMake 3.16+, a C++17 compiler and Qt 6 (Core, Network, Gui, Widgets). The Python bots also need Python 3 and pybind11.

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

**Play a game.** Start the server from its folder: it keeps `auth.txt` (accounts) and `stats.txt` (results) in its working directory. Then open two clients and connect both, each with its own login and password. A new login is registered on first connect. The server pairs the two players automatically. The Connect dialog also sets the board size. A game allows 60 s per turn; tick **Custom time control** in the dialog to pick another limit, such as 5 minutes per game.

```bash
cd build/server && ./GameServer
```

**Play against a bot.** Bots accept any time control; the random bot also plays any board size, the AZ bot only 10×10. Start the random bot and tell it to look for you: `./VirusWarRandomBot --login randombot --password <any> --preferred <your login>`. For a strong opponent, run the AZ bot's `play.py`: see [viruswar/az-bot/README.md](viruswar/az-bot/README.md).

**Python bots.** Both bots use one venv and the built `viruswar_sim` module:

```bash
cd viruswar
python3 -m venv .venv
.venv/bin/pip install -r requirements.txt
ln -s ../../build/server/viruswar_sim.*.so az-bot/    # the same for rl-bot/
```

`viruswar_sim` needs pybind11, which the venv installs. Once `viruswar/.venv` exists, CMake builds the module for the venv's Python and takes pybind11 from it, so configure and build again (`cmake -B build && cmake --build build`). Without pybind11 it prints a warning and skips the module.

### Tests

```bash
(cd build && ctest --output-on-failure)            # C++: game rules, time controls, matchmaking, client protocol and clock
cd viruswar && .venv/bin/python -m pytest tests   # Python: log format, server connection, AZ bot internals
```

The Python tests need the built `viruswar_sim` (see above); none of them start a server or a bot.

---

## Logging

Every program logs in one format, so their logs read and grep the same way:

```
2026-10-18 14:03:22.417 INFO  server  game_start id=7 first=alice second=bob board=10x10 time=turn60
2026-10-18 14:04:10.902 INFO  server  game_over id=7 winner=bob loser=alice cause=timeout
2026-10-18 14:04:11.120 WARN  azbot   slow_move login=azbot move=12 secs=11.2 sims=400
```

Date and time, level, program, then an event name and `key=value` details. Levels: `DEBUG` (every protocol line, passwords masked), `INFO` (what happened), `WARN` (unexpected but handled), `ERROR`.

| Program | Name in the log | Level | Log file |
|---|---|---|---|
| `GameServer` | `server` | `--log-level` (default `info`) | `--log-file` (none by default) |
| `VirusWarRandomBot` | `randbot` | `--log-level` | `--log-file` |
| `VirusWarClient` | `client` | `debug`, shown in the window's log panel | none |
| AZ bot | `azbot`, `aztrain`, `ladder`, `azeval` | `VIRUSWAR_LOG_LEVEL` (default `info`) | `models/play_az.log`, `train_az.log`, `ladder_az.log`, `eval_az.log` |
| RL bot | `rlbot`, `rltrain`, `rlenv` | `VIRUSWAR_LOG_LEVEL` | `models/play_rl.log`, `train_rl.log` |

Reports meant for reading rather than grepping, such as the ladder's result table (`models/ladder_az.md`), stay in Markdown. The C++ side is in `shared/logging.h`, the Python side in `viruswar/common/log.py`.

---

## Goals

- **Server** - handles multiple simultaneous games and players. Designed so that adding a new game requires minimal changes to the server itself.
- **Client** - desktop GUI to connect, authenticate, and play
- **Random bot** - plays random valid moves; used as a training opponent for the RL bot
- **RL bot** - reinforcement learning agent trained via self-play using MaskablePPO. Frozen: it plateaued far below the AZ bot, see [viruswar/rl-bot/README.md](viruswar/rl-bot/README.md)
- **AZ bot** - AlphaZero-style agent: Monte Carlo tree search guided by a neural network, trained by pure self-play. The strongest player, see [viruswar/az-bot/README.md](viruswar/az-bot/README.md)

---

## TODO

### Server
- [x] TCP server with client connection management
- [x] Login/password authentication
- [x] Ping/pong keep-alive
- [x] Virus War game logic
- [x] Abstract game interface - new games plug in without touching networking
- [x] Session-based game management - multiple simultaneous games per connection
- [x] Automatic matchmaking
- [x] Enforce max concurrent sessions per account
- [x] Preferred opponent - request a specific player by login
- [x] Configurable game type and board size per session
- [x] Randomize player side at game start to prevent first-move bias
- [x] Win/loss statistics
- [x] Bot accounts - dedicated bot role with multi-game support
- [x] Protocol versioning
- [x] Time controls like in chess: per turn, per game, with increment; running out of time loses
- [x] Resignation, and every result says why the game ended

### Client
- [x] Connect dialog with persistent settings
- [x] Board rendering
- [x] Move input
- [x] Highlight last moves of both players
- [x] In-app log panel
- [ ] Hand-drawn art style for board and pieces
- [x] Board size and time control chosen per session
- [x] Clocks for both players
- [x] Leaving a game asks for confirmation; the result dialog says why the game ended

### Random bot
- [x] Play random valid moves against any opponent
- [x] Multi-game support - handles unlimited simultaneous sessions
- [x] Graceful shutdown - finishes active games before exiting

### RL bot (frozen)
- [x] Gymnasium environment with action masking
- [x] Self-play training with MaskablePPO
- [x] Automatic promotion when win rate reaches threshold
- [x] Inference bot with multi-game support and graceful shutdown
- [x] Measured against the random bot and the AZ bot; frozen

### AZ bot
- [x] MCTS with a policy/value network, one tree node per sub-move
- [x] Offline self-play training on parallel worker processes, no server needed
- [x] Board-symmetry augmentation and anti-memorisation safeguards
- [x] Automatic promotion after a pit against the newest promoted model
- [x] Resumable training: checkpoint, replay buffer and optimizer state
- [x] Ladder tournament to measure progress across promotions
- [x] League inference bot with hot reload of promoted models
