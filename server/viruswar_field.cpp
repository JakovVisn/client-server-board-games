#include "viruswar_field.h"

VirusWarField::VirusWarField(int width, int height)
    : width_(width), height_(height), moveIndex_(0), cells_(width * height, Cell::Clear)
{
}

int VirusWarField::width() const { return width_; }
int VirusWarField::height() const { return height_; }
int VirusWarField::moveIndex() const { return moveIndex_; }
void VirusWarField::setMoveIndex(int index) { moveIndex_ = index; }

int VirusWarField::cellIndex(int x, int y) const
{
    return y * width_ + x;
}

bool VirusWarField::isInBounds(int x, int y) const
{
    return x >= 0 && y >= 0 && x < width_ && y < height_;
}

VirusWarField::Cell VirusWarField::cell(int x, int y) const
{
    if (!isInBounds(x, y))
        return Cell::Clear;
    return cells_[cellIndex(x, y)];
}

void VirusWarField::setCell(int x, int y, Cell value)
{
    if (!isInBounds(x, y))
        return;
    cells_[cellIndex(x, y)] = value;
}

std::string VirusWarField::serialize() const
{
    std::string result;
    result.reserve(cells_.size());
    for (Cell c : cells_) {
        switch (c) {
        case Cell::Clear:  result += '0'; break;
        case Cell::X:      result += '1'; break;
        case Cell::O:      result += '2'; break;
        case Cell::CatchX: result += '3'; break;
        case Cell::CatchO: result += '4'; break;
        }
    }
    return result;
}

VirusWarField VirusWarField::fromBoard(const std::string& board, int width, int height)
{
    VirusWarField field(width, height);
    for (int i = 0; i < width * height && i < static_cast<int>(board.size()); ++i) {
        Cell c = Cell::Clear;
        switch (board[i]) {
        case '1': c = Cell::X;      break;
        case '2': c = Cell::O;      break;
        case '3': c = Cell::CatchX; break;
        case '4': c = Cell::CatchO; break;
        default: break;
        }
        field.setCell(i % width, i / width, c);
    }
    return field;
}

bool VirusWarField::isConnectedToX(int x, int y, std::vector<bool>& visited) const
{
    visited[cellIndex(x, y)] = true;

    static const int dx[] = { 0,  1,  0, -1, -1,  1, -1,  1 };
    static const int dy[] = { -1,  0,  1,  0, -1, -1,  1,  1 };

    for (int d = 0; d < 8; d++) {
        const int nx = x + dx[d];
        const int ny = y + dy[d];
        if (!isInBounds(nx, ny) || visited[cellIndex(nx, ny)])
            continue;
        const Cell c = cell(nx, ny);
        if (c == Cell::X)
            return true;
        if (c == Cell::CatchX && isConnectedToX(nx, ny, visited))
            return true;
        visited[cellIndex(nx, ny)] = true;
    }
    return false;
}

bool VirusWarField::isValidMove(int x, int y) const
{
    if (!isInBounds(x, y))
        return false;

    const Cell c = cell(x, y);
    if (c == Cell::CatchX || c == Cell::CatchO || c == Cell::X)
        return false;

    if ((x == 0 && y == 0 && c != Cell::O && cell(width_ - 1, height_ - 1) != Cell::X) ||
        (x == width_ - 1 && y == height_ - 1 && c != Cell::O && cell(0, 0) != Cell::X))
        return true;

    std::vector<bool> visited(width_ * height_, false);
    return isConnectedToX(x, y, visited);
}

bool VirusWarField::applyMove(int x, int y)
{
    if (++moveIndex_ == 3)
        moveIndex_ = 0;

    const bool captured = cell(x, y) == Cell::O;
    setCell(x, y, captured ? Cell::CatchX : Cell::X);
    return captured;
}

bool VirusWarField::canCompleteTurn(int& found, std::vector<bool>& marked, int movesPerTurn)
{
    for (int i = 0; i < width_ && found < movesPerTurn; i++) {
        for (int j = 0; j < height_ && found < movesPerTurn; j++) {
            std::vector<bool> visited(width_ * height_, false);
            const Cell c = cell(i, j);
            if (c == Cell::CatchX || c == Cell::CatchO || c == Cell::X)
                continue;
            if (!isConnectedToX(i, j, visited))
                continue;

            if (!marked[cellIndex(i, j)])
                found++;
            marked[cellIndex(i, j)] = true;

            const Cell replaced = (c == Cell::O) ? Cell::CatchX : Cell::X;
            setCell(i, j, replaced);
            if (canCompleteTurn(found, marked, movesPerTurn)) {
                setCell(i, j, c);
                return true;
            }
            setCell(i, j, c);
        }
    }

    if (found >= movesPerTurn ||
        cell(0, 0) == Cell::Clear ||
        cell(width_ - 1, height_ - 1) == Cell::Clear)
        return true;

    return false;
}

bool VirusWarField::hasMovesForNextTurn(int movesPerTurn)
{
    int found = 0;
    std::vector<bool> marked(width_ * height_, false);
    return canCompleteTurn(found, marked, movesPerTurn);
}
