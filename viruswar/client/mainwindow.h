#pragma once

#include <QMainWindow>
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
    QLabel* stateLabel_;
    QAction* connectAction_;
    QAction* disconnectAction_;

    NetworkClient* network_;

    enum class AppState { Disconnected, WaitingForMatch, InGame };
    AppState appState_ = AppState::Disconnected;

    QString lastHost_     = "127.0.0.1";
    quint16 lastPort_     = 1234;
    QString lastLogin_;
    QString lastPassword_;
    QString lastPref_;

    void setupToolbar();
    void setupStatusBar();
    void setupNetwork();
    void setAppState(AppState state);

    void onConnectClicked();
    void onDisconnectClicked();

    static QPlainTextEdit* s_logPanel_;
    static void messageHandler(QtMsgType type, const QMessageLogContext&, const QString& msg);
};
