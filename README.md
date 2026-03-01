# Client-Server Board Games

A multiplayer board game platform with a dedicated server and desktop client.

---

## Virus War

A two-player territory game on a grid.

- Each turn a player makes **3 moves**, placing pieces on the board
- A cell is valid if it is empty and **connected to your existing territory**
- Placing next to an opponent's cell **captures** it
- The player who leaves their opponent with **no valid moves** wins

---

## Goals

- **Server** - handles multiple simultaneous games and players. Designed so that adding a new game requires minimal changes to the server itself.
- **Client** - desktop GUI to connect, authenticate, and play
- **Random bot** - plays random valid moves; used as a training opponent for the RL bot
- **RL bot** - reinforcement learning agent trained via self-play using MaskablePPO

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

### RL bot
- [ ] Gymnasium environment with action masking
- [ ] Self-play training with MaskablePPO
- [ ] Automatic promotion when win rate reaches threshold
- [ ] Inference bot with multi-game support and graceful shutdown
- [ ] Experiment with larger networks / hyperparameter tuning
