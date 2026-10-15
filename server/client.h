#pragma once

#include <optional>
#include <QDateTime>
#include <QMap>
#include <QObject>
#include <QString>
#include <QTcpSocket>
#include "time_control.h"

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

    [[nodiscard]] QString gameName() const;
    [[nodiscard]] int boardWidth() const;
    [[nodiscard]] int boardHeight() const;
    void setGameName(const QString& name);
    void setBoardWidth(int w);
    void setBoardHeight(int h);

    // The time control asked for in mbclient:, or none (accepts any)
    [[nodiscard]] std::optional<TimeControl> timeControl() const;
    void setTimeControl(const std::optional<TimeControl>& tc);

    [[nodiscard]] QMap<QString, int> preferredOpponents() const;
    void setPreferredOpponents(const QMap<QString, int>& prefs);

    [[nodiscard]] bool isBot() const;
    void setBot(bool bot);

    [[nodiscard]] int autoLimit() const;
    void setAutoLimit(int limit);
    [[nodiscard]] int autoSessionCount() const;
    void adjustAutoSessionCount(int delta);

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
    QString gameName_;
    int boardWidth_ = 0;
    int boardHeight_ = 0;
    std::optional<TimeControl> timeControl_;
    QMap<QString, int> preferredOpponents_;
    bool isBot_ = false;
    int autoLimit_ = 0;
    int autoSessionCount_ = 0;
    QDateTime lastActivity_ = QDateTime::currentDateTime();
    bool pingPending_ = false;
    GameSession* session_ = nullptr;
};
