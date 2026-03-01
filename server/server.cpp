#include <algorithm>
#include <set>
#include <QRandomGenerator>
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

    // Groups: 1=VERSION 2=LOGIN 3=PASSWORD 4=GAMENAME(opt) 5=WIDTH(opt) 6=HEIGHT(opt) 7=TARGET(opt)
    // TARGET: "login" or "login/N" or "login1/N1,login2/N2"
    static const QRegularExpression rx(
        QString(R"(mbclient:(\d+):([\w]{%1,%2}):([\w]{%3,%4})(?::([\w]+)(?::(\d+):(\d+)(?::([\w/,]+))?)?)?:)")
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
    const QString gameNameParam = match.captured(4);
    int w = match.captured(5).toInt();
    int h = match.captured(6).toInt();
    const QString target   = match.captured(7);

    client->setLogin(login);

    if (!target.isEmpty()) {
        QMap<QString, int> prefs;
        for (const QString& entry : target.split(',')) {
            const QStringList parts = entry.split('/');
            const QString name = parts.at(0).trimmed();
            if (name.isEmpty()) continue;
            int limit = 1;
            if (parts.size() >= 2) {
                bool ok = false;
                const int n = parts.at(1).toInt(&ok);
                if (ok && n > 0) limit = n;
            }
            prefs[name] = limit;
        }
        if (!prefs.isEmpty())
            client->setPreferredOpponents(prefs);
    }

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

        const QString gameName = gameNameParam.isEmpty()
            ? ServerConfig::DEFAULT_GAME_NAME : gameNameParam;
        auto it = gamePrototypes_.find(gameName);
        if (it == gamePrototypes_.end()) {
            client->send("badgame:");
            client->disconnect();
            return true;
        }
        AbstractGame* proto = it->second.get();

        if (w == 0 && h == 0) {
            auto [defW, defH] = proto->defaultBoardSize();
            w = defW;
            h = defH;
        } else if (!proto->isValidBoardSize(w, h)) {
            client->send("badgame:");
            client->disconnect();
            return true;
        }

        client->setGameName(gameName);
        client->setBoardWidth(w);
        client->setBoardHeight(h);
        client->setStatus(Client::Status::Authorized);
        client->send(QString("mbserver:%1:%2:%3:%4:")
            .arg(static_cast<int>(ServerConfig::PROTOCOL_VERSION))
            .arg(gameName)
            .arg(w)
            .arg(h));
        qDebug() << login << "authorized for" << gameName << w << "x" << h;
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

std::unique_ptr<AbstractGame> Server::makeGame(const QString& name, int w, int h) const
{
    auto it = gamePrototypes_.find(name);
    if (it == gamePrototypes_.end()) return nullptr;
    return it->second->create(w, h);
}

bool Server::sameGame(const Client* a, const Client* b)
{
    return a->gameName() == b->gameName()
        && a->boardWidth() == b->boardWidth()
        && a->boardHeight() == b->boardHeight();
}

int Server::countActiveSessions(const Client* a, const Client* b) const
{
    int count = 0;
    for (const auto& session : sessions_)
        if (!session->isFinished() && session->involvesPlayer(a) && session->involvesPlayer(b))
            ++count;
    return count;
}

void Server::startGame(Client* p0, Client* p1)
{
    if (QRandomGenerator::global()->bounded(2))
        std::swap(p0, p1);
    auto game = makeGame(p0->gameName(), p0->boardWidth(), p0->boardHeight());
    if (!game) return;

    p0->setStatus(Client::Status::InGame);
    p1->setStatus(Client::Status::InGame);

    auto session = std::make_unique<GameSession>(std::move(game), p0, p1, &authManager_);
    session->setOnFinished([this] { matchClients(); });
    session->start();
    sessions_.push_back(std::move(session));

    qDebug() << "Game started:" << p0->login() << "vs" << p1->login();
}

void Server::matchClients()
{
    std::vector<Client*> waiting;
    for (auto* c : clients_) {
        if (c->status() == Client::Status::Authorized)
            waiting.push_back(c);
    }

    // Pass 1: preferred matches (with per-pair concurrency limits)
    std::set<Client*> matched;
    for (Client* c : waiting) {
        if (matched.count(c)) continue;
        if (c->preferredOpponents().isEmpty()) continue;

        for (auto it = c->preferredOpponents().constBegin();
             it != c->preferredOpponents().constEnd() && !matched.count(c);
             ++it) {
            const QString& target = it.key();
            const int limit = it.value();

            for (Client* opponent : waiting) {
                if (opponent == c || matched.count(opponent)) continue;
                if (opponent->login() != target) continue;
                if (!sameGame(c, opponent)) continue;
                if (countActiveSessions(c, opponent) >= limit) continue;

                startGame(c, opponent);
                matched.insert(c);
                matched.insert(opponent);
                break;
            }
        }
    }

    // Pass 2: random matching for those with no preference
    for (size_t i = 0; i < waiting.size(); ++i) {
        Client* a = waiting[i];
        if (a->status() != Client::Status::Authorized) continue;
        if (!a->preferredOpponents().isEmpty()) continue;

        for (size_t j = i + 1; j < waiting.size(); ++j) {
            Client* b = waiting[j];
            if (b->status() != Client::Status::Authorized) continue;
            if (!b->preferredOpponents().isEmpty()) continue;
            if (!sameGame(a, b)) continue;

            startGame(a, b);
            break;
        }
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
