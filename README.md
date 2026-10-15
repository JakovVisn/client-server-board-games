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

## Goals

- **Server** - handles multiple simultaneous games and players. Designed so that adding a new game requires minimal changes to the server itself.
- **Client** - desktop GUI to connect, authenticate, and play
- **Random bot** - plays random valid moves; used as a training opponent for the RL bot
- **RL bot** - reinforcement learning agent trained via self-play using MaskablePPO. Frozen: it plateaued far below the AZ bot, see [viruswar/rl-bot/README.md](viruswar/rl-bot/README.md)
- **AZ bot** - AlphaZero-style agent: Monte Carlo tree search guided by a neural network, trained by pure self-play

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
- [ ] Turn timer

### Client
- [x] Connect dialog with persistent settings
- [x] Board rendering
- [x] Move input
- [x] Highlight last moves of both players
- [x] In-app log panel
- [ ] Hand-drawn art style for board and pieces
- [ ] Dynamic board size configurable per session

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
