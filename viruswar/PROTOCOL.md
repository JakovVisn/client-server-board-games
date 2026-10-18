# Virus War Protocol

How Virus War fills the game-specific parts of the [server protocol](../docs/PROTOCOL.md), which covers messages, login, errors, matchmaking and keep-alive.

Server code: `server/virus_war_game.cpp`, `server/viruswar_field.cpp`.

## Parameters

| Field | Value |
|---|---|
| `GAME` | `viruswar` (the server's default game) |
| `W`, `H` | 5–20 each; default 10×10 (`0:0` in `mbclient:`) |
| `MOVES` in `go:` | 3 at the start of a turn, then 2, then 1 |
| `TYPE` in `field1:` / `field2:` | `standard` (placed on an empty cell) or `caught` (captured an opponent's piece) |

## Board encoding

`BOARD` in `go:` is `W × H` digits, row by row: index `y * W + x`. It is always **from the receiving player's point of view**:

| Digit | Cell |
|---|---|
| `0` | empty |
| `1` | my piece |
| `2` | opponent's piece |
| `3` | opponent's piece I captured (mine now, permanent) |
| `4` | my piece the opponent captured (theirs now, permanent) |

So both players can use the same code: my cells are always `1` and `3`.

## Rules, as enforced by the server

- **Neighbours** are the 8 surrounding cells.
- **A move is legal** on an empty cell (`0`) or an opponent's piece (`2`) connected to one of my pieces (`1`): adjacent to it, or linked to it through a chain of my captured cells (`3`).
- **Placing on an opponent's piece (`2`) captures it.** The move reports `caught`; the cell becomes `3` for me and `4` for the opponent, for good.
- **Start:** the corners `(0, 0)` and `(W−1, H−1)` are also legal when empty, unless I already have a piece (`1`) in the opposite corner. The first player starts in one corner, the second takes the other.
- **A turn is 3 moves.** After a player completes a turn, if the opponent cannot make 3 legal moves in their next turn, the player who just moved wins (`win:…:rules:` / `lose:…:rules:`). While either start corner is empty, the game goes on.

## Example session

A complete game on a 5×5 board. Both players ask for the same game and size, so they are matched, and for no time limit (`none`, after an empty `TARGET`), so `go:` has no clock fields (timed games: [Time control](../docs/PROTOCOL.md#time-control)). `C:` lines are sent by a client, `S:` lines by the server; real traffic has no prefixes or comments.

**Login**
```
alice C: mbclient:2:alice:secret:viruswar:5:5::none:
alice S: mbserver:2:viruswar:5:5:
bob   C: mbclient:2:bob:hunter2:viruswar:5:5::none:
bob   S: mbserver:2:viruswar:5:5:
```

**The game starts (id 0); alice moves first**
```
alice S: found:0:bob:viruswar:5:5:first:none:
bob   S: found:0:alice:viruswar:5:5:second:none:
alice S: go:0000000000000000000000000:3:0:
```

**alice's turn: three moves, starting in the top-left corner**
```
alice C: step:0:0:0:
alice S: field2:standard:0:0:0:
bob   S: field1:standard:0:0:0:
alice S: go:1000000000000000000000000:2:0:
alice C: step:1:1:0:
alice S: field2:standard:1:1:0:
bob   S: field1:standard:1:1:0:
alice S: go:1000001000000000000000000:1:0:
alice C: step:2:2:0:
alice S: field2:standard:2:2:0:
bob   S: field1:standard:2:2:0:
```

**bob's turn: alice's pieces are `2` for him. He takes the other corner, then captures (2,2)**
```
bob   S: go:2000002000002000000000000:3:0:
bob   C: step:4:4:0:
bob   S: field2:standard:4:4:0:
alice S: field1:standard:4:4:0:
bob   S: go:2000002000002000000000001:2:0:
bob   C: step:3:3:0:
bob   S: field2:standard:3:3:0:
alice S: field1:standard:3:3:0:
bob   S: go:2000002000002000001000001:1:0:
bob   C: step:2:2:0:
bob   S: field2:caught:2:2:0:
alice S: field1:caught:2:2:0:
```

**alice's turn: (2,2) is now `4` (captured by bob). Her first try is illegal**
```
alice S: go:1000001000004000002000002:3:0:
alice C: step:4:0:0:
alice S: wrongstep:0:
alice S: go:1000001000004000002000002:3:0:
```
(4,0) has no neighbour of hers.

**… play continues until one side is cut off …**

The final move is reported like any other, then the result follows:
```
bob   C: step:1:3:0:
bob   S: field2:standard:1:3:0:
alice S: field1:standard:1:3:0:
bob   S: win:0:rules:
alice S: lose:0:rules:
```

### Reading a board

alice's last `go:` board above, `1000001000004000002000002`, cut into rows of `W` = 5 digits (`10000 01000 00400 00020 00002`) and stacked top to bottom:

| | x=0 | x=1 | x=2 | x=3 | x=4 |
|---|---|---|---|---|---|
| **y=0** | 1 | 0 | 0 | 0 | 0 |
| **y=1** | 0 | 1 | 0 | 0 | 0 |
| **y=2** | 0 | 0 | 4 | 0 | 0 |
| **y=3** | 0 | 0 | 0 | 2 | 0 |
| **y=4** | 0 | 0 | 0 | 0 | 2 |
