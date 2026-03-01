"""
Valid move calculation mirroring Field::isValidMove + Field::isConnectedToX.
Used to build action masks so the model never selects illegal moves.

Board encoding (from current player's perspective):
  0 = Clear
  1 = Mine       (my active cells)
  2 = Opponent   (opponent's cells)
  3 = CatchX     (cells I've captured — traversable for connectivity)
  4 = CatchO     (my cells captured by opponent)
"""

_DIRS = [(0, -1), (1, 0), (0, 1), (-1, 0), (-1, -1), (1, -1), (-1, 1), (1, 1)]


def _is_connected_to_x(board: list[str], w: int, h: int,
                        x: int, y: int, visited: list[bool]) -> bool:
    """DFS through '3' (CatchX) cells; returns True if a '1' cell is reachable.

    Mirrors Field::isConnectedToX exactly: single shared visited array,
    DFS starts from (x, y) itself (not from its neighbors).
    """
    visited[y * w + x] = True
    for dx, dy in _DIRS:
        nx, ny = x + dx, y + dy
        if nx < 0 or nx >= w or ny < 0 or ny >= h:
            continue
        idx = ny * w + nx
        if visited[idx]:
            continue
        c = board[idx]
        if c == "1":
            return True
        if c == "3" and _is_connected_to_x(board, w, h, nx, ny, visited):
            return True
        visited[idx] = True
    return False


def is_valid_move(board: list[str], w: int, h: int, x: int, y: int) -> bool:
    if x < 0 or x >= w or y < 0 or y >= h:
        return False

    cell = board[y * w + x]
    if cell not in ("0", "2"):
        return False

    # Corner starting-position shortcut (mirrors server exactly)
    if x == 0 and y == 0 and cell != "2" and board[(h - 1) * w + (w - 1)] != "1":
        return True
    if x == w - 1 and y == h - 1 and cell != "2" and board[0] != "1":
        return True

    visited = [False] * (w * h)
    return _is_connected_to_x(board, w, h, x, y, visited)


def valid_action_mask(board: str, w: int, h: int) -> list[bool]:
    """Return a boolean list of length w*h: True where a move is legal."""
    cells = list(board)
    return [is_valid_move(cells, w, h, idx % w, idx // w) for idx in range(w * h)]
