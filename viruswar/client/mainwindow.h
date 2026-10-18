#pragma once

#include <QElapsedTimer>
#include <QMainWindow>
#include <QPointer>
#include "boardwidget.h"
#include "gameclock.h"
#include "gamestate.h"
#include "networkclient.h"

class QLabel;
class QMessageBox;
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
    BoardWidget* boardWidget_ = nullptr;
    QPlainTextEdit* logPanel_ = nullptr;
    QLabel* stateLabel_ = nullptr;
    QLabel* gameInfoLabel_ = nullptr;
    QLabel* clockLabel_ = nullptr;
    QTimer* clockTimer_ = nullptr;
    QAction* connectAction_ = nullptr;
    QAction* disconnectAction_ = nullptr;
    QPointer<QMessageBox> leaveQuestion_;  // open "Leave the game?" question, if any

    GameState gameState_;
    NetworkClient* network_ = nullptr;
    GameClock clock_;
    QElapsedTimer monotonic_;  // time base for clock_

    enum class AppState { Disconnected, Connecting, WaitingForMatch, WaitingForTurn, YourTurn };
    AppState appState_ = AppState::Disconnected;
    int boardWidth_  = 10;
    int boardHeight_ = 10;
    QString opponent_;
    int movesLeft_ = 0;
    bool opponentTurnActive_ = false;
    bool invalidMove_ = false;     // last sub-move was rejected; shown until the next one
    bool resultShown_ = false;     // the game is over; its result dialog is (being) shown
    bool keepFinalBoard_ = false;  // game over: board stays until the result dialog is closed

    // Last connection settings
    struct ConnectionSettings
    {
        QString host = "127.0.0.1";
        quint16 port = 1234;
        QString login;
        QString password;
        bool rememberPassword = false;
        QString preferred;
        int width  = 10;
        int height = 10;
        QString timeControl;

        void load();
        void save() const;
    } settings_;

    void setupToolbar();
    void setupStatusBar();
    void setupNetwork();
    void setAppState(AppState state);
    void updateStatus();
    [[nodiscard]] qint64 now() const { return monotonic_.elapsed(); }
    [[nodiscard]] bool inGame() const;
    [[nodiscard]] bool confirmLeavingGame();
    void connectWithSettings();
    void showGameOver(bool won, const QString& cause);
    void showError(const QString& text);
    void updateClock();

    void onConnectClicked();
    void onDisconnectClicked();
};
