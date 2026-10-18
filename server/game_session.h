#pragma once

#include <functional>
#include <memory>
#include <QElapsedTimer>
#include <QTimer>
#include "abstract_game.h"
#include "time_control.h"

class AuthManager;
class Client;

// Why a game ended; sent as the CAUSE field of win: / lose:
enum class GameOverCause {
    Rules,       // the game's rules ended it (Virus War: the opponent can't make a full turn)
    Timeout,     // the loser ran out of time
    Resign,      // the loser sent resign:
    Disconnect,  // the loser's connection was lost or timed out
};

class GameSession
{
public:
    explicit GameSession(std::unique_ptr<AbstractGame> game,
                         Client* player0, Client* player1,
                         AuthManager* authManager,
                         const TimeControl& timeControl = {});

    void setOnFinished(std::function<void()> cb) { onFinished_ = std::move(cb); }

    void start();
    bool handleMove(Client* sender, int x, int y);
    bool handleResign(Client* sender);
    void handleDisconnect(Client* disconnected);

    [[nodiscard]] int id() const;
    [[nodiscard]] bool isFinished() const;
    [[nodiscard]] Client* opponentOf(const Client* client) const;
    [[nodiscard]] bool involvesPlayer(const Client* c) const;

private:
    std::unique_ptr<AbstractGame> game_;
    Client* players_[2];
    int currentPlayerIndex_;
    bool isFinished_;
    AuthManager* authManager_;
    int id_;

    TimeControl timeControl_;
    qint64 clockMs_[2];          // Game mode: each player's remaining time, between turns
    QElapsedTimer turnClock_;    // time spent in the current turn
    QTimer deadline_;            // fires when the player to move runs out of time

    static int nextId_;
    std::function<void()> onFinished_;

    [[nodiscard]] Client* currentPlayer() const;
    [[nodiscard]] Client* waitingPlayer() const;

    void startTurn();
    void armDeadline();
    void endTurn(bool completed);
    [[nodiscard]] qint64 timeLeftMs() const;
    void onTimeout();

    void finishGame(Client* winner, Client* loser, GameOverCause cause);

    void sendFound(Client* receiver, Client* opponent, const QString& role) const;
    void sendGo(Client* player) const;
    void sendField1(Client* player, const QString& type, int x, int y) const;
    void sendField2(Client* player, const QString& type, int x, int y) const;
    void sendWrongStep(Client* player) const;
    [[nodiscard]] static QString causeName(GameOverCause cause);
    void sendResult(Client* player, bool won, GameOverCause cause) const;
    void sendAbort(Client* player) const;
};
