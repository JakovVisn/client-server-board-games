#pragma once

#include <QObject>
#include <QTcpSocket>

// The TCP connection to the server: sends the client's messages and turns the server's
// lines (parsed by Protocol::parse) into signals for the current game.
class NetworkClient : public QObject
{
    Q_OBJECT

public:
    explicit NetworkClient(QObject* parent = nullptr);

    void connectToServer(const QString& host, quint16 port,
                         const QString& login, const QString& password,
                         const QString& preferredOpponent, int boardWidth, int boardHeight,
                         const QString& timeControl);
    void disconnectFromServer();            // at once; unsent data is dropped
    void disconnectGracefully(int waitMs);  // sends what is queued first (blocks up to waitMs)
    void sendMove(int x, int y);
    void resign();  // give up the current game; the server answers lose:GAMEID:resign:

    [[nodiscard]] bool isConnected() const;

signals:
    void connected();                              // TCP connected, login sent
    void disconnected();
    void loggedIn(int width, int height);          // 0x0: any size
    void loginFailed(const QString& error);        // wrongver:, wronguser:, ...
    void connectionError(const QString& message);  // socket error, except the server closing
    void commandRejected();                        // wrongcmd: (the connection stays open)
    void matchFound(const QString& opponent, bool goFirst, int width, int height,
                    const QString& timeControl);   // timeControl empty if the server sent none
    // myMs / opponentMs are -1 when the game has no time control
    void yourTurn(const QString& board, int movesRemaining, qint64 myMs, qint64 opponentMs);
    void moveConfirmed(const QString& type, int x, int y);
    void opponentMoved(const QString& type, int x, int y);
    void wrongStep();
    // cause: rules, timeout, resign or disconnect; empty from an older server
    void gameOver(bool won, const QString& cause);

private slots:
    void onConnected();
    void onDisconnected();
    void onReadyRead();
    void onSocketError(QAbstractSocket::SocketError error);

private:
    QTcpSocket* socket_ = nullptr;
    QString login_;
    QString password_;
    QString preferredOpponent_;
    int boardWidth_ = 10;
    int boardHeight_ = 10;
    QString timeControl_;

    // The game in progress, from found: (a human plays one game at a time)
    int gameId_ = -1;
    int gameWidth_ = 0;
    int gameHeight_ = 0;

    void send(const QString& cmd);
    void handleLine(const QString& line);
};
