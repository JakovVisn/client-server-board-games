#include "botlogic.h"
#include <QRandomGenerator>

namespace BotLogic {

// Mirrors the server's VirusWarField::isConnectedToX: 8-connected path from (x,y)
// through my captured cells ('3') to one of my pieces ('1').
static bool isConnectedToMine(const QString& board, int w, int h,
                               int x, int y, QVector<bool>& visited)
{
    visited[y * w + x] = true;

    static const int dx[] = {  0,  1,  0, -1, -1,  1, -1,  1 };
    static const int dy[] = { -1,  0,  1,  0, -1, -1,  1,  1 };

    for (int d = 0; d < 8; ++d) {
        const int nx = x + dx[d];
        const int ny = y + dy[d];
        if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
        if (visited[ny * w + nx]) continue;

        const char c = board[ny * w + nx].toLatin1();
        if (c == '1') return true;
        if (c == '3') {
            if (isConnectedToMine(board, w, h, nx, ny, visited))
                return true;
        } else {
            visited[ny * w + nx] = true;
        }
    }
    return false;
}

// Mirrors the server's VirusWarField::isValidMove. Cells, from the bot's view: 0 empty,
// 1 mine, 2 opponent, 3 captured by me, 4 captured by the opponent.
bool isValidMove(const QString& board, int w, int h, int x, int y)
{
    if (x < 0 || y < 0 || x >= w || y >= h) return false;

    const char c = board[y * w + x].toLatin1();
    if (c == '1' || c == '3' || c == '4') return false;

    const char topLeft     = board[0].toLatin1();
    const char bottomRight = board[(h - 1) * w + (w - 1)].toLatin1();
    if ((x == 0     && y == 0     && c != '2' && bottomRight != '1') ||
        (x == w - 1 && y == h - 1 && c != '2' && topLeft     != '1'))
        return true;

    QVector<bool> visited(w * h, false);
    return isConnectedToMine(board, w, h, x, y, visited);
}

QPoint randomMove(const QString& board, int w, int h)
{
    QList<QPoint> moves;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            if (isValidMove(board, w, h, x, y))
                moves.append(QPoint(x, y));

    if (moves.isEmpty()) return {-1, -1};
    return moves[QRandomGenerator::global()->bounded(moves.size())];
}

} // namespace BotLogic
