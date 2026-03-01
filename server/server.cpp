#include <QTcpSocket>
#include "server.h"
#include "server_constants.h"

Server::Server(QObject* parent)
    : QTcpServer(parent)
{
}

bool Server::start()
{
    if (!listen(ServerConfig::ADDRESS, ServerConfig::PORT)) {
        qCritical() << "Unable to start the server:" << errorString();
        return false;
    }

    startTimer(ServerConfig::SEARCH_INTERVAL);
    qDebug() << "Server started on port" << ServerConfig::PORT;
    return true;
}

void Server::incomingConnection(qintptr socketDescriptor)
{
    auto* socket = new QTcpSocket(this);
    if (!socket->setSocketDescriptor(socketDescriptor)) {
        qWarning() << "Failed to set socket descriptor";
        socket->deleteLater();
        return;
    }

    auto* client = new Client(socket, this);
    connect(client, &Client::dataReceived, this, [this, client](const QString& data) {
        onClientDataReceived(client, data);
    });
    connect(client, &Client::disconnected, this, &Server::onClientDisconnected);

    clients_.push_back(client);
    qDebug() << "New connection";
}

void Server::onClientDataReceived(Client* client, const QString& data)
{
    parseData(data, client);
}

void Server::parseData(const QString& cmd, Client* client)
{
    qDebug() << "←" << cmd;

    if (stateReceivePing(cmd, client)) return;
    if (stateReceivePong(cmd, client)) return;

    client->send("wrongcmd:");
}

bool Server::stateReceivePing(const QString& cmd, Client* client)
{
    if (cmd != "ping:") return false;
    client->send("pong:");
    return true;
}

bool Server::stateReceivePong(const QString& cmd, Client* client)
{
    if (cmd != "pong:") return false;
    client->updateActivity();
    return true;
}

void Server::timerEvent(QTimerEvent* event)
{
    Q_UNUSED(event);

    for (auto it = clients_.begin(); it != clients_.end(); ) {
        Client* client = *it;

        if (client->status() == Client::Status::Disconnected) {
            it = clients_.erase(it);
            client->deleteLater();
            continue;
        }

        if (client->millisecondsSinceLastActivity() >= ServerConfig::INACTIVE_MIN_MS
                && !client->isPingPending()) {
            client->send("ping:");
            client->markPingSent();
        }

        if (client->millisecondsSinceLastActivity() >= ServerConfig::INACTIVE_MAX_MS) {
            client->setStatus(Client::Status::Disconnected);
            client->disconnect();
        }

        ++it;
    }
}

void Server::onClientDisconnected(Client* client)
{
    client->setStatus(Client::Status::Disconnected);
    qDebug() << "Client disconnected";
}
