#pragma once

#include <QDateTime>
#include <QObject>
#include <QString>
#include <QTcpSocket>

class GameSession;

class Client : public QObject
{
    Q_OBJECT

public:
    enum class Status {
        Unauthenticated,
        Authorized,
        InGame,
        Disconnected,
    };

    explicit Client(QTcpSocket* socket, QObject* parent = nullptr);

    [[nodiscard]] QTcpSocket* socket() const;
    void send(const QString& cmd);
    void disconnect();

    [[nodiscard]] Status status() const;
    void setStatus(Status status);

    [[nodiscard]] QString login() const;
    void setLogin(const QString& login);

    [[nodiscard]] GameSession* session() const;
    void setSession(GameSession* session);

    void updateActivity();
    [[nodiscard]] qint64 millisecondsSinceLastActivity() const;

    [[nodiscard]] bool isPingPending() const;
    void markPingSent();

signals:
    void dataReceived(const QString& data);
    void disconnected(Client* client);

private slots:
    void onReadyRead();
    void onDisconnected();

private:
    QTcpSocket* socket_;
    QString login_;
    Status status_ = Status::Unauthenticated;
    QDateTime lastActivity_ = QDateTime::currentDateTime();
    bool pingPending_ = false;
    GameSession* session_ = nullptr;
};
