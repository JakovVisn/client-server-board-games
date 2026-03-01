#include "botclient.h"
#include "botlogic.h"
#include <QCoreApplication>
#include <QDebug>
#include <QRegularExpression>

static constexpr int kReconnectDelayMs = 3000;

BotClient::BotClient(QString host, quint16 port, QString login, QString password,
                     QString preferred, QObject* parent)
    : QObject(parent)
    , socket_(new QTcpSocket(this))
    , reconnectTimer_(new QTimer(this))
    , host_(std::move(host))
    , port_(port)
    , login_(std::move(login))
    , password_(std::move(password))
    , preferred_(std::move(preferred))
{
    reconnectTimer_->setSingleShot(true);
    reconnectTimer_->setInterval(kReconnectDelayMs);

    connect(socket_, &QTcpSocket::connected,          this, &BotClient::onConnected);
    connect(socket_, &QTcpSocket::disconnected,       this, &BotClient::onDisconnected);
    connect(socket_, &QTcpSocket::readyRead,          this, &BotClient::onReadyRead);
    connect(socket_, &QAbstractSocket::errorOccurred, this, &BotClient::onSocketError);
    connect(reconnectTimer_, &QTimer::timeout,        this, &BotClient::connectToServer);
}

void BotClient::start()
{
    connectToServer();
}

void BotClient::connectToServer()
{
    qDebug() << login_ << "connecting to" << host_ << ":" << port_;
    socket_->connectToHost(host_, port_);
}

void BotClient::send(const QString& cmd)
{
    if (socket_->state() != QAbstractSocket::ConnectedState) return;
    qDebug() << login_ << "→" << cmd;
    socket_->write((cmd + '\n').toUtf8());
}

void BotClient::onConnected()
{
    qDebug() << login_ << "connected";
    if (preferred_.isEmpty())
        send(QString("mbclient:2:%1:%2:").arg(login_, password_));
    else
        send(QString("mbclient:2:%1:%2:viruswar:10:10:%3:").arg(login_, password_, preferred_));
}

void BotClient::onDisconnected()
{
    qDebug() << login_ << "disconnected";
    if (shuttingDown_) {
        QCoreApplication::quit();
    } else {
        gameSizes_.clear();
        activeGames_ = 0;
        scheduleReconnect();
    }
}

void BotClient::onReadyRead()
{
    while (socket_->canReadLine()) {
        const QString line = QString::fromUtf8(socket_->readLine()).trimmed();
        if (!line.isEmpty()) {
            qDebug() << login_ << "←" << line;
            parseLine(line);
        }
    }
}

void BotClient::onSocketError(QAbstractSocket::SocketError error)
{
    if (error == QAbstractSocket::RemoteHostClosedError) return;
    qDebug() << login_ << "socket error:" << socket_->errorString();
    scheduleReconnect();
}

void BotClient::parseLine(const QString& line)
{
    if (line == "ping:") { send("pong:"); return; }

    static const QRegularExpression rxWin(R"(win:(\d+):)");
    auto m = rxWin.match(line);
    if (m.hasMatch()) {
        const int id = m.captured(1).toInt();
        qDebug() << login_ << "won game" << id;
        if (gameSizes_.remove(id)) {
            --activeGames_;
            checkShutdown();
        }
        return;
    }

    static const QRegularExpression rxLose(R"(lose:(\d+):)");
    m = rxLose.match(line);
    if (m.hasMatch()) {
        const int id = m.captured(1).toInt();
        qDebug() << login_ << "lost game" << id;
        if (gameSizes_.remove(id)) {
            --activeGames_;
            checkShutdown();
        }
        return;
    }

    static const QRegularExpression rxAbort(R"(abort:(\d+):)");
    m = rxAbort.match(line);
    if (m.hasMatch()) {
        const int id = m.captured(1).toInt();
        qDebug() << login_ << "game aborted" << id;
        if (gameSizes_.remove(id)) {
            --activeGames_;
            checkShutdown();
        }
        return;
    }

    static const QRegularExpression rxWrongstep(R"(wrongstep:(\d+):)");
    m = rxWrongstep.match(line);
    if (m.hasMatch()) {
        qDebug() << login_ << "wrongstep in game" << m.captured(1) << "— move logic error";
        return;
    }

    static const QRegularExpression rxMbserver(R"(mbserver:\d+:\w+:\d+:\d+:)");
    m = rxMbserver.match(line);
    if (m.hasMatch()) { qDebug() << login_ << "authenticated"; return; }

    // found:GAMEID:OPPONENT:GAMENAME:W:H:role:
    static const QRegularExpression rxFound(R"(found:(\d+):\w+:\w+:(\d+):(\d+):(?:first|second):)");
    m = rxFound.match(line);
    if (m.hasMatch()) {
        gameSizes_[m.captured(1).toInt()] = {m.captured(2).toInt(), m.captured(3).toInt()};
        ++activeGames_;
        return;
    }

    // go:BOARD:MOVES:GAMEID:
    static const QRegularExpression rxGo(R"(go:([0-4]+):(\d+):(\d+):)");
    m = rxGo.match(line);
    if (m.hasMatch()) {
        const int gameId = m.captured(3).toInt();
        const auto size = gameSizes_.value(gameId, {10, 10});
        makeMove(m.captured(1), size.first, size.second, gameId);
        return;
    }

    // field1:, field2:, wrongver:, wronguser:, etc.
}

void BotClient::makeMove(const QString& board, int w, int h, int gameId)
{
    const QPoint move = BotLogic::randomMove(board, w, h);
    if (move.x() < 0) {
        qDebug() << login_ << "no valid moves in game" << gameId;
        return;
    }
    send(QString("step:%1:%2:%3:").arg(move.x()).arg(move.y()).arg(gameId));
}

void BotClient::scheduleReconnect()
{
    if (!reconnectTimer_->isActive())
        reconnectTimer_->start();
}

void BotClient::initiateShutdown()
{
    if (shuttingDown_) return;
    shuttingDown_ = true;
    reconnectTimer_->stop();
    qDebug() << login_ << "graceful shutdown —" << activeGames_ << "game(s) remaining";
    checkShutdown();
}

void BotClient::checkShutdown()
{
    if (shuttingDown_ && activeGames_ == 0)
        socket_->disconnectFromHost();
}
