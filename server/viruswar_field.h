#pragma once

#include <QVector>
#include <QString>

class VirusWarField
{
public:
    enum class Cell
    {
        Clear = 0,
        X,
        O,
        CatchX,
        CatchO,
    };

    VirusWarField(int width, int height);

    [[nodiscard]] int width() const;
    [[nodiscard]] int height() const;
    [[nodiscard]] int moveIndex() const;

    [[nodiscard]] Cell cell(int x, int y) const;
    void setCell(int x, int y, Cell value);

    [[nodiscard]] bool isValidMove(int x, int y) const;
    [[nodiscard]] bool applyMove(int x, int y);
    [[nodiscard]] bool hasMovesForNextTurn(int movesPerTurn);

    [[nodiscard]] QString serialize() const;

private:
    int width_;
    int height_;
    int moveIndex_;
    QVector<Cell> cells_;

    [[nodiscard]] int cellIndex(int x, int y) const;
    [[nodiscard]] bool isInBounds(int x, int y) const;
    [[nodiscard]] bool isConnectedToX(int x, int y, QVector<bool>& visited) const;
    [[nodiscard]] bool canCompleteTurn(int& found, QVector<bool>& marked, int movesPerTurn);
};
