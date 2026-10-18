#pragma once

#include <QMap>
#include <QTcpSocket>
#include <QTimer>

class BotClient : public QObject
{
    Q_OBJECT

public:
    BotClient(QString host, quint16 port, QString login, QString password,
              QString preferred = QString(), QObject* parent = nullptr);
    void start();
    void initiateShutdown();

private:
    void connectToServer();
    void send(const QString& cmd);
    void parseLine(const QString& line);
    void makeMove(const QString& board, int w, int h, int gameId);
    void scheduleReconnect();
    void checkShutdown();

    void onConnected();
    void onDisconnected();
    void onReadyRead();
    void onSocketError(QAbstractSocket::SocketError error);

    QTcpSocket* socket_ = nullptr;
    QTimer*     reconnectTimer_ = nullptr;
    QString     host_;
    quint16     port_ = 0;
    QString     login_;
    QString     password_;
    QString     preferred_;
    QMap<int, QPair<int,int>> gameSizes_;
    int  activeGames_   = 0;
    bool shuttingDown_  = false;
};
