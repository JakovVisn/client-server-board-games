#include "game_session.h"
#include <limits>
#include <QDebug>
#include "auth_manager.h"
#include "client.h"

int GameSession::nextId_ = 0;

GameSession::GameSession(std::unique_ptr<AbstractGame> game,
                         Client* player0, Client* player1,
                         AuthManager* authManager,
                         const TimeControl& timeControl)
    : game_(std::move(game))
    , players_{player0, player1}
    , currentPlayerIndex_(0)
    , isFinished_(false)
    , authManager_(authManager)
    , id_(nextId_++)
    , timeControl_(timeControl)
    , clockMs_{timeControl.baseMs, timeControl.baseMs}
{
    deadline_.setSingleShot(true);
    deadline_.setTimerType(Qt::PreciseTimer);
    QObject::connect(&deadline_, &QTimer::timeout, [this] { onTimeout(); });
}

int GameSession::id() const { return id_; }

void GameSession::start()
{
    if (!players_[0]->isBot()) players_[0]->setSession(this);
    if (!players_[1]->isBot()) players_[1]->setSession(this);
    sendFound(players_[0], players_[1], "first");
    sendFound(players_[1], players_[0], "second");
    startTurn();
    sendGo(players_[0]);
}

bool GameSession::handleMove(Client* sender, int x, int y)
{
    if (isFinished_ || sender != currentPlayer())
        return false;

    // The deadline timer may not have fired yet when a late move arrives
    if (timeControl_.mode != TimeControl::Mode::None && timeLeftMs() <= 0) {
        onTimeout();
        return true;
    }

    if (!game_->isValidMove(x, y)) {
        sendWrongstep(sender);
        sendGo(sender);
        return true;
    }

    const QString type = QString::fromStdString(game_->applyMove(x, y));

    sendField2(currentPlayer(), type, x, y);
    sendField1(waitingPlayer(), type, x, y);

    switch (game_->turnResult()) {
    case AbstractGame::TurnResult::Ongoing:
        sendGo(currentPlayer());
        break;

    case AbstractGame::TurnResult::TurnComplete:
        endTurn(true);
        game_->advanceTurn();
        currentPlayerIndex_ = 1 - currentPlayerIndex_;
        startTurn();
        sendGo(currentPlayer());
        break;

    case AbstractGame::TurnResult::GameOver:
        endTurn(false);
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

// ── Clocks ────────────────────────────────────────────────────────────────────

void GameSession::startTurn()
{
    if (timeControl_.mode == TimeControl::Mode::None) return;
    turnClock_.start();
    armDeadline();
}

void GameSession::armDeadline()
{
    // QTimer takes an int; onTimeout() re-arms if a longer clock still has time left
    deadline_.start(static_cast<int>(qMin<qint64>(timeLeftMs(), std::numeric_limits<int>::max())));
}

void GameSession::endTurn(bool completed)
{
    if (timeControl_.mode == TimeControl::Mode::None) return;
    deadline_.stop();
    if (timeControl_.mode == TimeControl::Mode::Game) {
        clockMs_[currentPlayerIndex_] -= turnClock_.elapsed();
        if (completed)
            clockMs_[currentPlayerIndex_] += timeControl_.incrementMs;
    }
}

qint64 GameSession::timeLeftMs() const
{
    const qint64 budget = (timeControl_.mode == TimeControl::Mode::Turn)
        ? timeControl_.baseMs : clockMs_[currentPlayerIndex_];
    return qMax<qint64>(0, budget - turnClock_.elapsed());
}

void GameSession::onTimeout()
{
    if (isFinished_) return;
    if (timeLeftMs() > 0) {  // fired early, or the clock exceeds one QTimer interval
        armDeadline();
        return;
    }
    qDebug() << "Game" << id_ << ":" << currentPlayer()->login() << "ran out of time"
             << "(" << timeControl_.toString() << ")";
    endTurn(false);
    finishGame(waitingPlayer(), currentPlayer());
}

void GameSession::finishGame(Client* winner, Client* loser)
{
    deadline_.stop();
    if (authManager_) {
        authManager_->recordWin(winner->login());
        authManager_->recordLoss(loser->login());
    }

    sendWin(winner);
    sendLose(loser);

    auto cleanup = [](Client* p) {
        if (p->status() != Client::Status::Disconnected)
            p->setStatus(Client::Status::Authorized);
        p->setSession(nullptr);  // no-op for bots (was never set)
    };
    cleanup(winner);
    cleanup(loser);

    isFinished_ = true;
    if (onFinished_) onFinished_();
}

void GameSession::finishGameDisconnect(Client* winner, Client* loser)
{
    deadline_.stop();
    if (authManager_) {
        authManager_->recordWin(winner->login());
        authManager_->recordLoss(loser->login());
    }

    // Bots get abort (no reward), humans get win/lose as usual
    winner->isBot() ? sendAbort(winner) : sendWin(winner);
    if (loser->status() != Client::Status::Disconnected)
        loser->isBot() ? sendAbort(loser) : sendLose(loser);

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

// ── Unified message helpers ───────────────────────────────────────────────────

void GameSession::sendFound(Client* receiver, Client* opponent, const QString& role) const
{
    const QString gameInfo = QString("%1:%2:%3:")
        .arg(QString::fromStdString(game_->gameName())).arg(game_->boardWidth()).arg(game_->boardHeight());
    receiver->send(QString("found:%1:%2:%3%4:%5:")
        .arg(id_).arg(opponent->login()).arg(gameInfo).arg(role).arg(timeControl_.toString()));
}

void GameSession::sendGo(Client* player) const
{
    QString msg = QString("go:%1:%2:%3:")
        .arg(QString::fromStdString(game_->serializeForCurrentPlayer()))
        .arg(game_->movesRemaining())
        .arg(id_);
    // With a time control: the mover's time left, then the opponent's
    // (Turn mode: the full turn limit the opponent will get)
    if (timeControl_.mode != TimeControl::Mode::None) {
        const qint64 opponentMs = (timeControl_.mode == TimeControl::Mode::Turn)
            ? timeControl_.baseMs : clockMs_[1 - currentPlayerIndex_];
        msg += QString("%1:%2:").arg(timeLeftMs()).arg(opponentMs);
    }
    player->send(msg);
}

void GameSession::sendField1(Client* player, const QString& type, int x, int y) const
{
    player->send(QString("field1:%1:%2:%3:%4:").arg(type).arg(x).arg(y).arg(id_));
}

void GameSession::sendField2(Client* player, const QString& type, int x, int y) const
{
    player->send(QString("field2:%1:%2:%3:%4:").arg(type).arg(x).arg(y).arg(id_));
}

void GameSession::sendWrongstep(Client* player) const
{
    player->send(QString("wrongstep:%1:").arg(id_));
}

void GameSession::sendWin(Client* player) const
{
    player->send(QString("win:%1:").arg(id_));
}

void GameSession::sendLose(Client* player) const
{
    player->send(QString("lose:%1:").arg(id_));
}

void GameSession::sendAbort(Client* player) const
{
    player->send(QString("abort:%1:").arg(id_));
}
