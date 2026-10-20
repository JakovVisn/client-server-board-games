// A real Server on a local port, driven by clients over TCP sockets.
#include <memory>
#include <QTest>
#include "server.h"

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

}  // namespace

class TestNetwork : public QObject
{
    Q_OBJECT

    QTemporaryDir dir_;
    std::unique_ptr<Server> server_;

    quint16 port() const { return server_->serverPort(); }

private slots:
    void init()
    {
        QFile auth(dir_.filePath("auth.txt"));
        QVERIFY(auth.open(QIODevice::WriteOnly | QIODevice::Text));
        auth.write("bot1:bot1:bot\n");
        auth.close();
        server_ = std::make_unique<Server>(dir_.filePath("auth.txt"), dir_.filePath("stats.txt"));
        QVERIFY(server_->start(QHostAddress::LocalHost, 0));
    }

    void cleanup() { server_.reset(); }

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
        alice.send(login("alice"));
        bob.send(login("bob"));
        const QString found = bob.lineStartingWith("found:");
        QVERIFY(!found.isEmpty());
        const QString gameId = found.section(':', 1, 1);

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
};

QTEST_GUILESS_MAIN(TestNetwork)
#include "test_network.moc"
