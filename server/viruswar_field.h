#pragma once

#include <string>
#include <vector>

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
    void setMoveIndex(int index);  // sub-moves already made this turn

    [[nodiscard]] Cell cell(int x, int y) const;
    void setCell(int x, int y, Cell value);

    [[nodiscard]] bool isValidMove(int x, int y) const;
    [[nodiscard]] bool applyMove(int x, int y);
    [[nodiscard]] bool hasMovesForNextTurn(int movesPerTurn);

    [[nodiscard]] std::string serialize() const;
    // The inverse of serialize(): a board as sent in go:, from its owner's view
    [[nodiscard]] static VirusWarField fromBoard(const std::string& board, int width, int height);

private:
    int width_;
    int height_;
    int moveIndex_;
    std::vector<Cell> cells_;

    [[nodiscard]] int cellIndex(int x, int y) const;
    [[nodiscard]] bool isInBounds(int x, int y) const;
    [[nodiscard]] bool isConnectedToX(int x, int y, std::vector<bool>& visited) const;
    [[nodiscard]] bool canCompleteTurn(int& found, std::vector<bool>& marked, int movesPerTurn);
};
