#pragma once

#include <QObject>
#include <QTcpSocket>

class NetworkClient : public QObject
{
    Q_OBJECT

public:
    explicit NetworkClient(QObject* parent = nullptr);

    void connectToServer(const QString& host, quint16 port,
                         const QString& login, const QString& password,
                         const QString& preferredOpponent = QString());
    void disconnectFromServer();
    void sendMove(int x, int y);

    [[nodiscard]] bool isConnected() const;

signals:
    void connected();
    void disconnected();
    void authSuccess(const QString& gameName, int width, int height);
    void matchFound(const QString& opponent, bool goFirst);
    void yourTurn(const QString& board, int movesRemaining);
    void moveConfirmed(const QString& type, int x, int y);
    void opponentMoved(const QString& type, int x, int y);
    void wrongStep();
    void gameOver(bool won);
    void errorOccurred(const QString& message);

private slots:
    void onConnected();
    void onDisconnected();
    void onReadyRead();
    void onSocketError(QAbstractSocket::SocketError error);

private:
    QTcpSocket* socket_;
    QString login_;
    QString password_;
    QString preferredOpponent_;
    int gameId_ = 0;

    void send(const QString& cmd);
    void parseLine(const QString& line);
};
