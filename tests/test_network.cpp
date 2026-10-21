// A real Server on a local port, driven by clients over TCP sockets.
#include <memory>
#include <vector>
#include <QDeadlineTimer>
#include <QFile>
#include <QHostAddress>
#include <QObject>
#include <QSignalSpy>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include "server.h"
#include "server_constants.h"

namespace {

constexpr int kWaitMs = 5000;

// qWait, not a blocking socket wait: the server runs in this thread
template <typename Done>
bool waitUntil(Done done)
{
    const QDeadlineTimer deadline(kWaitMs);
    while (!done() && !deadline.hasExpired())
        QTest::qWait(10);
    return done();
}

class Peer
{
public:
    explicit Peer(quint16 port)
    {
        socket_.connectToHost(QHostAddress::LocalHost, port);
        waitUntil([this] { return connected(); });
    }

    void send(const QString& line) { socket_.write((line + '\n').toUtf8()); }

    // Skips earlier lines; empty on timeout
    QString lineStartingWith(const QString& prefix)
    {
        QString found;
        waitUntil([&] {
            while (found.isEmpty() && socket_.canReadLine()) {
                const QString line = QString::fromUtf8(socket_.readLine()).trimmed();
                if (line.startsWith(prefix)) found = line;
            }
            return !found.isEmpty();
        });
        return found;
    }

    QString nextLine() { return lineStartingWith({}); }

    bool waitForDisconnect()
    {
        return waitUntil([this] { return socket_.state() == QAbstractSocket::UnconnectedState; });
    }

    // A closed connection may not have been noticed yet; a reply proves the server still serves it
    bool servedByServer()
    {
        send("ping:");
        return lineStartingWith("pong:") == "pong:";
    }

private:
    bool connected() const { return socket_.state() == QAbstractSocket::ConnectedState; }

    QTcpSocket socket_;
};

QString login(const QString& name) { return QString("mbclient:2:%1:%1:").arg(name); }

// Both log in and are paired; returns the game id
QString startGame(Peer& a, Peer& b)
{
    a.send(login("alice"));
    b.send(login("bob"));
    return b.lineStartingWith("found:").section(':', 1, 1);
}

}  // namespace

class TestNetwork : public QObject
{
    Q_OBJECT

    std::unique_ptr<QTemporaryDir> dir_;
    std::unique_ptr<Server> server_;

    quint16 port() const { return server_->serverPort(); }

private slots:
    void init()
    {
        dir_ = std::make_unique<QTemporaryDir>();
        QFile auth(dir_->filePath("auth.txt"));
        QVERIFY(auth.open(QIODevice::WriteOnly | QIODevice::Text));
        auth.write("bot1:bot1:bot\n");
        auth.close();
        server_ = std::make_unique<Server>(dir_->filePath("auth.txt"), dir_->filePath("stats.txt"));
        QVERIFY(server_->start(QHostAddress::LocalHost, 0));
    }

    void cleanup()
    {
        server_.reset();
        dir_.reset();
    }

    void humanLoginReplacesTheOldConnection()
    {
        Peer first(port());
        first.send(login("alice"));
        QVERIFY(!first.lineStartingWith("mbserver:").isEmpty());

        Peer second(port());
        second.send(login("alice"));
        QVERIFY(!second.lineStartingWith("mbserver:").isEmpty());
        QVERIFY(first.waitForDisconnect());
        QVERIFY(second.servedByServer());
    }

    void replacedPlayerLosesTheGame()
    {
        Peer alice(port());
        Peer bob(port());
        const QString gameId = startGame(alice, bob);
        QVERIFY(!gameId.isEmpty());

        Peer aliceAgain(port());
        aliceAgain.send(login("alice"));
        QVERIFY(!aliceAgain.lineStartingWith("mbserver:").isEmpty());
        QCOMPARE(bob.lineStartingWith("win:"), QString("win:%1:disconnect:").arg(gameId));
    }

    void wrongPasswordDoesNotReplace()
    {
        Peer first(port());
        first.send(login("alice"));
        QVERIFY(!first.lineStartingWith("mbserver:").isEmpty());

        Peer intruder(port());
        intruder.send("mbclient:2:alice:wrong:");
        QCOMPARE(intruder.nextLine(), QString("wronguser:"));
        QVERIFY(first.servedByServer());
    }

