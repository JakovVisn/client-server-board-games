#include "mainwindow.h"
#include "connectdialog.h"
#include "logging.h"
#include <QAction>
#include <QHash>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>  // needed to compare clickedButton() with a QPushButton*
#include <QSettings>
#include <QSplitter>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>

namespace {
constexpr int kResignWaitMs = 500;  // closing the window: time to deliver resign:
}

// ── Settings ─────────────────────────────────────────────────────────────────

void MainWindow::ConnectionSettings::load()
{
    const QSettings s("VirusWar", "Client");
    host             = s.value("host", host).toString();
    port             = s.value("port", port).value<quint16>();
    login            = s.value("login", login).toString();
    password         = s.value("password", password).toString();
    rememberPassword = s.value("rememberPassword", !password.isEmpty()).toBool();
    preferred        = s.value("preferred", preferred).toString();
    width            = s.value("width", width).toInt();
    height           = s.value("height", height).toInt();
    timeControl      = s.value("timeControl", timeControl).toString();
}

void MainWindow::ConnectionSettings::save() const
{
    QSettings s("VirusWar", "Client");
    s.setValue("host", host);
    s.setValue("port", port);
    s.setValue("login", login);
    if (rememberPassword)
        s.setValue("password", password);
    else
        s.remove("password");
    s.setValue("rememberPassword", rememberPassword);
    s.setValue("preferred", preferred);
    s.setValue("width", width);
    s.setValue("height", height);
    s.setValue("timeControl", timeControl);
}

// ── Construction ─────────────────────────────────────────────────────────────

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle("VirusWar");
    setMinimumSize(520, 560);
    resize(560, 600);
    monotonic_.start();
    settings_.load();

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

    // Every log line also goes to the panel; queued, because logging may happen on any thread
    Log::setSink([panel = QPointer<QPlainTextEdit>(logPanel_)](const QString& line) {
        QMetaObject::invokeMethod(panel, [panel, line] {
            if (panel) panel->appendPlainText(line);
        }, Qt::QueuedConnection);
    });

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

MainWindow::~MainWindow()
{
    Log::setSink(nullptr);
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (inGame()) {
        if (!confirmLeavingGame()) {
            event->ignore();
            return;
        }
        if (inGame()) {  // the game may have ended while the question was open
            resultShown_ = true;  // closing: no "connection lost" dialog when the socket closes
            network_->resign();
            network_->disconnectGracefully(kResignWaitMs);  // deliver resign: before closing
        }
    }
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
}

