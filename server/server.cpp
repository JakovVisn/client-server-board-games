#include <algorithm>
#include <utility>
#include <QRandomGenerator>
#include <QRegularExpression>
#include "logging.h"
#include "matchmaking.h"
#include "server.h"
#include "server_constants.h"
#include "virus_war_game.h"

Server::Server(const QString& authFile, const QString& statsFile, QObject* parent)
    : QTcpServer(parent), authManager_(authFile, statsFile)
{
    gamePrototypes_[ServerConfig::DEFAULT_GAME_NAME] = std::make_unique<VirusWarGame>();
}

bool Server::start(const QHostAddress& address, quint16 port)
{
    if (!listen(address, port)) {
        Log::error(QString("server_start_failed host=%1 port=%2 error=%3")
                       .arg(address.toString()).arg(port).arg(Log::quoted(errorString())));
        return false;
    }

    startTimer(ServerConfig::SEARCH_INTERVAL);
    Log::info(QString("server_start host=%1 port=%2").arg(address.toString()).arg(port));
    return true;
}

void Server::incomingConnection(qintptr socketDescriptor)
{
    auto* socket = new QTcpSocket(this);
    if (!socket->setSocketDescriptor(socketDescriptor)) {
        Log::warn("connection_failed reason=socket_descriptor");
        socket->deleteLater();
        return;
    }

    auto* client = new Client(socket, this);
    connect(client, &Client::dataReceived, this, [this, client](const QString& data) {
        onClientDataReceived(client, data);
    });
    connect(client, &Client::disconnected, this, &Server::onClientDisconnected);

    clients_.push_back(client);
    Log::info(QString("connection peer=%1:%2")
                  .arg(socket->peerAddress().toString()).arg(socket->peerPort()));
}

void Server::onClientDataReceived(Client* client, const QString& data)
{
    parseData(data, client);
}

void Server::parseData(const QString& cmd, Client* client)
{
    Log::debug(QString("recv login=%1 line=%2")
                   .arg(Log::quoted(client->login()), Log::maskPassword(cmd)));

    if (stateAuthorize(cmd, client))     return;
    if (stateReceiveSteps(cmd, client))  return;
    if (stateReceiveResign(cmd, client)) return;
    if (stateReceivePing(cmd, client))   return;
    if (stateReceivePong(cmd, client))   return;
    if (stateReceiveNoMatch(cmd, client)) return;

    client->send("wrongcmd:");
}

bool Server::stateAuthorize(const QString& cmd, Client* client)
{
    if (client->status() != Client::Status::Unauthenticated)
        return false;

    // Groups: 1=VERSION 2=LOGIN 3=PASSWORD 4=GAMENAME(opt) 5=WIDTH(opt) 6=HEIGHT(opt)
    //         7=TARGET(opt, may be empty) 8=TIME(opt)
    // WIDTH:HEIGHT may be 0:0 (default) or any:any; TARGET: login[/N][,login[/N]...];
    // TIME (time_control.h) needs the TARGET field, which may be empty
    static const QRegularExpression rx(
        QString(R"(^mbclient:(\d+):([\w]{%1,%2}):([\w]{%3,%4})(?::([\w]+)(?::(\d{1,4}|any):(\d{1,4}|any)(?::([\w/,]*)(?::([\w+]+))?)?)?)?:$)")
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
    const bool anyWidth  = match.captured(5) == "any";
    const bool anyHeight = match.captured(6) == "any";
    int w = match.captured(5).toInt();  // 0 for "any"
    int h = match.captured(6).toInt();
    const QString target   = match.captured(7);
    const QString timeParam = match.captured(8);

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
        Log::info(QString("login_failed login=%1 reason=wrongver").arg(login));
        client->send("wrongver:");
        client->closeConnection();
        return true;
    }

    AuthManager::Result result = authManager_.verify(login, password);
    if (result == AuthManager::Result::NotFound)
        result = authManager_.registerUser(login, password);

    if (result == AuthManager::Result::Authorized) {
        if (isAlreadyConnected(login)) {
            Log::info(QString("login_failed login=%1 reason=alreadyconnected").arg(login));
            client->send("alreadyconnected:");
            client->closeConnection();
            return true;
        }

        const QString gameName = gameNameParam.isEmpty()
            ? ServerConfig::DEFAULT_GAME_NAME : gameNameParam;
        auto it = gamePrototypes_.find(gameName);
        if (it == gamePrototypes_.end()) {
            Log::info(QString("login_failed login=%1 reason=badgame").arg(login));
            client->send("badgame:");
            client->closeConnection();
            return true;
        }
        AbstractGame* proto = it->second.get();

        if (anyWidth != anyHeight) {
            Log::info(QString("login_failed login=%1 reason=badgame").arg(login));
            client->send("badgame:");
            client->closeConnection();
            return true;
        }
        if (anyWidth) {
            w = h = 0;  // decided per game by the opponent's size
        } else if (w == 0 && h == 0) {
            auto [defW, defH] = proto->defaultBoardSize();
            w = defW;
            h = defH;
        } else if (!proto->isValidBoardSize(w, h)) {
            Log::info(QString("login_failed login=%1 reason=badgame").arg(login));
            client->send("badgame:");
            client->closeConnection();
            return true;
        }

        std::optional<TimeControl> timeControl;
        if (!timeParam.isEmpty()) {
            timeControl = TimeControl::parse(timeParam);
            if (!timeControl) {
                Log::info(QString("login_failed login=%1 reason=badgame").arg(login));
                client->send("badgame:");
                client->closeConnection();
                return true;
            }
        }

        client->setBot(authManager_.isBot(login));
        if (client->isBot())
            client->setAutoLimit(authManager_.autoConnectionLimit(login));
        client->setGameName(gameName);
        client->setBoardWidth(w);
        client->setBoardHeight(h);
        client->setAnyBoardSize(anyWidth);
        client->setTimeControl(timeControl);
        client->setStatus(Client::Status::Authorized);
        const QString size = anyWidth ? QStringLiteral("any:any") : QString("%1:%2").arg(w).arg(h);
        client->send(QString("mbserver:%1:%2:%3:")
            .arg(static_cast<int>(ServerConfig::PROTOCOL_VERSION))
            .arg(gameName)
            .arg(size));
        Log::info(QString("login login=%1 bot=%2 game=%3 size=%4 time=%5")
                      .arg(login, client->isBot() ? "yes" : "no", gameName, size,
                           timeControl ? timeControl->toString() : QStringLiteral("any")));
        return true;
    }

    if (result == AuthManager::Result::WrongPassword) {
        Log::info(QString("login_failed login=%1 reason=wronguser").arg(login));
        client->send("wronguser:");
        client->closeConnection();
        return true;
    }

    Log::warn(QString("login_failed login=%1 reason=servererror").arg(login));
    client->send("servererror:");
    client->closeConnection();
    return true;
}

