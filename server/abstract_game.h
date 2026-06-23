#pragma once

#include <memory>
#include <string>
#include <utility>

class AbstractGame
{
public:
    enum class TurnResult {
        Ongoing,
        TurnComplete,
        GameOver,
    };

    virtual ~AbstractGame() = default;

    [[nodiscard]] virtual std::string gameName() const = 0;
    [[nodiscard]] virtual int boardWidth() const = 0;
    [[nodiscard]] virtual int boardHeight() const = 0;
    [[nodiscard]] virtual std::pair<int,int> defaultBoardSize() const = 0;
    [[nodiscard]] virtual bool isValidBoardSize(int w, int h) const = 0;
    [[nodiscard]] virtual int movesRemaining() const = 0;

    [[nodiscard]] virtual bool isValidMove(int x, int y) const = 0;
    [[nodiscard]] virtual std::string applyMove(int x, int y) = 0;
    [[nodiscard]] virtual TurnResult turnResult() const = 0;

    [[nodiscard]] virtual std::string serializeForCurrentPlayer() const = 0;
    [[nodiscard]] virtual std::string serializeForOpponent() const = 0;

    virtual void advanceTurn() = 0;

    [[nodiscard]] virtual std::unique_ptr<AbstractGame> create(int w, int h) const = 0;
};