    void rejectedLoginDoesNotReplace()
    {
        Peer first(port());
        first.send(login("alice"));
        QVERIFY(!first.lineStartingWith("mbserver:").isEmpty());

        Peer second(port());
        second.send("mbclient:2:alice:alice:viruswar:99:99:");
        QCOMPARE(second.nextLine(), QString("badgame:"));
        QVERIFY(first.servedByServer());
    }

    void invalidRequestRegistersNothing()
    {
        Peer peer(port());
        peer.send("mbclient:2:newcomer:secret:viruswar:99:99:");
        QCOMPARE(peer.nextLine(), QString("badgame:"));

        QFile auth(dir_->filePath("auth.txt"));
        QVERIFY(auth.open(QIODevice::ReadOnly | QIODevice::Text));
        QVERIFY(!auth.readAll().contains("newcomer"));
    }

    void secondBotInstanceIsRejected()
    {
        Peer first(port());
        first.send(login("bot1"));
        QVERIFY(!first.lineStartingWith("mbserver:").isEmpty());

        Peer second(port());
        second.send(login("bot1"));
        QCOMPARE(second.nextLine(), QString("alreadyconnected:"));
        QVERIFY(first.servedByServer());
    }

    void pendingLoginsPerAddressAreCapped()
    {
        std::vector<std::unique_ptr<Peer>> pending;
        for (int i = 0; i < ServerConfig::MAX_PENDING_LOGINS_PER_IP; ++i)
            pending.push_back(std::make_unique<Peer>(port()));
        QVERIFY(pending.back()->servedByServer());

        Peer oneTooMany(port());
        QVERIFY(oneTooMany.waitForDisconnect());

        pending.front()->send(login("alice"));
        QVERIFY(!pending.front()->lineStartingWith("mbserver:").isEmpty());
        Peer afterLogin(port());  // a logged-in connection no longer counts
        QVERIFY(afterLogin.servedByServer());
    }

    void shutdownClosesIdleClientsAndStopsListening()
    {
        Peer idle(port());
        idle.send(login("alice"));
        QVERIFY(!idle.lineStartingWith("mbserver:").isEmpty());

        const quint16 oldPort = port();
        QSignalSpy stopped(server_.get(), &Server::stopped);
        server_->shutDown(kWaitMs);
        QCOMPARE(stopped.count(), 1);  // no games to wait for
        QVERIFY(idle.waitForDisconnect());

        QTcpSocket late;
        late.connectToHost(QHostAddress::LocalHost, oldPort);
        QVERIFY(waitUntil([&late] { return late.state() == QAbstractSocket::UnconnectedState; }));
    }

    void runningGameFinishesBeforeStop()
    {
        Peer alice(port());
        Peer bob(port());
        const QString gameId = startGame(alice, bob);
        QVERIFY(!gameId.isEmpty());

        QSignalSpy stopped(server_.get(), &Server::stopped);
        server_->shutDown(kWaitMs);
        QVERIFY(alice.servedByServer());
        QCOMPARE(stopped.count(), 0);

        alice.send(QString("resign:%1:").arg(gameId));
        QCOMPARE(bob.lineStartingWith("win:"), QString("win:%1:resign:").arg(gameId));
        QTRY_COMPARE_WITH_TIMEOUT(stopped.count(), 1, kWaitMs);  // may fire while bob waits
        QVERIFY(alice.waitForDisconnect());
        QVERIFY(bob.waitForDisconnect());
    }

    void graceCutsGamesWithoutAResult()
    {
        Peer alice(port());
        Peer bob(port());
        QVERIFY(!startGame(alice, bob).isEmpty());

        QSignalSpy stopped(server_.get(), &Server::stopped);
        server_->shutDown(300);
        QTRY_COMPARE_WITH_TIMEOUT(stopped.count(), 1, kWaitMs);
        QVERIFY(!QFile::exists(dir_->filePath("stats.txt")));
    }
};

QTEST_GUILESS_MAIN(TestNetwork)
#include "test_network.moc"