bool Server::stateReceiveSteps(const QString& cmd, Client* client)
{
    static const QRegularExpression rx(QStringLiteral(R"(^step:(-?\d{1,6}):(-?\d{1,6}):(\d{1,9}):$)"));
    const auto m = rx.match(cmd);
    if (!m.hasMatch()) return false;

    client->updateActivity();
    const int x = m.captured(1).toInt();
    const int y = m.captured(2).toInt();
    const int sessionId = m.captured(3).toInt();

    auto it = sessionById_.find(sessionId);
    if (it == sessionById_.end()) { client->send("wrongcmd:"); return true; }
    it.value()->handleMove(client, x, y);
    return true;
}

bool Server::stateReceiveResign(const QString& cmd, Client* client)
{
    static const QRegularExpression rx(QStringLiteral(R"(^resign:(\d{1,9}):$)"));
    const auto m = rx.match(cmd);
    if (!m.hasMatch()) return false;

    client->updateActivity();
    auto it = sessionById_.find(m.captured(1).toInt());
    if (it == sessionById_.end()) { client->send("wrongcmd:"); return true; }
    it.value()->handleResign(client);  // ignored for a game the client is not in
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

bool Server::stateReceiveNoMatch(const QString& cmd, Client* client)
{
    // A client shutting down: no new games, the current ones are played to the end
    if (cmd != "nomatch:" || client->status() == Client::Status::Unauthenticated) return false;
    client->updateActivity();
    if (client->isMatching()) {
        client->stopMatching();
        Log::info(QString("no_new_games login=%1").arg(Log::quoted(client->login())));
    }
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

std::unique_ptr<AbstractGame> Server::makeGame(const Client* a, const Client* b) const
{
    auto it = gamePrototypes_.find(a->gameName());
    if (it == gamePrototypes_.end()) return nullptr;
    // The size of whoever chose one; the default when both accept any
    const Client* sized = !a->anyBoardSize() ? a : !b->anyBoardSize() ? b : nullptr;
    if (!sized) {
        auto [w, h] = it->second->defaultBoardSize();
        return it->second->create(w, h);
    }
    return it->second->create(sized->boardWidth(), sized->boardHeight());
}

bool Server::sameGame(const Client* a, const Client* b)
{
    // A client that asked for no time control accepts any
    const bool timeOk = !a->timeControl() || !b->timeControl()
        || *a->timeControl() == *b->timeControl();
    // Likewise for a client that asked for any board size
    const bool sizeOk = a->anyBoardSize() || b->anyBoardSize()
        || (a->boardWidth() == b->boardWidth() && a->boardHeight() == b->boardHeight());
    return a->gameName() == b->gameName() && sizeOk && timeOk;
}

TimeControl Server::timeControlFor(const Client* a, const Client* b) const
{
    if (a->timeControl()) return *a->timeControl();
    if (b->timeControl()) return *b->timeControl();
    return defaultTimeControl_;
}

int Server::countActiveSessions(const Client* a, const Client* b) const
{
    int count = 0;
    for (const auto& session : sessions_)
        if (!session->isFinished() && session->involvesPlayer(a) && session->involvesPlayer(b))
            ++count;
    return count;
}

void Server::startSession(Client* a, Client* b, Client* autoBot)
{
    auto game = makeGame(a, b);
    if (!game) return;
    if (QRandomGenerator::global()->bounded(2))  // who moves first
        std::swap(a, b);

    // Humans and auto-match seekers play one game at a time; other bots stay Authorized
    for (Client* p : {a, b})
        if (!p->isBot() || (autoBot && p != autoBot))
            p->setStatus(Client::Status::InGame);
    if (autoBot)
        autoBot->adjustAutoSessionCount(1);

    auto session = std::make_unique<GameSession>(std::move(game), a, b, &authManager_,
                                                 timeControlFor(a, b));
    const int id = session->id();
    session->setOnFinished([this, autoBot] {
        if (autoBot)
            autoBot->adjustAutoSessionCount(-1);
        matchClients();
    });
    session->start();
    sessionById_[id] = session.get();
    sessions_.push_back(std::move(session));
}

void Server::matchClients()
{
    std::vector<Client*> pool;
    for (auto* c : clients_) {
        if (c->status() == Client::Status::Authorized && c->isMatching() && !c->inPostGameHold())
            pool.push_back(c);
    }

    std::vector<Matchmaking::Candidate> candidates;
    candidates.reserve(pool.size());
    for (const Client* c : pool)
        candidates.push_back({c->login(), c->isBot(), c->autoLimit(), c->autoSessionCount(),
                              c->preferredOpponents()});

    const auto pairings = Matchmaking::findPairings(
        candidates,
        [&pool](int i, int j) { return sameGame(pool[i], pool[j]); },
        [this, &pool](int i, int j) { return countActiveSessions(pool[i], pool[j]); });

    for (const auto& p : pairings)
        startSession(pool[p.a], pool[p.b], p.autoMatch ? pool[p.b] : nullptr);
}

void Server::timerEvent(QTimerEvent* event)
{
    Q_UNUSED(event);

    sessions_.remove_if([this](const std::unique_ptr<GameSession>& s) {
        if (s->isFinished()) {
            sessionById_.remove(s->id());
            return true;
        }
        return false;
    });

    for (auto it = clients_.begin(); it != clients_.end(); ) {
        Client* client = *it;

        if (client->status() == Client::Status::Disconnected) {
            it = clients_.erase(it);
            client->deleteLater();
            continue;
        }

        if (client->status() == Client::Status::Unauthenticated) {
            if (client->millisecondsSinceConnect() >= ServerConfig::LOGIN_TIMEOUT_MS) {
                const QTcpSocket* socket = client->socket();
                Log::info(QString("login_timeout peer=%1:%2")
                              .arg(socket ? socket->peerAddress().toString() : "-")
                              .arg(socket ? socket->peerPort() : 0));
                markClientDisconnected(client);
            }
            ++it;
            continue;
        }

        if (client->millisecondsSinceLastActivity() >= ServerConfig::INACTIVE_MIN_MS
                && !client->isPingPending()) {
            client->send("ping:");
            client->markPingSent();
        }

        if (client->millisecondsSinceLastActivity() >= ServerConfig::INACTIVE_MAX_MS) {
            Log::info(QString("disconnect login=%1 reason=inactive").arg(Log::quoted(client->login())));
            disconnectClient(client);
        }

        ++it;
    }

    matchClients();
}

void Server::onClientDisconnected(Client* client)
{
    Log::info(QString("disconnect login=%1 reason=closed").arg(Log::quoted(client->login())));
    disconnectClient(client);
}

void Server::disconnectClient(Client* client)
{
    if (client->isBot()) {
        markClientDisconnected(client);
        // Collect first: finishing a game re-runs matchmaking, which adds to sessionById_.
        // Finished sessions stay until timerEvent, so the opponent's late step: is ignored
        QList<GameSession*> games;
        for (GameSession* session : std::as_const(sessionById_))
            if (session->involvesPlayer(client))
                games.append(session);
        for (GameSession* game : games)
            game->handleDisconnect(client);
    } else {
        GameSession* session = client->session();
        markClientDisconnected(client);
        if (session)
            session->handleDisconnect(client);
    }
}

void Server::markClientDisconnected(Client* client) const
{
    if (!client) return;
    client->setStatus(Client::Status::Disconnected);
    client->setSession(nullptr);
    disconnect(client, nullptr, this, nullptr);
    if (QTcpSocket* socket = client->socket())
        socket->disconnectFromHost();
}
