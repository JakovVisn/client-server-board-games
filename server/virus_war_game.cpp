#include "virus_war_game.h"
#include <memory>

VirusWarGame::VirusWarGame(int width, int height)
    : width_(width), height_(height)
    , fields_{{width, height}, {width, height}}
    , currentPlayer_(0)
{
}

std::string VirusWarGame::gameName() const { return "viruswar"; }
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

int VirusWarGame::currentPlayer() const { return currentPlayer_; }

bool VirusWarGame::isValidMove(int x, int y) const
{
    return fields_[currentPlayer_].isValidMove(x, y);
}

std::string VirusWarGame::applyMove(int x, int y)
{
    const bool captured = fields_[currentPlayer_].applyMove(x, y);
    fields_[1 - currentPlayer_].setCell(x, y, captured ? VirusWarField::Cell::CatchO : VirusWarField::Cell::O);
    return captured ? "caught" : "standard";
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

std::string VirusWarGame::serializeForCurrentPlayer() const
{
    return fields_[currentPlayer_].serialize();
}

std::string VirusWarGame::serializeForOpponent() const
{
    return fields_[1 - currentPlayer_].serialize();
}

void VirusWarGame::advanceTurn()
{
    currentPlayer_ = 1 - currentPlayer_;
}

void VirusWarGame::initFromBoard(const std::string& board, int movesRemaining)
{
    // board is serialised from currentPlayer_'s perspective:
    //   '1' = my cell (X), '2' = opponent cell (O),
    //   '3' = my captured (CatchX), '4' = opponent captured (CatchO)
    // Both fields are rebuilt, and the turn position is set from movesRemaining.
    const int me  = currentPlayer_;
    const int opp = 1 - currentPlayer_;
    for (int i = 0; i < width_ * height_; ++i) {
        VirusWarField::Cell myCell, oppCell;
        switch (board[i]) {
        case '1': myCell = VirusWarField::Cell::X;      oppCell = VirusWarField::Cell::O;      break;
        case '2': myCell = VirusWarField::Cell::O;      oppCell = VirusWarField::Cell::X;      break;
        case '3': myCell = VirusWarField::Cell::CatchX; oppCell = VirusWarField::Cell::CatchO; break;
        case '4': myCell = VirusWarField::Cell::CatchO; oppCell = VirusWarField::Cell::CatchX; break;
        default:  myCell = VirusWarField::Cell::Clear;  oppCell = VirusWarField::Cell::Clear;  break;
        }
        fields_[me ].setCell(i % width_, i / width_, myCell);
        fields_[opp].setCell(i % width_, i / width_, oppCell);
    }
    fields_[me ].setMoveIndex(kMovesPerTurn - movesRemaining);
    fields_[opp].setMoveIndex(0);
}

std::unique_ptr<AbstractGame> VirusWarGame::create(int w, int h) const
{
    return std::make_unique<VirusWarGame>(w, h);
}
