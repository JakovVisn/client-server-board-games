#pragma once

#include <QVector>
#include <QString>

enum class Cell {
    Clear = 0,
    Mine = 1,
    Opponent = 2,
    CapturedByMe = 3,
    CapturedByOpp = 4,
};

class GameState
{
public:
    GameState() = default;

    void reset(int width, int height);
    void fromString(const QString& s);

    [[nodiscard]] int width() const { return width_; }
    [[nodiscard]] int height() const { return height_; }
    [[nodiscard]] bool isValid() const { return width_ > 0 && height_ > 0; }

    [[nodiscard]] Cell cell(int x, int y) const;
    void setCell(int x, int y, Cell c);

private:
    int width_ = 0;
    int height_ = 0;
    QVector<Cell> cells_;

    [[nodiscard]] bool inBounds(int x, int y) const;
};
