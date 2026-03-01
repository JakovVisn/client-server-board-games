#include "gamestate.h"

void GameState::reset(int width, int height)
{
    width_ = width;
    height_ = height;
    cells_      = QVector<Cell>(width * height, Cell::Clear);
    newOverlay_ = QVector<NewCell>(width * height, NewCell::None);
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

NewCell GameState::newCell(int x, int y) const
{
    if (!inBounds(x, y)) return NewCell::None;
    return newOverlay_[y * width_ + x];
}

void GameState::setNewCell(int x, int y, NewCell value)
{
    if (!inBounds(x, y)) return;
    newOverlay_[y * width_ + x] = value;
}

void GameState::clearNewCells()
{
    newOverlay_.fill(NewCell::None);
}

void GameState::clearMyNewCells()
{
    for (auto& nc : newOverlay_)
        if (nc == NewCell::MyMove || nc == NewCell::MyCatch)
            nc = NewCell::None;
}

void GameState::clearOppNewCells()
{
    for (auto& nc : newOverlay_)
        if (nc == NewCell::OppMove || nc == NewCell::OppCatch)
            nc = NewCell::None;
}

bool GameState::inBounds(int x, int y) const
{
    return x >= 0 && y >= 0 && x < width_ && y < height_;
}
