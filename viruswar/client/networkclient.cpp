#include "networkclient.h"
#include "logging.h"
#include "protocol.h"

NetworkClient::NetworkClient(QObject* parent)
    : QObject(parent), socket_(new QTcpSocket(this))
{
    connect(socket_, &QTcpSocket::connected,    this, &NetworkClient::onConnected);
    connect(socket_, &QTcpSocket::disconnected, this, &NetworkClient::onDisconnected);
    connect(socket_, &QTcpSocket::readyRead,    this, &NetworkClient::onReadyRead);
    connect(socket_, &QAbstractSocket::errorOccurred, this, &NetworkClient::onSocketError);
}

void NetworkClient::connectToServer(const QString& host, quint16 port,
                                    const QString& login, const QString& password,
                                    const QString& preferredOpponent, int boardWidth,
                                    int boardHeight, const QString& timeControl)
{
    login_ = login;
    password_ = password;
    preferredOpponent_ = preferredOpponent;
    boardWidth_ = boardWidth;
    boardHeight_ = boardHeight;
    timeControl_ = timeControl;
    gameId_ = -1;
    Log::info(QString("connecting host=%1 port=%2 login=%3").arg(host).arg(port).arg(login));
    socket_->connectToHost(host, port);
}

void NetworkClient::disconnectFromServer()
{
    if (socket_->state() != QAbstractSocket::UnconnectedState)
        socket_->abort();
}

void NetworkClient::disconnectGracefully(int waitMs)
{
    if (socket_->state() == QAbstractSocket::UnconnectedState)
        return;
    socket_->disconnectFromHost();
    if (socket_->state() != QAbstractSocket::UnconnectedState && !socket_->waitForDisconnected(waitMs))
        socket_->abort();
}

void NetworkClient::sendMove(int x, int y)
{
    send(QString("step:%1:%2:%3:").arg(x).arg(y).arg(gameId_));
}

void NetworkClient::resign()
{
    if (gameId_ >= 0)
        send(QString("resign:%1:").arg(gameId_));
}

bool NetworkClient::isConnected() const
{
    return socket_->state() == QAbstractSocket::ConnectedState;
}

void NetworkClient::send(const QString& cmd)
{
    if (socket_->state() != QAbstractSocket::ConnectedState) return;
    Log::debug(QString("send line=%1").arg(Log::maskPassword(cmd)));
    socket_->write((cmd + '\n').toUtf8());
}

void NetworkClient::onConnected()
{
    Log::info("connected");
    emit connected();
    send(Protocol::loginLine(login_, password_, boardWidth_, boardHeight_,
                             preferredOpponent_, timeControl_));
}

void NetworkClient::onDisconnected()
{
    Log::info("disconnected");
    gameId_ = -1;
    emit disconnected();
}

void NetworkClient::onReadyRead()
{
    while (socket_->canReadLine()) {
        const QString line = QString::fromUtf8(socket_->readLine()).trimmed();
        if (line.isEmpty()) continue;
        Log::debug(QString("recv line=%1").arg(line));
        handleLine(line);
    }
}

void NetworkClient::onSocketError(QAbstractSocket::SocketError error)
{
    if (error == QAbstractSocket::RemoteHostClosedError) return;  // reported as disconnected()
    Log::warn(QString("socket_error error=%1").arg(Log::quoted(socket_->errorString())));
    emit connectionError(socket_->errorString());
}

void NetworkClient::handleLine(const QString& line)
{
    using Type = Protocol::ServerMessage::Type;
    const Protocol::ServerMessage msg = Protocol::parse(line);

    // In-game messages for any other game are stale (e.g. the game just ended)
    const bool inGameMessage = msg.type == Type::Go || msg.type == Type::OwnMove ||
                               msg.type == Type::OpponentMove || msg.type == Type::WrongStep ||
                               msg.type == Type::Result;
    if (inGameMessage && msg.gameId != gameId_) {
        Log::debug(QString("ignored line=%1 reason=other_game current=%2").arg(line).arg(gameId_));
        return;
    }

    switch (msg.type) {
    case Type::Ping:
        send("pong:");
        break;
    case Type::CommandError:
        emit commandRejected();
        break;
    case Type::LoginError:
        emit loginFailed(msg.text);
        break;
    case Type::LoginOk:
        emit loggedIn(msg.width, msg.height);
        break;
    case Type::Found:
        gameId_ = msg.gameId;
        gameWidth_ = msg.width;
        gameHeight_ = msg.height;
        emit matchFound(msg.text, msg.first, msg.width, msg.height, msg.timeControl);
        break;
    case Type::Go:
        if (msg.board.size() != gameWidth_ * gameHeight_) {
            Log::warn(QString("bad_board length=%1 expected=%2")
                          .arg(msg.board.size()).arg(gameWidth_ * gameHeight_));
            break;
        }
        emit yourTurn(msg.board, msg.movesRemaining, msg.myMs, msg.opponentMs);
        break;
    case Type::OwnMove:
        emit moveConfirmed(msg.text, msg.x, msg.y);
        break;
    case Type::OpponentMove:
        emit opponentMoved(msg.text, msg.x, msg.y);
        break;
    case Type::WrongStep:
        emit wrongStep();
        break;
    case Type::Result:
        emit gameOver(msg.won, msg.text);
        break;
    case Type::Unknown:
        Log::warn(QString("unhandled_message line=%1").arg(line));
        break;
    }
}
