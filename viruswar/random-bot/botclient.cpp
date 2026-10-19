#include "botclient.h"
#include "logging.h"
#include "viruswar_field.h"
#include <QCoreApplication>
#include <QList>
#include <QPoint>
#include <QRandomGenerator>
#include <QRegularExpression>

static constexpr int kReconnectDelayMs = 3000;

// A uniformly random legal move, or (-1, -1) if there is none
static QPoint randomMove(const QString& board, int w, int h)
{
    const VirusWarField field = VirusWarField::fromBoard(board.toStdString(), w, h);
    QList<QPoint> moves;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            if (field.isValidMove(x, y))
                moves.append(QPoint(x, y));
    if (moves.isEmpty()) return {-1, -1};
    return moves[QRandomGenerator::global()->bounded(moves.size())];
}

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
    Log::info(QString("connecting login=%1 host=%2 port=%3").arg(login_, host_).arg(port_));
    socket_->connectToHost(host_, port_);
}

void BotClient::send(const QString& cmd)
{
    if (socket_->state() != QAbstractSocket::ConnectedState) return;
    Log::debug(QString("send line=%1").arg(Log::maskPassword(cmd)));
    socket_->write((cmd + '\n').toUtf8());
}

void BotClient::onConnected()
{
    Log::info(QString("connected login=%1").arg(login_));
    // any:any — random moves work on every board, so play whatever size the opponent chose
    QString msg = QString("mbclient:2:%1:%2:viruswar:any:any:").arg(login_, password_);
    if (!preferred_.isEmpty())
        msg += preferred_ + ':';
    send(msg);
}

void BotClient::onDisconnected()
{
    Log::info(QString("disconnected login=%1").arg(login_));
    if (loginRejected_) {
        QCoreApplication::exit(1);
    } else if (shuttingDown_) {
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
            Log::debug(QString("recv line=%1").arg(line));
            parseLine(line);
        }
    }
}

void BotClient::onSocketError(QAbstractSocket::SocketError error)
{
    if (error == QAbstractSocket::RemoteHostClosedError) return;
    Log::warn(QString("socket_error login=%1 error=%2")
                  .arg(login_, Log::quoted(socket_->errorString())));
    if (shuttingDown_)
        QCoreApplication::quit();  // not connected: no games to finish
    else
        scheduleReconnect();
}

void BotClient::parseLine(const QString& line)
{
    if (line == "ping:") { send("pong:"); return; }

    static const QRegularExpression rxLoginError(
        R"(^(wrongver|wronguser|alreadyconnected|badgame|servererror):$)");
    auto m = rxLoginError.match(line);
    if (m.hasMatch()) {
        const QString error = m.captured(1);
        // Only these can clear up: our old connection times out, auth.txt becomes readable
        const bool retry = error == "alreadyconnected" || error == "servererror";
        Log::warn(QString("login_failed login=%1 error=%2 action=%3")
                      .arg(login_, error, retry ? "retry" : "quit"));
        loginRejected_ = !retry;
        return;
    }

    // win:/lose:GAMEID:CAUSE: or abort:GAMEID:
    static const QRegularExpression rxResult(R"(^(win|lose|abort):(\d+):(?:(\w+):)?)");
    m = rxResult.match(line);
    if (m.hasMatch()) {
        const int id = m.captured(2).toInt();
        Log::info(QString("game_over id=%1 result=%2 cause=%3")
                      .arg(id).arg(m.captured(1), m.captured(3).isEmpty() ? "-" : m.captured(3)));
        if (gameSizes_.remove(id)) {
            --activeGames_;
            checkShutdown();
        }
        return;
    }

    static const QRegularExpression rxWrongStep(R"(^wrongstep:(\d+):)");
    m = rxWrongStep.match(line);
    if (m.hasMatch()) {
        Log::warn(QString("wrongstep id=%1 reason=move_logic_error").arg(m.captured(1)));
        return;
    }

    static const QRegularExpression rxMbserver(R"(^mbserver:\d+:\w+:\w+:\w+:)");  // W:H may be any:any
    m = rxMbserver.match(line);
    if (m.hasMatch()) { Log::info(QString("login_ok login=%1").arg(login_)); return; }

    // found:GAMEID:OPPONENT:GAMENAME:W:H:ROLE:[TIME:]
    static const QRegularExpression rxFound(R"(^found:(\d+):(\w+):\w+:(\d+):(\d+):(?:first|second):)");
    m = rxFound.match(line);
    if (m.hasMatch()) {
        const int id = m.captured(1).toInt();
        gameSizes_[id] = {m.captured(3).toInt(), m.captured(4).toInt()};
        ++activeGames_;
        Log::info(QString("game_start id=%1 opponent=%2 board=%3x%4")
                      .arg(id).arg(m.captured(2), m.captured(3), m.captured(4)));
        return;
    }

    // go:BOARD:MOVES:GAMEID:[MYTIME:OPPTIME:]
    static const QRegularExpression rxGo(R"(^go:([0-4]+):(\d+):(\d+):)");
    m = rxGo.match(line);
    if (m.hasMatch()) {
        const int gameId = m.captured(3).toInt();
        if (!gameSizes_.contains(gameId)) {
            Log::warn(QString("unknown_game id=%1").arg(gameId));
            return;
        }
        const auto size = gameSizes_.value(gameId);
        makeMove(m.captured(1), size.first, size.second, gameId);
        return;
    }

    // Ignored: field1:, field2:, ...
}

void BotClient::makeMove(const QString& board, int w, int h, int gameId)
{
    if (board.size() != w * h) {
        Log::warn(QString("bad_board id=%1 length=%2 expected=%3").arg(gameId).arg(board.size()).arg(w * h));
        return;
    }
    const QPoint move = randomMove(board, w, h);
    if (move.x() < 0) {
        Log::warn(QString("no_valid_moves id=%1").arg(gameId));
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
    Log::info(QString("shutdown login=%1 active_games=%2").arg(login_).arg(activeGames_));
    if (socket_->state() != QAbstractSocket::ConnectedState) {
        QCoreApplication::quit();  // disconnected or still connecting: nothing to finish
        return;
    }
    send("nomatch:");  // no new games while the active ones finish
    checkShutdown();
}

void BotClient::checkShutdown()
{
    if (shuttingDown_ && activeGames_ == 0)
        socket_->disconnectFromHost();
}
