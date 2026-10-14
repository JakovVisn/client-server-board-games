#include "mainwindow.h"
#include "connectdialog.h"
#include <QAction>
#include <QCloseEvent>
#include <QFontDatabase>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QSettings>
#include <QSplitter>
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
    setMinimumSize(520, 560);
    resize(560, 600);

    {
        QSettings s("VirusWar", "Client");
        lastHost_     = s.value("host",      lastHost_).toString();
        lastPort_     = s.value("port",      lastPort_).value<quint16>();
        lastLogin_    = s.value("login",     lastLogin_).toString();
        lastPassword_ = s.value("password",  lastPassword_).toString();
        lastPref_     = s.value("preferred", lastPref_).toString();
    }

    gameState_.reset(boardWidth_, boardHeight_);
    boardWidget_ = new BoardWidget(this);
    boardWidget_->setGameState(&gameState_);

    logPanel_ = new QPlainTextEdit;
    logPanel_->setReadOnly(true);
    logPanel_->setMaximumBlockCount(500);
    logPanel_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    logPanel_->setStyleSheet("background:#111118; color:#8888aa; border:none;");

    auto* splitter = new QSplitter(Qt::Vertical, this);
    splitter->addWidget(boardWidget_);
    splitter->addWidget(logPanel_);
    splitter->setStretchFactor(0, 4);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({440, 120});
    setCentralWidget(splitter);

    s_logPanel_ = logPanel_;
    qInstallMessageHandler(messageHandler);

    setupToolbar();
    setupStatusBar();
    setupNetwork();

    connect(boardWidget_, &BoardWidget::cellClicked, this, [this](int x, int y) {
        if (appState_ != AppState::YourTurn) return;
        network_->sendMove(x, y);
        boardWidget_->setInteractive(false);
        appState_ = AppState::WaitingForTurn;
        updateStatus();
    });
}

MainWindow::~MainWindow() = default;

void MainWindow::closeEvent(QCloseEvent* event)
{
    qInstallMessageHandler(nullptr);
    s_logPanel_ = nullptr;
    network_->disconnectFromServer();
    event->accept();
}

// ── Setup ────────────────────────────────────────────────────────────────────

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
    stateLabel_    = new QLabel("Not connected");
    gameInfoLabel_ = new QLabel;

    statusBar()->addWidget(stateLabel_);
    statusBar()->addPermanentWidget(gameInfoLabel_);
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

    connect(network_, &NetworkClient::authSuccess, this,
            [this](const QString&, int w, int h) {
        boardWidth_  = w;
        boardHeight_ = h;
    });

    connect(network_, &NetworkClient::matchFound, this,
            [this](const QString& opponent, bool) {
        opponent_ = opponent;
        opponentTurnActive_ = false;
        gameState_.reset(boardWidth_, boardHeight_);
        boardWidget_->update();
        setAppState(AppState::WaitingForTurn);
    });

    connect(network_, &NetworkClient::yourTurn, this,
            [this](const QString& board, int moves) {
        if (moves == 3) {
            gameState_.clearMyNewCells();
            opponentTurnActive_ = false;
        }
        gameState_.fromString(board);
        movesLeft_ = moves;
        setAppState(AppState::YourTurn);
    });

    connect(network_, &NetworkClient::moveConfirmed, this,
            [this](const QString& type, int x, int y) {
        const bool caught = type == "caught";
        gameState_.setCell(x, y, caught ? Cell::CapturedByMe : Cell::Mine);
        gameState_.setNewCell(x, y, caught ? NewCell::MyCatch : NewCell::MyMove);
        boardWidget_->update();
    });

    connect(network_, &NetworkClient::opponentMoved, this,
            [this](const QString& type, int x, int y) {
        if (!opponentTurnActive_) {
            gameState_.clearOppNewCells();
            opponentTurnActive_ = true;
        }
        const bool caught = type == "caught";
        gameState_.setCell(x, y, caught ? Cell::CapturedByOpp : Cell::Opponent);
        gameState_.setNewCell(x, y, caught ? NewCell::OppCatch : NewCell::OppMove);
        boardWidget_->update();
        updateStatus();
    });

    connect(network_, &NetworkClient::wrongStep, this, [this] {
        stateLabel_->setText("Invalid move — try again");
    });

    connect(network_, &NetworkClient::gameOver, this, [this](bool won) {
        // Disconnecting resets the board; keep the final moves visible until OK
        keepFinalBoard_ = true;
        boardWidget_->setInteractive(false);
        network_->disconnectFromServer();
        QMessageBox::information(this, "Game over", won ? "You win!" : "You lose.");
        keepFinalBoard_ = false;
        gameState_.reset(boardWidth_, boardHeight_);
        boardWidget_->update();
    });

    connect(network_, &NetworkClient::errorOccurred, this, [this](const QString& msg) {
        const QString text = [&] {
            if (msg == "wronguser:")        return QStringLiteral("Wrong login or password.");
            if (msg == "alreadyconnected:") return QStringLiteral("Already connected from another session.");
            if (msg == "badgame:")          return QStringLiteral("Unknown game or invalid board size.");
            if (msg == "wrongver:")         return QStringLiteral("Protocol version mismatch.");
            if (msg == "servererror:")      return QStringLiteral("Server error.");
            if (msg == "wrongcmd:")         return QStringLiteral("Server rejected the command.");
            return msg;
        }();
        QMessageBox::warning(this, "Error", text);
        setAppState(AppState::Disconnected);
    });
}

// ── Actions ──────────────────────────────────────────────────────────────────

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

// ── State & status ───────────────────────────────────────────────────────────

void MainWindow::setAppState(AppState state)
{
    if (state == AppState::Disconnected) {
        if (!keepFinalBoard_) {
            gameState_.reset(boardWidth_, boardHeight_);
            boardWidget_->update();
        }
        opponent_.clear();
        opponentTurnActive_ = false;
    }
    appState_ = state;
    boardWidget_->setInteractive(state == AppState::YourTurn);
    connectAction_->setEnabled(state == AppState::Disconnected);
    disconnectAction_->setEnabled(state != AppState::Disconnected);
    updateStatus();
}

void MainWindow::updateStatus()
{
    switch (appState_) {
    case AppState::Disconnected:
        stateLabel_->setText("Not connected");
        gameInfoLabel_->clear();
        break;
    case AppState::WaitingForMatch:
        stateLabel_->setText("Searching for opponent…");
        gameInfoLabel_->clear();
        break;
    case AppState::WaitingForTurn:
        stateLabel_->setText("Opponent's turn");
        gameInfoLabel_->setText(QString("vs %1").arg(opponent_));
        break;
    case AppState::YourTurn:
        stateLabel_->setText(QString("Your turn  ·  %1 move%2 left")
                                 .arg(movesLeft_)
                                 .arg(movesLeft_ == 1 ? "" : "s"));
        gameInfoLabel_->setText(QString("vs %1").arg(opponent_));
        break;
    }
}
