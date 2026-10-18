#include "mainwindow.h"
#include "connectdialog.h"
#include <QAction>
#include <QCloseEvent>
#include <QFontDatabase>
#include <QHash>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QSettings>
#include <QSplitter>
#include <QStatusBar>
#include <QTime>
#include <QTimer>
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
        lastWidth_    = s.value("width",     lastWidth_).toInt();
        lastHeight_   = s.value("height",    lastHeight_).toInt();
        lastTimeControl_ = s.value("timeControl", lastTimeControl_).toString();
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
    if (inGame()) {
        if (!confirmLeavingGame()) {
            event->ignore();
            return;
        }
        if (inGame())  // the game may have ended while the question was open
            network_->resign();
    }
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
    clockLabel_    = new QLabel;
    clockLabel_->setTextFormat(Qt::RichText);

    statusBar()->addWidget(stateLabel_);
    statusBar()->addPermanentWidget(clockLabel_);
    statusBar()->addPermanentWidget(gameInfoLabel_);

    clockTimer_ = new QTimer(this);
    clockTimer_->setInterval(200);
    connect(clockTimer_, &QTimer::timeout, this, &MainWindow::updateClock);
    clockTimer_->start();
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
            [this](const QString& opponent, bool goFirst, int w, int h, const QString& timeControl) {
        opponent_ = opponent;
        opponentTurnActive_ = false;
        boardWidth_  = w;
        boardHeight_ = h;
        gameState_.reset(boardWidth_, boardHeight_);
        boardWidget_->update();
        startClocks(timeControl, goFirst);
        setAppState(AppState::WaitingForTurn);
    });

    connect(network_, &NetworkClient::yourTurn, this,
            [this](const QString& board, int moves, qint64 myMs, qint64 opponentMs) {
        if (moves == 3) {
            gameState_.clearMyNewCells();
            opponentTurnActive_ = false;
        }
        gameState_.fromString(board);
        movesLeft_ = moves;
        if (myMs >= 0) {
            myMs_ = myMs;
            opponentMs_ = opponentMs;
            clockSince_.restart();
            runningClock_ = Clock::Mine;
        }
        setAppState(AppState::YourTurn);
    });

    connect(network_, &NetworkClient::moveConfirmed, this,
            [this](const QString& type, int x, int y) {
        const bool caught = type == "caught";
        gameState_.setCell(x, y, caught ? Cell::CapturedByMe : Cell::Mine);
        gameState_.setNewCell(x, y, caught ? NewCell::MyCatch : NewCell::MyMove);
        boardWidget_->update();
        if (movesLeft_ == 1)  // that was the last sub-move of my turn
            switchClock(Clock::Opponent);
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

    connect(network_, &NetworkClient::gameOver, this, &MainWindow::showGameOver);

    connect(network_, &NetworkClient::errorOccurred, this, [this](const QString& msg) {
        const QString text = [&] {
            if (msg == "wronguser:")        return QStringLiteral("Wrong login or password.");
            if (msg == "alreadyconnected:") return QStringLiteral("Already connected from another session.");
            if (msg == "badgame:")          return QStringLiteral("Unknown game, invalid board size or time control.");
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
    dlg.setBoardSize(lastWidth_, lastHeight_);
    dlg.setTimeControl(lastTimeControl_);

    if (dlg.exec() != QDialog::Accepted) return;

    lastHost_     = dlg.host();
    lastPort_     = dlg.port();
    lastLogin_    = dlg.login();
    lastPassword_ = dlg.password();
    lastPref_     = dlg.preferredOpponent();
    lastWidth_    = dlg.boardWidth();
    lastHeight_   = dlg.boardHeight();
    lastTimeControl_ = dlg.timeControl();

    {
        QSettings s("VirusWar", "Client");
        s.setValue("host",      lastHost_);
        s.setValue("port",      lastPort_);
        s.setValue("login",     lastLogin_);
        s.setValue("password",  lastPassword_);
        s.setValue("preferred", lastPref_);
        s.setValue("width",     lastWidth_);
        s.setValue("height",    lastHeight_);
        s.setValue("timeControl", lastTimeControl_);
    }

    network_->connectToServer(lastHost_, lastPort_, lastLogin_, lastPassword_, lastPref_,
                              lastWidth_, lastHeight_, lastTimeControl_);
}

void MainWindow::onDisconnectClicked()
{
    if (inGame()) {
        // Resign instead of just dropping the connection: the server answers
        // lose:...:resign:, and showGameOver() reports it and disconnects
        if (confirmLeavingGame() && inGame())  // it may have ended while the question was open
            network_->resign();
        return;
    }
    network_->disconnectFromServer();
}

// ── Game over ────────────────────────────────────────────────────────────────

bool MainWindow::inGame() const
{
    return appState_ == AppState::WaitingForTurn || appState_ == AppState::YourTurn;
}

bool MainWindow::confirmLeavingGame()
{
    return QMessageBox::question(this, "Leave the game?",
                                 "Leaving now counts as a loss.",
                                 QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
           == QMessageBox::Yes;
}

void MainWindow::showGameOver(bool won, const QString& cause)
{
    // The one place that explains a result; cause is the server's CAUSE field
    static const QHash<QString, QString> winReasons = {
        {"rules",      "Your opponent can't make 3 moves."},
        {"timeout",    "Your opponent ran out of time."},
        {"resign",     "Your opponent resigned."},
        {"disconnect", "Your opponent left the game."},
    };
    static const QHash<QString, QString> loseReasons = {
        {"rules",   "You can't make 3 moves."},
        {"timeout", "You ran out of time."},
        {"resign",  "You resigned."},
    };

    // Disconnecting resets the board; keep the final moves visible until OK
    keepFinalBoard_ = true;
    switchClock(Clock::Stopped);
    boardWidget_->setInteractive(false);
    network_->disconnectFromServer();

    QMessageBox box(QMessageBox::Information, "Game over", won ? "You win!" : "You lose.",
                    QMessageBox::Ok, this);
    box.setInformativeText((won ? winReasons : loseReasons).value(cause));
    box.exec();

    keepFinalBoard_ = false;
    gameState_.reset(boardWidth_, boardHeight_);
    boardWidget_->update();
    myMs_ = opponentMs_ = -1;
    updateClock();
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
        if (!keepFinalBoard_) {
            runningClock_ = Clock::Stopped;
            myMs_ = opponentMs_ = -1;
            updateClock();
        }
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

// ── Clocks ───────────────────────────────────────────────────────────────────

void MainWindow::startClocks(const QString& timeControl, bool goFirst)
{
    // turnN, gameN or gameN+I in seconds (docs/PROTOCOL.md#time-control); none or empty: no clock
    static const QRegularExpression rx(R"(^(turn|game)(\d+)(?:\+(\d+))?$)");
    const auto m = rx.match(timeControl);
    if (!m.hasMatch()) {
        runningClock_ = Clock::Stopped;
        myMs_ = opponentMs_ = -1;
        updateClock();
        return;
    }
    turnLimit_   = m.captured(1) == "turn";
    turnLimitMs_ = m.captured(2).toLongLong() * 1000;
    incrementMs_ = m.captured(3).toLongLong() * 1000;
    myMs_ = opponentMs_ = turnLimitMs_;  // the starting clock in both modes
    clockSince_.start();
    runningClock_ = goFirst ? Clock::Mine : Clock::Opponent;
    updateClock();
}

void MainWindow::switchClock(Clock clock)
{
    if (myMs_ < 0 || runningClock_ == clock) return;
    const qint64 spent = clockSince_.elapsed();
    if (runningClock_ == Clock::Mine) {
        myMs_ = turnLimit_ ? turnLimitMs_ : qMax<qint64>(0, myMs_ - spent);
        if (clock == Clock::Opponent && !turnLimit_)
            myMs_ += incrementMs_;  // my turn is complete
    } else if (runningClock_ == Clock::Opponent) {
        opponentMs_ = turnLimit_ ? turnLimitMs_ : qMax<qint64>(0, opponentMs_ - spent);
    }
    runningClock_ = clock;
    clockSince_.restart();
    updateClock();
}

void MainWindow::updateClock()
{
    if (myMs_ < 0) {
        clockLabel_->clear();
        return;
    }
    const qint64 spent = clockSince_.isValid() ? clockSince_.elapsed() : 0;
    const qint64 mine = runningClock_ == Clock::Mine ? qMax<qint64>(0, myMs_ - spent) : myMs_;
    const qint64 theirs = runningClock_ == Clock::Opponent
        ? qMax<qint64>(0, opponentMs_ - spent) : opponentMs_;

    auto format = [](qint64 ms, bool running) {
        const qint64 s = (ms + 999) / 1000;  // round up: 0:00 only when the time is really out
        QString text = QString("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QChar('0'));
        if (running && ms < 10000) text = "<span style='color:#e05050'>" + text + "</span>";
        return running ? "<b>" + text + "</b>" : text;
    };

    if (turnLimit_) {
        // Only the side to move has a running limit
        if (runningClock_ == Clock::Mine)
            clockLabel_->setText("Your time " + format(mine, true));
        else if (runningClock_ == Clock::Opponent)
            clockLabel_->setText("Opponent's time " + format(theirs, true));
        else
            clockLabel_->clear();
    } else {
        clockLabel_->setText("You " + format(mine, runningClock_ == Clock::Mine)
                             + "  ·  Opponent " + format(theirs, runningClock_ == Clock::Opponent));
    }
}
