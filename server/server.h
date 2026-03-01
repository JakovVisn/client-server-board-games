#pragma once

#include <list>
#include <QTcpServer>
#include "auth_manager.h"
#include "client.h"

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
    std::list<Client*> clients_;

    void parseData(const QString& cmd, Client* client);
    [[nodiscard]] bool stateAuthorize(const QString& cmd, Client* client);
    [[nodiscard]] static bool stateReceivePing(const QString& cmd, Client* client);
    [[nodiscard]] static bool stateReceivePong(const QString& cmd, Client* client);
    [[nodiscard]] static bool checkProtocolVersion(const QString& version);
    [[nodiscard]] bool isAlreadyConnected(const QString& login) const;

private slots:
    void onClientDataReceived(Client* client, const QString& data);
    void onClientDisconnected(Client* client);
};
