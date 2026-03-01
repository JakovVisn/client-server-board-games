#pragma once

#include <list>
#include <map>
#include <memory>
#include <QTcpServer>
#include "abstract_game.h"
#include "auth_manager.h"
#include "client.h"
#include "game_session.h"

class Server : public QTcpServer
{
    Q_OBJECT

public:
    explicit Server(QObject* parent = nullptr);
    [[nodiscard]] bool start();

protected:
    void incomingConnection(qintptr socketDescriptor) override;
    void timerEvent(QTimerEvent* event) override;

private:
    AuthManager authManager_;
    std::map<QString, std::unique_ptr<AbstractGame>> gamePrototypes_;
    std::list<Client*> clients_;
    std::list<std::unique_ptr<GameSession>> sessions_;

    void parseData(const QString& cmd, Client* client);
    [[nodiscard]] bool stateAuthorize(const QString& cmd, Client* client);
    [[nodiscard]] bool stateHandleStep(const QString& cmd, Client* client);
    [[nodiscard]] static bool stateReceivePing(const QString& cmd, Client* client);
    [[nodiscard]] static bool stateReceivePong(const QString& cmd, Client* client);
    [[nodiscard]] static bool checkProtocolVersion(const QString& version);
    [[nodiscard]] bool isAlreadyConnected(const QString& login) const;
    [[nodiscard]] std::unique_ptr<AbstractGame> makeGame(const QString& name, int w, int h) const;
    [[nodiscard]] static bool sameGame(const Client* a, const Client* b);
    [[nodiscard]] int countActiveSessions(const Client* a, const Client* b) const;
    void matchClients();
    void startGame(Client* p0, Client* p1);
    void disconnectClient(Client* client);
    void markClientDisconnected(Client* client) const;

private slots:
    void onClientDataReceived(Client* client, const QString& data);
    void onClientDisconnected(Client* client);
};
