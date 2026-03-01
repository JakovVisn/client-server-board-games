#include "gamestate.h"

void GameState::reset(int width, int height)
{
    width_ = width;
    height_ = height;
    cells_ = QVector<Cell>(width * height, Cell::Clear);
}

void GameState::fromString(const QString& s)
{
    for (int i = 0; i < s.size() && i < cells_.size(); ++i) {
        const int v = s[i].digitValue();
        if (v >= 0 && v <= 4)
            cells_[i] = static_cast<Cell>(v);
    }
}

Cell GameState::cell(int x, int y) const
{
    if (!inBounds(x, y)) return Cell::Clear;
    return cells_[y * width_ + x];
}

void GameState::setCell(int x, int y, Cell c)
{
    if (!inBounds(x, y)) return;
    cells_[y * width_ + x] = c;
}

bool GameState::inBounds(int x, int y) const
{
    return x >= 0 && y >= 0 && x < width_ && y < height_;
}
