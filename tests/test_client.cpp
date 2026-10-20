// Unit tests for the client's pure parts: protocol parsing and the game clock.
#include <QTest>
#include "gameclock.h"
#include "protocol.h"

using Protocol::ServerMessage;
using Side = GameClock::Side;

class TestClient : public QObject
{
    Q_OBJECT

private slots:
    // QtTest runs test slots through the meta-object system, so they must stay members.
    // NOLINTBEGIN(readability-convert-member-functions-to-static)
    // ── Protocol ──────────────────────────────────────────────────────────

    void parseSimpleMessages()
    {
        QCOMPARE(Protocol::parse("ping:").type, ServerMessage::Type::Ping);
        QCOMPARE(Protocol::parse("wrongcmd:").type, ServerMessage::Type::CommandError);
        const auto err = Protocol::parse("wronguser:");
        QCOMPARE(err.type, ServerMessage::Type::LoginError);
        QCOMPARE(err.text, QString("wronguser:"));
    }

    void parseLogin()
    {
        const auto m = Protocol::parse("mbserver:2:viruswar:12:8:");
        QCOMPARE(m.type, ServerMessage::Type::LoginOk);
        QCOMPARE(m.width, 12);
        QCOMPARE(m.height, 8);
        const auto any = Protocol::parse("mbserver:2:viruswar:any:any:");
        QCOMPARE(any.type, ServerMessage::Type::LoginOk);
        QCOMPARE(any.width, 0);
    }

    void parseFound()
    {
        const auto m = Protocol::parse("found:7:bob:viruswar:10:10:first:turn60:");
        QCOMPARE(m.type, ServerMessage::Type::Found);
        QCOMPARE(m.gameId, 7);
        QCOMPARE(m.text, QString("bob"));
        QCOMPARE(m.width, 10);
        QVERIFY(m.first);
        QCOMPARE(m.timeControl, QString("turn60"));

        const auto old = Protocol::parse("found:0:alice:viruswar:5:5:second:");  // no TIME
        QCOMPARE(old.type, ServerMessage::Type::Found);
        QVERIFY(!old.first);
        QVERIFY(old.timeControl.isEmpty());
    }

    void parseGo()
    {
        const auto m = Protocol::parse("go:1000001000004000002000002:3:0:");
        QCOMPARE(m.type, ServerMessage::Type::Go);
        QCOMPARE(m.board, QString("1000001000004000002000002"));
        QCOMPARE(m.movesRemaining, 3);
        QCOMPARE(m.myMs, qint64(-1));

        const auto timed = Protocol::parse("go:0000000000000000000000000:2:7:57650:60000:");
        QCOMPARE(timed.type, ServerMessage::Type::Go);
        QCOMPARE(timed.gameId, 7);
        QCOMPARE(timed.myMs, qint64(57650));
        QCOMPARE(timed.opponentMs, qint64(60000));
    }

    void parseMovesAndResults()
    {
        const auto own = Protocol::parse("field2:caught:2:3:7:");
        QCOMPARE(own.type, ServerMessage::Type::OwnMove);
        QCOMPARE(own.text, QString("caught"));
        QCOMPARE(own.x, 2);
        QCOMPARE(own.y, 3);
        QCOMPARE(Protocol::parse("field1:standard:0:0:7:").type, ServerMessage::Type::OpponentMove);
        QCOMPARE(Protocol::parse("wrongstep:7:").type, ServerMessage::Type::WrongStep);

        const auto win = Protocol::parse("win:7:timeout:");
        QCOMPARE(win.type, ServerMessage::Type::Result);
        QVERIFY(win.won);
        QCOMPARE(win.text, QString("timeout"));
        const auto lose = Protocol::parse("lose:7:");  // older server: no cause
        QVERIFY(!lose.won);
        QVERIFY(lose.text.isEmpty());
    }

    void rejectMalformedLines_data()
    {
        QTest::addColumn<QString>("line");
        QTest::newRow("no colon")      << "ping";
        QTest::newRow("bad size")      << "mbserver:2:viruswar:x:10:";
        QTest::newRow("zero size")     << "mbserver:2:viruswar:0:10:";
        QTest::newRow("half any")      << "mbserver:2:viruswar:any:10:";
        QTest::newRow("bad board")     << "go:12a:3:0:";
        QTest::newRow("bad role")      << "found:7:bob:viruswar:10:10:third:";
        QTest::newRow("huge number")   << "wrongstep:99999999999:";
        QTest::newRow("prefix junk")   << "xwin:7:rules:";
        QTest::newRow("extra fields")  << "field1:standard:0:0:7:8:";
        QTest::newRow("unknown")       << "hello:";
    }

    void rejectMalformedLines()
    {
        QFETCH(QString, line);
        QCOMPARE(Protocol::parse(line).type, ServerMessage::Type::Unknown);
    }

    void loginLine()
    {
        QCOMPARE(Protocol::loginLine("alice", "secret", 10, 10, "", ""),
                 QString("mbclient:2:alice:secret:viruswar:10:10:"));
        QCOMPARE(Protocol::loginLine("alice", "secret", 10, 10, "bob", ""),
                 QString("mbclient:2:alice:secret:viruswar:10:10:bob:"));
        QCOMPARE(Protocol::loginLine("alice", "secret", 12, 12, "", "game300+5"),
                 QString("mbclient:2:alice:secret:viruswar:12:12::game300+5:"));
    }

    // ── Game clock ────────────────────────────────────────────────────────

    void noTimeControlNoClock()
    {
        GameClock c;
        c.start("none", true, 0);
        QVERIFY(!c.active());
        c.start("", true, 0);
        QVERIFY(!c.active());
    }

    void turnLimitResetsEachTurn()
    {
        GameClock c;
        c.start("turn60", true, 0);
        QVERIFY(c.active());
        QVERIFY(c.turnLimit());
        QCOMPARE(c.running(), Side::Mine);
        QCOMPARE(c.remaining(Side::Mine, 15000), qint64(45000));
        c.myTurnDone(20000);
        QCOMPARE(c.running(), Side::Opponent);
        QCOMPARE(c.remaining(Side::Mine, 20000), qint64(60000));      // fresh limit next turn
        QCOMPARE(c.remaining(Side::Opponent, 30000), qint64(50000));
    }

    void gameClockWithIncrement()
    {
        GameClock c;
        c.start("game300+5", false, 0);
        QCOMPARE(c.running(), Side::Opponent);
        QCOMPARE(c.remaining(Side::Opponent, 10000), qint64(290000));
        c.sync(300000, 290000, 10000);                    // go: — my turn starts
        QCOMPARE(c.running(), Side::Mine);
        c.myTurnDone(40000);                              // 30 s spent, +5 s increment
        QCOMPARE(c.remaining(Side::Mine, 40000), qint64(275000));
        c.stop(50000);
        QCOMPARE(c.running(), Side::None);
        QCOMPARE(c.remaining(Side::Opponent, 99000), qint64(280000));  // frozen
    }

    void neverNegative()
    {
        GameClock c;
        c.start("turn30", true, 0);
        QCOMPARE(c.remaining(Side::Mine, 45000), qint64(0));
    }
    // NOLINTEND(readability-convert-member-functions-to-static)
};

QTEST_GUILESS_MAIN(TestClient)
#include "test_client.moc"
