#include "client.h"
#include "logging.h"
#include "server_constants.h"

Client::Client(QTcpSocket* socket, QObject* parent)
    : QObject(parent)
    , socket_(socket)
{
    socket_->setParent(this);  // deleted with the Client
    connectedAt_.start();
    connect(socket_, &QTcpSocket::readyRead,    this, &Client::onReadyRead);
    connect(socket_, &QTcpSocket::disconnected, this, &Client::onDisconnected);
}

QTcpSocket* Client::socket() const { return socket_; }

void Client::send(const QString& cmd)
{
    if (socket_->state() != QAbstractSocket::ConnectedState) return;
    Log::debug(QString("send login=%1 line=%2").arg(Log::quoted(login_), cmd));
    socket_->write((cmd + '\n').toUtf8());
}

void Client::closeConnection()
{
    closing_ = true;
    socket_->disconnectFromHost();
}

void Client::startPostGameHold()
{
    if (!isBot_) postGameHold_.start();
}

bool Client::inPostGameHold() const
{
    return postGameHold_.isValid() && postGameHold_.elapsed() < ServerConfig::POST_GAME_HOLD_MS;
}

void Client::stopMatching() { matching_ = false; }
bool Client::isMatching() const { return matching_; }

qint64 Client::millisecondsSinceConnect() const { return connectedAt_.elapsed(); }

Client::Status Client::status() const { return status_; }
void Client::setStatus(Status status) { status_ = status; }

QString Client::login() const { return login_; }
void Client::setLogin(const QString& login) { login_ = login; }

void Client::updateActivity()
{
    lastActivity_ = QDateTime::currentDateTime();
    pingPending_ = false;
}

qint64 Client::millisecondsSinceLastActivity() const
{
    return lastActivity_.msecsTo(QDateTime::currentDateTime());
}

GameSession* Client::session() const { return session_; }
void Client::setSession(GameSession* session) { session_ = session; }

QString Client::gameName() const { return gameName_; }
int Client::boardWidth() const { return boardWidth_; }
int Client::boardHeight() const { return boardHeight_; }
void Client::setGameName(const QString& name) { gameName_ = name; }
void Client::setBoardWidth(int w) { boardWidth_ = w; }
void Client::setBoardHeight(int h) { boardHeight_ = h; }

bool Client::anyBoardSize() const { return anyBoardSize_; }
void Client::setAnyBoardSize(bool any) { anyBoardSize_ = any; }

std::optional<TimeControl> Client::timeControl() const { return timeControl_; }
void Client::setTimeControl(const std::optional<TimeControl>& tc) { timeControl_ = tc; }

const QMap<QString, int>& Client::preferredOpponents() const { return preferredOpponents_; }
void Client::setPreferredOpponents(const QMap<QString, int>& prefs) { preferredOpponents_ = prefs; }

bool Client::isBot() const { return isBot_; }
void Client::setBot(bool bot) { isBot_ = bot; }

int Client::autoLimit() const { return autoLimit_; }
void Client::setAutoLimit(int limit) { autoLimit_ = limit; }
int Client::autoSessionCount() const { return autoSessionCount_; }
void Client::adjustAutoSessionCount(int delta) { autoSessionCount_ += delta; }

bool Client::isPingPending() const { return pingPending_; }
void Client::markPingSent() { pingPending_ = true; }

void Client::onReadyRead()
{
    while (!closing_ && socket_->canReadLine()) {
        const QString line = QString::fromUtf8(socket_->readLine()).trimmed();
        if (!line.isEmpty())
            emit dataReceived(line);
    }
    if (!closing_ && socket_->bytesAvailable() > ServerConfig::MAX_LINE_BYTES) {
        Log::warn(QString("line_too_long login=%1 bytes=%2")
                      .arg(Log::quoted(login_)).arg(socket_->bytesAvailable()));
        closeConnection();
        socket_->abort();  // nothing useful left to flush
    }
}

void Client::onDisconnected()
{
    status_ = Status::Disconnected;
    emit disconnected(this);
}