void MainWindow::setupNetwork()
{
    network_ = new NetworkClient(this);

    connect(network_, &NetworkClient::loggedIn, this, [this](int w, int h) {
        if (w > 0 && h > 0) {
            boardWidth_  = w;
            boardHeight_ = h;
        }
        setAppState(AppState::WaitingForMatch);
    });

    connect(network_, &NetworkClient::disconnected, this, [this] {
        if (inGame())  // not after our own result: the server or the network went away
            showGameOver(false, "connection_lost");
        else
            setAppState(AppState::Disconnected);
    });

    connect(network_, &NetworkClient::matchFound, this,
            [this](const QString& opponent, bool goFirst, int w, int h, const QString& timeControl) {
        Log::info(QString("game_start opponent=%1 board=%2x%3 time=%4 role=%5")
                      .arg(opponent).arg(w).arg(h)
                      .arg(timeControl.isEmpty() ? "-" : timeControl, goFirst ? "first" : "second"));
        opponent_ = opponent;
        opponentTurnActive_ = false;
        invalidMove_ = false;
        resultShown_ = false;
        boardWidth_  = w;
        boardHeight_ = h;
        gameState_.reset(boardWidth_, boardHeight_);
        boardWidget_->update();
        clock_.start(timeControl, goFirst, now());
        clockTimer_->start();
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
        if (myMs >= 0)
            clock_.sync(myMs, opponentMs, now());
        setAppState(AppState::YourTurn);
    });

    connect(network_, &NetworkClient::moveConfirmed, this,
            [this](const QString& type, int x, int y) {
        const bool caught = type == "caught";
        gameState_.setCell(x, y, caught ? Cell::CapturedByMe : Cell::Mine);
        gameState_.setNewCell(x, y, caught ? NewCell::MyCatch : NewCell::MyMove);
        boardWidget_->update();
        invalidMove_ = false;
        if (movesLeft_ == 1)  // that was the last sub-move of my turn
            clock_.myTurnDone(now());
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

    // The server sends go: again right after this; updateStatus() shows the hint
    connect(network_, &NetworkClient::wrongStep, this, [this] { invalidMove_ = true; });

    connect(network_, &NetworkClient::gameOver, this, &MainWindow::showGameOver);

    // wrongcmd: is not fatal: the connection and the game go on
    connect(network_, &NetworkClient::commandRejected, this, [this] {
        statusBar()->showMessage("The server rejected a command", 3000);
    });

    connect(network_, &NetworkClient::loginFailed, this, [this](const QString& error) {
        static const QHash<QString, QString> texts = {
            {"wronguser:",        "Wrong login or password."},
            {"alreadyconnected:", "Already connected from another session."},
            {"badgame:",          "Unknown game, invalid board size or time control."},
            {"wrongver:",         "Protocol version mismatch."},
            {"servererror:",      "Server error."},
        };
        network_->disconnectFromServer();
        showError(texts.value(error, error));
    });

    connect(network_, &NetworkClient::connectionError, this, [this](const QString& message) {
        network_->disconnectFromServer();
        if (inGame())
            showGameOver(false, "connection_lost");
        else
            showError(message);
    });
}

// ── Actions ──────────────────────────────────────────────────────────────────

void MainWindow::onConnectClicked()
{
    ConnectDialog dlg(this);
    dlg.setHost(settings_.host);
    dlg.setPort(settings_.port);
    dlg.setLogin(settings_.login);
    dlg.setPassword(settings_.password);
    dlg.setRememberPassword(settings_.rememberPassword);
    dlg.setPreferredOpponent(settings_.preferred);
    dlg.setBoardSize(settings_.width, settings_.height);
    dlg.setTimeControl(settings_.timeControl);

    if (dlg.exec() != QDialog::Accepted) return;

    settings_.host             = dlg.host();
    settings_.port             = dlg.port();
    settings_.login            = dlg.login();
    settings_.password         = dlg.password();
    settings_.rememberPassword = dlg.rememberPassword();
    settings_.preferred        = dlg.preferredOpponent();
    settings_.width            = dlg.boardWidth();
    settings_.height           = dlg.boardHeight();
    settings_.timeControl      = dlg.timeControl();
    settings_.save();

    connectWithSettings();
}

void MainWindow::connectWithSettings()
{
    setAppState(AppState::Connecting);
    network_->connectToServer(settings_.host, settings_.port, settings_.login, settings_.password,
                              settings_.preferred, settings_.width, settings_.height,
                              settings_.timeControl);
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
    setAppState(AppState::Disconnected);  // also cancels a connect still in progress
}

// ── Game over ────────────────────────────────────────────────────────────────

bool MainWindow::inGame() const
{
    return !resultShown_
           && (appState_ == AppState::WaitingForTurn || appState_ == AppState::YourTurn);
}

bool MainWindow::confirmLeavingGame()
{
    QMessageBox box(QMessageBox::Question, "Leave the game?", "Leaving now counts as a loss.",
                    QMessageBox::Yes | QMessageBox::No, this);
    box.setDefaultButton(QMessageBox::No);
    leaveQuestion_ = &box;  // closed by showGameOver() if the game ends meanwhile
    return box.exec() == QMessageBox::Yes;
}

void MainWindow::showGameOver(bool won, const QString& cause)
{
    // The one place that explains a result; cause is the server's CAUSE field,
    // or connection_lost when the connection broke mid-game
    static const QHash<QString, QString> winReasons = {
        {"rules",      "Your opponent can't make 3 moves."},
        {"timeout",    "Your opponent ran out of time."},
        {"resign",     "Your opponent resigned."},
        {"disconnect", "Your opponent left the game."},
    };
    static const QHash<QString, QString> loseReasons = {
        {"rules",           "You can't make 3 moves."},
        {"timeout",         "You ran out of time."},
        {"resign",          "You resigned."},
        {"connection_lost", "The connection to the server was lost; the game counts as a loss."},
    };
    if (resultShown_) return;
    resultShown_ = true;

    Log::info(QString("game_over result=%1 cause=%2")
                  .arg(won ? "win" : "lose", cause.isEmpty() ? "-" : cause));
    if (leaveQuestion_)
        leaveQuestion_->reject();

    // Disconnecting resets the board; keep the final moves visible until the dialog closes
    keepFinalBoard_ = true;
    clock_.stop(now());
    updateClock();
    boardWidget_->setInteractive(false);
    network_->disconnectFromServer();
    setAppState(AppState::Disconnected);

    QMessageBox box(QMessageBox::Information, "Game over", won ? "You win!" : "You lose.",
                    QMessageBox::Ok, this);
    box.setInformativeText((won ? winReasons : loseReasons).value(cause));
    QPushButton* playAgain = box.addButton("Play again", QMessageBox::AcceptRole);
    box.exec();

    keepFinalBoard_ = false;
    gameState_.reset(boardWidth_, boardHeight_);
    boardWidget_->update();
    clock_.reset();
    clockTimer_->stop();
    updateClock();

    if (box.clickedButton() == playAgain && appState_ == AppState::Disconnected)
        connectWithSettings();
}

void MainWindow::showError(const QString& text)
{
    setAppState(AppState::Disconnected);
    QMessageBox::warning(this, "Error", text);
}

// ── State & status ───────────────────────────────────────────────────────────

void MainWindow::setAppState(AppState state)
{
    if (state == AppState::Disconnected) {
        if (!keepFinalBoard_) {
            gameState_.reset(boardWidth_, boardHeight_);
            boardWidget_->update();
            clock_.reset();
            clockTimer_->stop();
            updateClock();
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
    case AppState::Connecting:
        stateLabel_->setText("Connecting…");
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
        stateLabel_->setText(QString("%1Your turn  ·  %2 move%3 left")
                                 .arg(invalidMove_ ? "Invalid move — try again  ·  " : "")
                                 .arg(movesLeft_)
                                 .arg(movesLeft_ == 1 ? "" : "s"));
        gameInfoLabel_->setText(QString("vs %1").arg(opponent_));
        break;
    }
}

// ── Clock display ────────────────────────────────────────────────────────────

void MainWindow::updateClock()
{
    using Side = GameClock::Side;
    if (!clock_.active()) {
        clockLabel_->clear();
        return;
    }
    const qint64 t = now();
    auto format = [](qint64 ms, bool running) {
        const qint64 s = (ms + 999) / 1000;  // round up: 0:00 only when the time is really out
        QString text = QString("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QChar('0'));
        if (running && ms < 10000) text = "<span style='color:#e05050'>" + text + "</span>";
        return running ? "<b>" + text + "</b>" : text;
    };

    const Side running = clock_.running();
    if (clock_.turnLimit()) {
        // Only the side to move has a running limit
        if (running == Side::Mine)
            clockLabel_->setText("Your time " + format(clock_.remaining(Side::Mine, t), true));
        else if (running == Side::Opponent)
            clockLabel_->setText("Opponent's time " + format(clock_.remaining(Side::Opponent, t), true));
        else
            clockLabel_->clear();
    } else {
        clockLabel_->setText("You " + format(clock_.remaining(Side::Mine, t), running == Side::Mine)
                             + "  ·  Opponent "
                             + format(clock_.remaining(Side::Opponent, t), running == Side::Opponent));
    }
}
