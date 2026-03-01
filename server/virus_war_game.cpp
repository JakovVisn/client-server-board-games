#include "virus_war_game.h"
#include <memory>

VirusWarGame::VirusWarGame(int width, int height)
    : width_(width), height_(height)
    , fields_{{width, height}, {width, height}}
    , currentPlayer_(0)
{
}

QString VirusWarGame::gameName() const { return QStringLiteral("viruswar"); }
int VirusWarGame::boardWidth() const { return width_; }
int VirusWarGame::boardHeight() const { return height_; }
std::pair<int,int> VirusWarGame::defaultBoardSize() const
{
    return {kDefaultWidth, kDefaultHeight};
}

bool VirusWarGame::isValidBoardSize(int w, int h) const
{
    return w >= kMinSize && w <= kMaxSize && h >= kMinSize && h <= kMaxSize;
}

int VirusWarGame::movesRemaining() const
{
    return kMovesPerTurn - fields_[currentPlayer_].moveIndex();
}

bool VirusWarGame::isValidMove(int x, int y) const
{
    return fields_[currentPlayer_].isValidMove(x, y);
}

QString VirusWarGame::applyMove(int x, int y)
{
    const bool captured = fields_[currentPlayer_].applyMove(x, y);
    fields_[1 - currentPlayer_].setCell(x, y, captured ? VirusWarField::Cell::CatchO : VirusWarField::Cell::O);
    return captured ? QStringLiteral("caught") : QStringLiteral("standard");
}

AbstractGame::TurnResult VirusWarGame::turnResult() const
{
    if (fields_[currentPlayer_].moveIndex() != 0)
        return TurnResult::Ongoing;

    VirusWarField opponentSnapshot = fields_[1 - currentPlayer_];
    if (!opponentSnapshot.hasMovesForNextTurn(kMovesPerTurn))
        return TurnResult::GameOver;

    return TurnResult::TurnComplete;
}

QString VirusWarGame::serializeForCurrentPlayer() const
{
    return fields_[currentPlayer_].serialize();
}

QString VirusWarGame::serializeForOpponent() const
{
    return fields_[1 - currentPlayer_].serialize();
}

void VirusWarGame::advanceTurn()
{
    currentPlayer_ = 1 - currentPlayer_;
}

std::unique_ptr<AbstractGame> VirusWarGame::create(int w, int h) const
{
    return std::make_unique<VirusWarGame>(w, h);
}
