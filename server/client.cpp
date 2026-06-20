#include <QDebug>
#include "client.h"

Client::Client(QTcpSocket* socket, QObject* parent)
    : QObject(parent)
    , socket_(socket)
{
    connect(socket_, &QTcpSocket::readyRead,    this, &Client::onReadyRead);
    connect(socket_, &QTcpSocket::disconnected, this, &Client::onDisconnected);
}

QTcpSocket* Client::socket() const { return socket_; }

void Client::send(const QString& cmd)
{
    if (socket_->state() != QAbstractSocket::ConnectedState) return;
    qDebug() << login_ << "→" << cmd;
    socket_->write((cmd + '\n').toUtf8());
}

void Client::disconnect()
{
    socket_->disconnectFromHost();
}

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

QMap<QString, int> Client::preferredOpponents() const { return preferredOpponents_; }
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
    while (socket_->canReadLine()) {
        const QString line = QString::fromUtf8(socket_->readLine()).trimmed();
        if (!line.isEmpty())
            emit dataReceived(line);
    }
}

void Client::onDisconnected()
{
    status_ = Status::Disconnected;
    emit disconnected(this);
}
