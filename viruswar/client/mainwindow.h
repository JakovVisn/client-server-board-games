#pragma once

#include <QElapsedTimer>
#include <QMainWindow>
#include "boardwidget.h"
#include "gamestate.h"
#include "networkclient.h"

class QLabel;
class QPlainTextEdit;
class QTimer;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    // UI
    BoardWidget* boardWidget_;
    QPlainTextEdit* logPanel_;
    QLabel* stateLabel_;
    QLabel* gameInfoLabel_;
    QLabel* clockLabel_;
    QTimer* clockTimer_;
    QAction* connectAction_;
    QAction* disconnectAction_;

    // Core
    GameState gameState_;
    NetworkClient* network_;

    // Session state
    enum class AppState { Disconnected, WaitingForMatch, WaitingForTurn, YourTurn };
    AppState appState_ = AppState::Disconnected;
    int boardWidth_  = 10;
    int boardHeight_ = 10;
    QString opponent_;
    int movesLeft_ = 0;
    bool opponentTurnActive_ = false;
    bool keepFinalBoard_ = false;  // game over: board stays until the result dialog is closed

    // Clocks, resynced from every go:; between them the running one is counted down locally
    enum class Clock { Stopped, Mine, Opponent };
    Clock runningClock_ = Clock::Stopped;
    qint64 myMs_ = -1;             // -1: the game has no time control
    qint64 opponentMs_ = -1;
    bool turnLimit_ = false;       // turnN: a fresh limit each turn instead of a game clock
    qint64 turnLimitMs_ = 0;
    qint64 incrementMs_ = 0;       // gameN+I: added after each completed turn
    QElapsedTimer clockSince_;

    // Last connection params
    QString lastHost_     = "127.0.0.1";
    quint16 lastPort_     = 1234;
    QString lastLogin_;
    QString lastPassword_;
    QString lastPref_;
    int lastWidth_  = 10;
    int lastHeight_ = 10;
    QString lastTimeControl_;

    void setupToolbar();
    void setupStatusBar();
    void setupNetwork();
    void setAppState(AppState state);
    void updateStatus();
    [[nodiscard]] bool inGame() const;
    [[nodiscard]] bool confirmLeavingGame();
    void showGameOver(bool won, const QString& cause);
    void startClocks(const QString& timeControl, bool goFirst);
    void switchClock(Clock clock);
    void updateClock();

    void onConnectClicked();
    void onDisconnectClicked();

    static QPlainTextEdit* s_logPanel_;
    static void messageHandler(QtMsgType type, const QMessageLogContext&, const QString& msg);
};
