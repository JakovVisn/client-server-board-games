#include <algorithm>
#include <QRegularExpression>
#include <QTcpSocket>
#include "server.h"
#include "server_constants.h"
#include "virus_war_game.h"

Server::Server(QObject* parent)
    : QTcpServer(parent), authManager_(AuthConfig::FILE_PATH, AuthConfig::STATS_FILE_PATH)
{
    gamePrototypes_[ServerConfig::DEFAULT_GAME_NAME] = std::make_unique<VirusWarGame>();
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
    qDebug() << client->login() << "←" << cmd;

    if (stateAuthorize(cmd, client))    return;
    if (stateHandleStep(cmd, client))   return;
    if (stateReceivePing(cmd, client))  return;
    if (stateReceivePong(cmd, client))  return;

    client->send("wrongcmd:");
}

bool Server::stateAuthorize(const QString& cmd, Client* client)
{
    if (client->status() != Client::Status::Unauthenticated)
        return false;

    static const QRegularExpression rx(
        QString(R"(mbclient:(\d+):([\w]{%1,%2}):([\w]{%3,%4}):)")
            .arg(AuthConfig::LOGIN_LENGTH_MIN)
            .arg(AuthConfig::LOGIN_LENGTH_MAX)
            .arg(AuthConfig::PASSWORD_LENGTH_MIN)
            .arg(AuthConfig::PASSWORD_LENGTH_MAX)
    );

    const auto match = rx.match(cmd);
    if (!match.hasMatch())
        return false;

    const QString version  = match.captured(1);
    const QString login    = match.captured(2);
    const QString password = match.captured(3);

    client->setLogin(login);

    if (!checkProtocolVersion(version)) {
        client->send("wrongver:");
        client->disconnect();
        return true;
    }

    AuthManager::Result result = authManager_.verify(login, password);
    if (result == AuthManager::Result::NotFound)
        result = authManager_.registerUser(login, password);

    if (result == AuthManager::Result::Authorized) {
        if (isAlreadyConnected(login)) {
            client->send("alreadyconnected:");
            client->disconnect();
            return true;
        }
        client->setStatus(Client::Status::Authorized);
        client->send(QString("mbserver:%1:")
            .arg(static_cast<int>(ServerConfig::PROTOCOL_VERSION)));
        qDebug() << login << "authorized";
        return true;
    }

    if (result == AuthManager::Result::WrongPassword) {
        client->send("wronguser:");
        client->disconnect();
        return true;
    }

    client->send("servererror:");
    client->disconnect();
    qWarning() << "Auth error for" << login;
    return true;
}

bool Server::stateHandleStep(const QString& cmd, Client* client)
{
    if (client->status() != Client::Status::InGame)
        return false;

    static const QRegularExpression rx(QStringLiteral(R"(step:(-?\d+):(-?\d+):)"));
    const auto match = rx.match(cmd);
    if (!match.hasMatch())
        return false;

    client->updateActivity();
    const int x = match.captured(1).toInt();
    const int y = match.captured(2).toInt();

    if (auto* session = client->session())
        session->handleMove(client, x, y);

    return true;
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

bool Server::checkProtocolVersion(const QString& version)
{
    bool ok = false;
    const int v = version.toInt(&ok);
    return ok && v == static_cast<int>(ServerConfig::PROTOCOL_VERSION);
}

bool Server::isAlreadyConnected(const QString& login) const
{
    return std::any_of(clients_.begin(), clients_.end(), [&login](const Client* c) {
        return c->login() == login
            && c->status() != Client::Status::Disconnected
            && c->status() != Client::Status::Unauthenticated;
    });
}

std::unique_ptr<AbstractGame> Server::makeGame() const
{
    auto it = gamePrototypes_.find(ServerConfig::DEFAULT_GAME_NAME);
    if (it == gamePrototypes_.end()) return nullptr;
    const auto [w, h] = it->second->defaultBoardSize();
    return it->second->create(w, h);
}

void Server::matchClients()
{
    std::vector<Client*> waiting;
    for (auto* c : clients_) {
        if (c->status() == Client::Status::Authorized)
            waiting.push_back(c);
    }

    for (size_t i = 0; i + 1 < waiting.size(); i += 2) {
        auto game = makeGame();
        if (!game) continue;

        Client* p0 = waiting[i];
        Client* p1 = waiting[i + 1];
        p0->setStatus(Client::Status::InGame);
        p1->setStatus(Client::Status::InGame);

        auto session = std::make_unique<GameSession>(std::move(game), p0, p1, &authManager_);
        session->setOnFinished([this] { matchClients(); });
        session->start();
        sessions_.push_back(std::move(session));

        qDebug() << "Game started:" << p0->login() << "vs" << p1->login();
    }
}

void Server::timerEvent(QTimerEvent* event)
{
    Q_UNUSED(event);

    sessions_.remove_if([](const std::unique_ptr<GameSession>& s) {
        return s->isFinished();
    });

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
            disconnectClient(client);
        }

        ++it;
    }

    matchClients();
}

void Server::onClientDisconnected(Client* client)
{
    qDebug() << client->login() << "disconnected";
    disconnectClient(client);
}

void Server::disconnectClient(Client* client)
{
    GameSession* session = client->session();
    markClientDisconnected(client);
    if (session)
        session->handleDisconnect(client);
}

void Server::markClientDisconnected(Client* client) const
{
    if (!client) return;
    client->setStatus(Client::Status::Disconnected);
    client->setSession(nullptr);
    QObject::disconnect(client, nullptr, this, nullptr);
    if (QTcpSocket* socket = client->socket())
        socket->disconnectFromHost();
}
