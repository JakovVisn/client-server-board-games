#include "mainwindow.h"
#include "connectdialog.h"
#include <QAction>
#include <QCloseEvent>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QSettings>
#include <QStatusBar>
#include <QTime>
#include <QToolBar>

QPlainTextEdit* MainWindow::s_logPanel_ = nullptr;

void MainWindow::messageHandler(QtMsgType type, const QMessageLogContext&, const QString& msg)
{
    QString prefix;
    switch (type) {
    case QtWarningMsg:  prefix = "WARN  "; break;
    case QtCriticalMsg: prefix = "ERROR "; break;
    default: break;
    }
    const QString line = QTime::currentTime().toString("hh:mm:ss") + "  " + prefix + msg;
    if (s_logPanel_)
        s_logPanel_->appendPlainText(line);
    fprintf(stderr, "%s\n", qPrintable(line));
}

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle("VirusWar");
    setMinimumSize(400, 100);

    {
        QSettings s("VirusWar", "Client");
        lastHost_     = s.value("host",      lastHost_).toString();
        lastPort_     = s.value("port",      lastPort_).value<quint16>();
        lastLogin_    = s.value("login",     lastLogin_).toString();
        lastPassword_ = s.value("password",  lastPassword_).toString();
        lastPref_     = s.value("preferred", lastPref_).toString();
    }

    qInstallMessageHandler(messageHandler);
    setupToolbar();
    setupStatusBar();
    setupNetwork();
}

MainWindow::~MainWindow() = default;

void MainWindow::closeEvent(QCloseEvent* event)
{
    qInstallMessageHandler(nullptr);
    s_logPanel_ = nullptr;
    network_->disconnectFromServer();
    event->accept();
}

void MainWindow::setupToolbar()
{
    QToolBar* toolbar = addToolBar("Main");
    toolbar->setMovable(false);

    connectAction_    = toolbar->addAction("Connect");
    disconnectAction_ = toolbar->addAction("Disconnect");
    disconnectAction_->setEnabled(false);

    connect(connectAction_,    &QAction::triggered, this, &MainWindow::onConnectClicked);
    connect(disconnectAction_, &QAction::triggered, this, &MainWindow::onDisconnectClicked);
}

void MainWindow::setupStatusBar()
{
    stateLabel_ = new QLabel("Not connected");
    statusBar()->addWidget(stateLabel_);
}

void MainWindow::setupNetwork()
{
    network_ = new NetworkClient(this);

    connect(network_, &NetworkClient::connected, this, [this] {
        setAppState(AppState::WaitingForMatch);
    });

    connect(network_, &NetworkClient::disconnected, this, [this] {
        setAppState(AppState::Disconnected);
    });

    connect(network_, &NetworkClient::matchFound, this,
            [this](const QString&, bool) {
        setAppState(AppState::InGame);
    });

    connect(network_, &NetworkClient::gameOver, this, [this](bool won) {
        network_->disconnectFromServer();
        QMessageBox::information(this, "Game over", won ? "You win!" : "You lose.");
    });

    connect(network_, &NetworkClient::errorOccurred, this, [this](const QString& msg) {
        const QString text = [&] {
            if (msg == "wronguser:")       return QStringLiteral("Wrong login or password.");
            if (msg == "alreadyconnected:") return QStringLiteral("Already connected from another session.");
            if (msg == "badgame:")         return QStringLiteral("Unknown game or invalid board size.");
            if (msg == "wrongver:")        return QStringLiteral("Protocol version mismatch.");
            if (msg == "servererror:")     return QStringLiteral("Server error.");
            if (msg == "wrongcmd:")        return QStringLiteral("Server rejected the command.");
            return msg;
        }();
        QMessageBox::warning(this, "Error", text);
        setAppState(AppState::Disconnected);
    });
}

void MainWindow::onConnectClicked()
{
    ConnectDialog dlg(this);
    dlg.setHost(lastHost_);
    dlg.setPort(lastPort_);
    dlg.setLogin(lastLogin_);
    dlg.setPassword(lastPassword_);
    dlg.setPreferredOpponent(lastPref_);

    if (dlg.exec() != QDialog::Accepted) return;

    lastHost_     = dlg.host();
    lastPort_     = dlg.port();
    lastLogin_    = dlg.login();
    lastPassword_ = dlg.password();
    lastPref_     = dlg.preferredOpponent();

    {
        QSettings s("VirusWar", "Client");
        s.setValue("host",      lastHost_);
        s.setValue("port",      lastPort_);
        s.setValue("login",     lastLogin_);
        s.setValue("password",  lastPassword_);
        s.setValue("preferred", lastPref_);
    }

    network_->connectToServer(lastHost_, lastPort_, lastLogin_, lastPassword_, lastPref_);
}

void MainWindow::onDisconnectClicked()
{
    network_->disconnectFromServer();
}

void MainWindow::setAppState(AppState state)
{
    appState_ = state;
    connectAction_->setEnabled(state == AppState::Disconnected);
    disconnectAction_->setEnabled(state != AppState::Disconnected);

    switch (state) {
    case AppState::Disconnected:    stateLabel_->setText("Not connected");       break;
    case AppState::WaitingForMatch: stateLabel_->setText("Searching for opponent…"); break;
    case AppState::InGame:          stateLabel_->setText("In game");              break;
    }
}
