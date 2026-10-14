#pragma once

#include <QMainWindow>
#include "boardwidget.h"
#include "gamestate.h"
#include "networkclient.h"

class QLabel;
class QPlainTextEdit;

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

    // Last connection params
    QString lastHost_     = "127.0.0.1";
    quint16 lastPort_     = 1234;
    QString lastLogin_;
    QString lastPassword_;
    QString lastPref_;

    void setupToolbar();
    void setupStatusBar();
    void setupNetwork();
    void setAppState(AppState state);
    void updateStatus();

    void onConnectClicked();
    void onDisconnectClicked();

    static QPlainTextEdit* s_logPanel_;
    static void messageHandler(QtMsgType type, const QMessageLogContext&, const QString& msg);
};
