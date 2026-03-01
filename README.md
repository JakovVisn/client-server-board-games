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

---

## TODO

### Server
- [x] TCP server with client connection management
- [x] Login/password authentication
- [x] Ping/pong keep-alive
- [ ] Virus War game logic
- [ ] Abstract game interface - new games plug in without touching networking
- [ ] Session-based game management - multiple simultaneous games per connection
- [ ] Automatic matchmaking
- [ ] Enforce max concurrent sessions per account
- [ ] Preferred opponent - request a specific player by login
- [ ] Configurable game type and board size per session
- [ ] Randomize player side at game start to prevent first-move bias
- [ ] Win/loss statistics
- [ ] Bot accounts - dedicated bot role with multi-game support
- [ ] Protocol versioning
- [ ] Turn timer

### Client
- [ ] Connect dialog with persistent settings
- [ ] Board rendering
- [ ] Move input
- [ ] Highlight last moves of both players
- [ ] In-app log panel
- [ ] Hand-drawn art style for board and pieces
- [ ] Dynamic board size configurable per session
