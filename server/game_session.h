#pragma once

#include <functional>
#include <memory>
#include "abstract_game.h"

class Client;

class GameSession
{
public:
    explicit GameSession(std::unique_ptr<AbstractGame> game,
                         Client* player0, Client* player1);

    void setOnFinished(std::function<void()> cb) { onFinished_ = std::move(cb); }

    void start();
    bool handleMove(Client* sender, int x, int y);
    void handleDisconnect(Client* disconnected);

    [[nodiscard]] bool isFinished() const;
    [[nodiscard]] Client* opponentOf(const Client* client) const;
    [[nodiscard]] bool involvesPlayer(const Client* c) const;

private:
    std::unique_ptr<AbstractGame> game_;
    Client* players_[2];
    int currentPlayerIndex_;
    bool isFinished_;
    std::function<void()> onFinished_;

    [[nodiscard]] Client* currentPlayer() const;
    [[nodiscard]] Client* waitingPlayer() const;

    void finishGame(Client* winner, Client* loser);
    void finishGameDisconnect(Client* winner, Client* loser);

    void sendFound(Client* receiver, Client* opponent, const QString& role) const;
    void sendGo(Client* player) const;
    void sendField1(Client* player, const QString& type, int x, int y) const;
    void sendField2(Client* player, const QString& type, int x, int y) const;
    void sendWrongstep(Client* player) const;
    void sendWin(Client* player) const;
    void sendLose(Client* player) const;
};
