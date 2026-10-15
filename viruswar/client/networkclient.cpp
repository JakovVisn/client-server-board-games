#include "networkclient.h"
#include <QDebug>
#include <QRegularExpression>

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
    qDebug() << "Connecting to" << host << "port" << port;
    socket_->connectToHost(host, port);
}

void NetworkClient::disconnectFromServer()
{
    if (socket_->state() != QAbstractSocket::UnconnectedState)
        socket_->abort();
}

void NetworkClient::sendMove(int x, int y)
{
    send(QString("step:%1:%2:%3:").arg(x).arg(y).arg(gameId_));
}

bool NetworkClient::isConnected() const
{
    return socket_->state() == QAbstractSocket::ConnectedState;
}

void NetworkClient::send(const QString& cmd)
{
    if (socket_->state() != QAbstractSocket::ConnectedState) return;
    qDebug() << "→" << cmd;
    socket_->write((cmd + '\n').toUtf8());
}

void NetworkClient::onConnected()
{
    qDebug() << "Connected";
    emit connected();
    // mbclient:VERSION:LOGIN:PASSWORD:GAME:W:H[:TARGET[:TIME]]: — TIME needs the TARGET field,
    // which may be empty
    QString msg = QString("mbclient:2:%1:%2:viruswar:%3:%4:")
        .arg(login_, password_).arg(boardWidth_).arg(boardHeight_);
    if (!timeControl_.isEmpty())
        msg += preferredOpponent_ + ':' + timeControl_ + ':';
    else if (!preferredOpponent_.isEmpty())
        msg += preferredOpponent_ + ':';
    send(msg);
}

void NetworkClient::onDisconnected()
{
    qDebug() << "Disconnected";
    emit disconnected();
}

void NetworkClient::onReadyRead()
{
    while (socket_->canReadLine()) {
        const QString line = QString::fromUtf8(socket_->readLine()).trimmed();
        if (line.isEmpty()) continue;
        qDebug() << "←" << line;
        parseLine(line);
    }
}

void NetworkClient::onSocketError(QAbstractSocket::SocketError error)
{
    if (error == QAbstractSocket::RemoteHostClosedError) return;
    qDebug() << "Socket error:" << socket_->errorString();
    emit errorOccurred(socket_->errorString());
}

void NetworkClient::parseLine(const QString& line)
{
    if (line == "ping:") { send("pong:"); return; }

    if (line == "wrongcmd:") { emit errorOccurred("wrongcmd:"); return; }

    static const QRegularExpression rxWin(R"(win:(\d+):)");
    auto m = rxWin.match(line);
    if (m.hasMatch()) { emit gameOver(true); return; }

    static const QRegularExpression rxLose(R"(lose:(\d+):)");
    m = rxLose.match(line);
    if (m.hasMatch()) { emit gameOver(false); return; }

    static const QRegularExpression rxWrongstep(R"(wrongstep:(\d+):)");
    m = rxWrongstep.match(line);
    if (m.hasMatch()) { emit wrongStep(); return; }

    static const QRegularExpression rxMbserver(R"(mbserver:\d+:(\w+):(\d+):(\d+):)");
    m = rxMbserver.match(line);
    if (m.hasMatch()) {
        emit authSuccess(m.captured(1), m.captured(2).toInt(), m.captured(3).toInt());
        return;
    }

    // found:GAMEID:OPPONENT:GAMENAME:W:H:role:[TIME:]  (GAMEID ignored — humans get it from go:)
    static const QRegularExpression rxFound(
        R"(found:(\d+):(\w+):\w+:(\d+):(\d+):(first|second):(?:([\w+]+):)?)");
    m = rxFound.match(line);
    if (m.hasMatch()) {
        emit matchFound(m.captured(2), m.captured(5) == "first",
                        m.captured(3).toInt(), m.captured(4).toInt(), m.captured(6));
        return;
    }

    // go:BOARD:MOVES:GAMEID:[MYMS:OPPONENTMS:]
    static const QRegularExpression rxGo(R"(go:([0-4]+):(\d+):(\d+):(?:(\d+):(\d+):)?)");
    m = rxGo.match(line);
    if (m.hasMatch()) {
        gameId_ = m.captured(3).toInt();
        const bool timed = m.hasCaptured(4);
        emit yourTurn(m.captured(1), m.captured(2).toInt(),
                      timed ? m.captured(4).toLongLong() : -1,
                      timed ? m.captured(5).toLongLong() : -1);
        return;
    }

    // field2:TYPE:X:Y:GAMEID:
    static const QRegularExpression rxField2(R"(field2:(\w+):(-?\d+):(-?\d+):(\d+):)");
    m = rxField2.match(line);
    if (m.hasMatch()) {
        emit moveConfirmed(m.captured(1), m.captured(2).toInt(), m.captured(3).toInt());
        return;
    }

    // field1:TYPE:X:Y:GAMEID:
    static const QRegularExpression rxField1(R"(field1:(\w+):(-?\d+):(-?\d+):(\d+):)");
    m = rxField1.match(line);
    if (m.hasMatch()) {
        emit opponentMoved(m.captured(1), m.captured(2).toInt(), m.captured(3).toInt());
        return;
    }

    if (line == "wrongver:" || line == "wronguser:" ||
        line == "alreadyconnected:" || line == "badgame:" || line == "servererror:") {
        emit errorOccurred(line);
        return;
    }

    qDebug() << "Unhandled:" << line;
}
