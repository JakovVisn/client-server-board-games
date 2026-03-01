#include "game_session.h"
#include "client.h"

GameSession::GameSession(std::unique_ptr<AbstractGame> game,
                         Client* player0, Client* player1)
    : game_(std::move(game))
    , players_{player0, player1}
    , currentPlayerIndex_(0)
    , isFinished_(false)
{
}

void GameSession::start()
{
    players_[0]->setSession(this);
    players_[1]->setSession(this);
    sendFound(players_[0], players_[1], "first");
    sendFound(players_[1], players_[0], "second");
    sendGo(players_[0]);
}

bool GameSession::handleMove(Client* sender, int x, int y)
{
    if (isFinished_ || sender != currentPlayer())
        return false;

    if (!game_->isValidMove(x, y)) {
        sendWrongstep(sender);
        sendGo(sender);
        return true;
    }

    const QString type = game_->applyMove(x, y);

    sendField2(currentPlayer(), type, x, y);
    sendField1(waitingPlayer(), type, x, y);

    switch (game_->turnResult()) {
    case AbstractGame::TurnResult::Ongoing:
        sendGo(currentPlayer());
        break;

    case AbstractGame::TurnResult::TurnComplete:
        game_->advanceTurn();
        currentPlayerIndex_ = 1 - currentPlayerIndex_;
        sendGo(currentPlayer());
        break;

    case AbstractGame::TurnResult::GameOver:
        finishGame(currentPlayer(), waitingPlayer());
        break;
    }

    return true;
}

void GameSession::handleDisconnect(Client* disconnected)
{
    if (isFinished_) return;
    finishGameDisconnect(opponentOf(disconnected), disconnected);
}

bool GameSession::isFinished() const { return isFinished_; }

Client* GameSession::opponentOf(const Client* client) const
{
    return (client == players_[0]) ? players_[1] : players_[0];
}

bool GameSession::involvesPlayer(const Client* c) const
{
    return players_[0] == c || players_[1] == c;
}

Client* GameSession::currentPlayer() const { return players_[currentPlayerIndex_]; }
Client* GameSession::waitingPlayer() const { return players_[1 - currentPlayerIndex_]; }

void GameSession::finishGame(Client* winner, Client* loser)
{
    sendWin(winner);
    sendLose(loser);

    auto cleanup = [](Client* p) {
        if (p->status() != Client::Status::Disconnected)
            p->setStatus(Client::Status::Authorized);
        p->setSession(nullptr);
    };
    cleanup(winner);
    cleanup(loser);

    isFinished_ = true;
    if (onFinished_) onFinished_();
}

void GameSession::finishGameDisconnect(Client* winner, Client* loser)
{
    sendWin(winner);
    if (loser->status() != Client::Status::Disconnected)
        sendLose(loser);

    auto cleanup = [](Client* p) {
        if (p->status() != Client::Status::Disconnected)
            p->setStatus(Client::Status::Authorized);
        p->setSession(nullptr);
    };
    cleanup(winner);
    cleanup(loser);

    isFinished_ = true;
    if (onFinished_) onFinished_();
}

// ── Message helpers ───────────────────────────────────────────────────────────

void GameSession::sendFound(Client* receiver, Client* opponent, const QString& role) const
{
    receiver->send(QString("found:%1:%2:%3:%4:%5:")
        .arg(opponent->login())
        .arg(game_->gameName())
        .arg(game_->boardWidth())
        .arg(game_->boardHeight())
        .arg(role));
}

void GameSession::sendGo(Client* player) const
{
    player->send(QString("go:%1:%2:")
        .arg(game_->serializeForCurrentPlayer())
        .arg(game_->movesRemaining()));
}

void GameSession::sendField1(Client* player, const QString& type, int x, int y) const
{
    player->send(QString("field1:%1:%2:%3:").arg(type).arg(x).arg(y));
}

void GameSession::sendField2(Client* player, const QString& type, int x, int y) const
{
    player->send(QString("field2:%1:%2:%3:").arg(type).arg(x).arg(y));
}

void GameSession::sendWrongstep(Client* player) const
{
    player->send("wrongstep:");
}

void GameSession::sendWin(Client* player) const
{
    player->send("win:");
}

void GameSession::sendLose(Client* player) const
{
    player->send("lose:");
}
