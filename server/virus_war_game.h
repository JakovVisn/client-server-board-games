#pragma once

#include "abstract_game.h"
#include "viruswar_field.h"

class VirusWarGame : public AbstractGame
{
public:
    static constexpr int kDefaultWidth = 10;
    static constexpr int kDefaultHeight = 10;

    explicit VirusWarGame(int width = kDefaultWidth, int height = kDefaultHeight);
    VirusWarGame(const VirusWarGame&) = default;

    [[nodiscard]] std::string gameName() const override;
    [[nodiscard]] int boardWidth() const override;
    [[nodiscard]] int boardHeight() const override;
    [[nodiscard]] std::pair<int,int> defaultBoardSize() const override;
    [[nodiscard]] bool isValidBoardSize(int w, int h) const override;
    [[nodiscard]] int movesRemaining() const override;

    [[nodiscard]] int currentPlayer() const;
    [[nodiscard]] bool isValidMove(int x, int y) const override;
    [[nodiscard]] std::string applyMove(int x, int y) override;
    [[nodiscard]] TurnResult turnResult() const override;

    [[nodiscard]] std::string serializeForCurrentPlayer() const override;
    [[nodiscard]] std::string serializeForOpponent() const override;

    void advanceTurn() override;
    // movesRemaining: 1..3, sub-moves the current player still has this turn
    void initFromBoard(const std::string& board, int movesRemaining = kMovesPerTurn);

    [[nodiscard]] std::unique_ptr<AbstractGame> create(int w, int h) const override;

private:
    static constexpr int kMovesPerTurn = 3;
    static constexpr int kMinSize = 5;
    static constexpr int kMaxSize = 20;

    int width_;
    int height_;
    VirusWarField fields_[2];
    int currentPlayer_;
};
