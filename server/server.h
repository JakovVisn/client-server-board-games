#pragma once

#include <list>
#include <map>
#include <memory>
#include <QHostAddress>
#include <QMap>
#include <QTcpServer>
#include "abstract_game.h"
#include "auth_manager.h"
#include "client.h"
#include "game_session.h"

class Server : public QTcpServer
{
    Q_OBJECT

public:
    explicit Server(const QString& authFile, const QString& statsFile,
                    QObject* parent = nullptr);
    [[nodiscard]] bool start(const QHostAddress& address, quint16 port);
    void setDefaultTimeControl(const TimeControl& tc) { defaultTimeControl_ = tc; }

protected:
    void incomingConnection(qintptr socketDescriptor) override;
    void timerEvent(QTimerEvent* event) override;

private:
    AuthManager authManager_;
    std::map<QString, std::unique_ptr<AbstractGame>> gamePrototypes_;
    std::list<Client*> clients_;
    std::list<std::unique_ptr<GameSession>> sessions_;
    QMap<int, GameSession*> sessionById_;
    TimeControl defaultTimeControl_;

    void parseData(const QString& cmd, Client* client);
    [[nodiscard]] bool stateAuthorize(const QString& cmd, Client* client);
    [[nodiscard]] bool stateReceiveSteps(const QString& cmd, Client* client);
    [[nodiscard]] bool stateReceiveResign(const QString& cmd, Client* client);
    [[nodiscard]] static bool stateReceivePing(const QString& cmd, Client* client);
    [[nodiscard]] static bool stateReceivePong(const QString& cmd, Client* client);
    [[nodiscard]] static bool checkProtocolVersion(const QString& version);
    [[nodiscard]] bool isAlreadyConnected(const QString& login) const;
    [[nodiscard]] std::unique_ptr<AbstractGame> makeGame(const Client* a, const Client* b) const;
    [[nodiscard]] static bool sameGame(const Client* a, const Client* b);
    [[nodiscard]] TimeControl timeControlFor(const Client* a, const Client* b) const;
    [[nodiscard]] int countActiveSessions(const Client* a, const Client* b) const;
    void matchClients();
    void connectTwoClients(Client* client1, Client* client2);
    void connectPlayerWithBot(Client* player, Client* bot);
    void connectTwoBots(Client* bot1, Client* bot2);
    void connectAutoWithBot(Client* seeker, Client* bot);
    void disconnectClient(Client* client);
    void markClientDisconnected(Client* client) const;

private slots:
    void onClientDataReceived(Client* client, const QString& data);
    void onClientDisconnected(Client* client);
};
