// Unit tests for the server's parts that need no sockets: Virus War rules, time controls,
// matchmaking, login rate limiting and account storage.
#include <vector>
#include <QFile>
#include <QObject>
#include <QTemporaryDir>
#include <QTest>
#include "auth_manager.h"
#include "matchmaking.h"
#include "rate_limiter.h"
#include "time_control.h"
#include "virus_war_game.h"

using Matchmaking::Candidate;
using Matchmaking::Pairing;

namespace {

int countValidMoves(const VirusWarGame& g)
{
    int n = 0;
    for (int y = 0; y < g.boardHeight(); ++y)
        for (int x = 0; x < g.boardWidth(); ++x)
            n += g.isValidMove(x, y);
    return n;
}

QString readFile(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly | QIODevice::Text) ? QString::fromUtf8(file.readAll()) : QString();
}

// Returns false at the first illegal move; callers wrap it in QVERIFY so the test stops there
bool playTurn(VirusWarGame& g, std::initializer_list<std::pair<int, int>> moves)
{
    for (auto [x, y] : moves) {
        if (!g.isValidMove(x, y)) {
            qWarning("illegal move (%d, %d)", x, y);
            return false;
        }
        (void)g.applyMove(x, y);
    }
    return true;
}

Candidate human(const QString& login, QMap<QString, int> prefs = {})
{
    return {login, false, 0, 0, std::move(prefs)};
}

Candidate leagueBot(const QString& login, int autoLimit, int autoGames = 0)
{
    return {login, true, autoLimit, autoGames, {}};
}

std::vector<Pairing> pairings(const std::vector<Candidate>& c, int activeGames = 0)
{
    return Matchmaking::findPairings(c, [](int, int) { return true; },
                                     [activeGames](int, int) { return activeGames; });
}

}  // namespace

class TestServer : public QObject
{
    Q_OBJECT

private slots:
    // QtTest runs test slots through the meta-object system, so they must stay members.
    // NOLINTBEGIN(readability-convert-member-functions-to-static)
    // ── Virus War rules ───────────────────────────────────────────────────

    void firstMoveOnlyInACorner()
    {
        VirusWarGame g(5, 5);
        QCOMPARE(countValidMoves(g), 2);
        QVERIFY(g.isValidMove(0, 0));
        QVERIFY(g.isValidMove(4, 4));
    }

    void movesMustConnect()
    {
        VirusWarGame g(5, 5);
        QCOMPARE(g.applyMove(0, 0), std::string("standard"));
        QCOMPARE(g.movesRemaining(), 2);
        // The three neighbours; the other corner is gone once I hold one
        QCOMPARE(countValidMoves(g), 3);
        QVERIFY(!g.isValidMove(4, 4));
        QVERIFY(!g.isValidMove(2, 0));
    }

    void threeSubMovesMakeATurn()
    {
        VirusWarGame g(5, 5);
        QVERIFY(playTurn(g, {{0, 0}, {1, 1}}));
        QCOMPARE(g.turnResult(), AbstractGame::TurnResult::Ongoing);
        QVERIFY(playTurn(g, {{2, 2}}));
        QCOMPARE(g.turnResult(), AbstractGame::TurnResult::TurnComplete);
        g.advanceTurn();
        QCOMPARE(g.currentPlayer(), 1);
        QCOMPARE(g.movesRemaining(), 3);
        // The second player may only start in the free corner
        QCOMPARE(countValidMoves(g), 1);
        QVERIFY(g.isValidMove(4, 4));
    }

    void boardIsSentFromEachPlayersView()
    {
        VirusWarGame g(5, 5);
        (void)g.applyMove(0, 0);
        QCOMPARE(g.serializeForCurrentPlayer()[0], '1');
        QCOMPARE(g.serializeForOpponent()[0], '2');
    }

    void placingOnAnOpponentPieceCaptures()
    {
        VirusWarGame g(5, 5);
        QVERIFY(playTurn(g, {{0, 0}, {1, 1}, {2, 2}}));
        g.advanceTurn();
        QVERIFY(playTurn(g, {{4, 4}, {3, 3}}));
        QCOMPARE(g.applyMove(2, 2), std::string("caught"));
        QCOMPARE(g.serializeForCurrentPlayer()[2 * 5 + 2], '3');
        QCOMPARE(g.serializeForOpponent()[2 * 5 + 2], '4');
        QVERIFY(!g.isValidMove(2, 2));  // captured cells never change again
    }

    void gameOverWhenTheOpponentCannotMove()
    {
        // Opponent's only piece is in the corner, walled in by my captures
        VirusWarGame g(5, 5);
        g.initFromBoard("10000" "00000" "00000" "00033" "00032");
        QVERIFY(playTurn(g, {{1, 0}, {0, 1}}));
        QCOMPARE(g.turnResult(), AbstractGame::TurnResult::Ongoing);
        QVERIFY(playTurn(g, {{1, 1}}));
        QCOMPARE(g.turnResult(), AbstractGame::TurnResult::GameOver);
    }

    void initFromBoardRoundTrip()
    {
        const std::string board = "1000001000004000002000002";
        VirusWarGame g(5, 5);
        g.initFromBoard(board);
        QCOMPARE(g.serializeForCurrentPlayer(), board);
        QCOMPARE(g.movesRemaining(), 3);
    }

    void initFromBoardMidTurn()
    {
        VirusWarGame g(5, 5);
        g.initFromBoard("1000001000004000002000002", 1);
        QCOMPARE(g.movesRemaining(), 1);
        QVERIFY(g.isValidMove(2, 1));
        QCOMPARE(g.applyMove(2, 1), std::string("standard"));
        QCOMPARE(g.turnResult(), AbstractGame::TurnResult::TurnComplete);
    }

    void fieldFromBoard()
    {
        const std::string board = "1000001000004000002000002";
        const VirusWarField field = VirusWarField::fromBoard(board, 5, 5);
        QCOMPARE(field.serialize(), board);
        QVERIFY(field.isValidMove(2, 1));     // next to my piece at (1, 1)
        QVERIFY(!field.isValidMove(2, 2));    // captured by the opponent
        QVERIFY(!field.isValidMove(4, 0));    // not connected
    }

    void boardSizes()
    {
        VirusWarGame g;
        QVERIFY(g.isValidBoardSize(5, 5));
        QVERIFY(g.isValidBoardSize(20, 7));
        QVERIFY(!g.isValidBoardSize(4, 10));
        QVERIFY(!g.isValidBoardSize(10, 21));
    }

    // ── Time controls ─────────────────────────────────────────────────────

    void timeControlParse_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<bool>("valid");
        QTest::addColumn<QString>("canonical");
        QTest::newRow("none")      << "none"      << true  << "none";
        QTest::newRow("turn")      << "turn60"    << true  << "turn60";
        QTest::newRow("game")      << "game300"   << true  << "game300";
        QTest::newRow("increment") << "game300+5" << true  << "game300+5";
        QTest::newRow("zero inc")  << "game300+0" << true  << "game300";
        QTest::newRow("turn0")     << "turn0"     << false << "";
        QTest::newRow("too long")  << "turn3601"  << false << "";
        QTest::newRow("turn inc")  << "turn60+5"  << false << "";
        QTest::newRow("dangling")  << "game300+"  << false << "";
        QTest::newRow("junk")      << "fast"      << false << "";
    }

    void timeControlParse()
    {
        QFETCH(QString, text);
        QFETCH(bool, valid);
        QFETCH(QString, canonical);
        const auto tc = TimeControl::parse(text);
        QCOMPARE(tc.has_value(), valid);
        if (valid)
            QCOMPARE(tc->toString(), canonical);
    }

    void timeControlValues()
    {
        const auto tc = TimeControl::parse("game300+5");
        QVERIFY(tc);
        QCOMPARE(tc->mode, TimeControl::Mode::Game);
        QCOMPARE(tc->baseMs, 300000);
        QCOMPARE(tc->incrementMs, 5000);
    }

    // ── Matchmaking ───────────────────────────────────────────────────────

    void twoHumansPlayEachOther()
    {
        const auto p = pairings({human("alice"), human("bob")});
        QCOMPARE(p.size(), size_t(1));
        QVERIFY(!p[0].autoMatch);
    }

    void seekerGetsALeagueBot()
    {
        const auto p = pairings({human("alice"), leagueBot("azbot", 3)});
        QCOMPARE(p.size(), size_t(1));
        QCOMPARE(p[0].a, 0);
        QCOMPARE(p[0].b, 1);
        QVERIFY(p[0].autoMatch);
    }

    void fullLeagueBotIsSkipped()
    {
        QVERIFY(pairings({human("alice"), leagueBot("azbot", 1, 1)}).empty());
    }

    void autoSlotsAreCounted()
    {
        const auto p = pairings({human("alice"), human("bob", {{"nobody", 1}}),
                                 leagueBot("azbot", 1), human("carol")});
        // alice and carol pair in pass 2; nobody is left for the bot
        QCOMPARE(p.size(), size_t(1));
        QVERIFY(!p[0].autoMatch);

        const auto q = pairings({leagueBot("azbot", 1),
                                 {"rltrain_1", true, 0, 0, {}}, {"rltrain_2", true, 0, 0, {}}});
        QCOMPARE(q.size(), size_t(1));  // one free slot, two seekers
    }

    void botRequestingAHumanStaysAvailable()
    {
        Candidate randbot{"randbot", true, 0, 0, {{"alice", 1}}};
        const auto p = pairings({randbot, human("alice"), human("bob")});
        QCOMPARE(p.size(), size_t(1));
        QCOMPARE(p[0].a, 0);
        QCOMPARE(p[0].b, 1);
    }

    void preferencesMustBeMutual()
    {
        QVERIFY(pairings({human("alice", {{"bob", 1}}), human("bob", {{"carol", 1}})}).empty());
        QCOMPARE(pairings({human("alice", {{"bob", 1}}), human("bob", {{"alice", 1}})}).size(),
                 size_t(1));
    }

    void preferenceLimit()
    {
        Candidate bot{"randbot", true, 0, 0, {{"azbot", 2}}};
        QCOMPARE(pairings({bot, leagueBot("azbot", 0)}, 1).size(), size_t(1));
        QVERIFY(pairings({bot, leagueBot("azbot", 0)}, 2).empty());
    }

    void bothPreferenceLimitsApply()
    {
        Candidate a{"bota", true, 0, 0, {{"botb", 5}}};
        Candidate b{"botb", true, 0, 0, {{"bota", 1}}};
        QCOMPARE(pairings({a, b}).size(), size_t(1));
        QVERIFY(pairings({a, b}, 1).empty());
        QVERIFY(pairings({b, a}, 1).empty());
    }

    void incompatibleClientsAreNotPaired()
    {
        const auto p = Matchmaking::findPairings({human("alice"), human("bob")},
                                                 [](int, int) { return false; },
                                                 [](int, int) { return 0; });
        QVERIFY(p.empty());
    }

    void aHumanPlaysOneGameAtATime()
    {
        const auto p = pairings({human("alice", {{"bob", 1}}), human("bob"), human("carol"),
                                 leagueBot("azbot", 3)});
        // alice-bob in pass 1, carol-azbot in pass 3; bob is not paired twice
        QCOMPARE(p.size(), size_t(2));
        QCOMPARE(p[1].a, 2);
        QVERIFY(p[1].autoMatch);
    }

    // ── Login rate limiting ───────────────────────────────────────────────

    void burstThenOneAttemptPerInterval()
    {
        RateLimiter limiter(3, 1000);
        for (int i = 0; i < 3; ++i)
            QVERIFY(limiter.allow("1.2.3.4", 0));
        QVERIFY(!limiter.allow("1.2.3.4", 999));
        QVERIFY(limiter.allow("5.6.7.8", 999));  // other addresses have their own bucket
        QVERIFY(limiter.allow("1.2.3.4", 1000));
        QVERIFY(!limiter.allow("1.2.3.4", 1500));
        QVERIFY(limiter.allow("1.2.3.4", 2000));  // the half token earned at 1500 is kept
    }

    void bucketRefillsUpToTheBurst()
    {
        RateLimiter limiter(3, 1000);
        QVERIFY(limiter.allow("1.2.3.4", 0));
        for (int i = 0; i < 3; ++i)
            QVERIFY(limiter.allow("1.2.3.4", 60000));
        QVERIFY(!limiter.allow("1.2.3.4", 60000));
    }

    void fullBucketsArePruned()
    {
        RateLimiter limiter(3, 1000);
        QVERIFY(limiter.allow("1.2.3.4", 0));
        QVERIFY(limiter.allow("5.6.7.8", 0));
        QVERIFY(limiter.allow("5.6.7.8", 0));
        limiter.prune(1000);
        QCOMPARE(limiter.size(), 1);  // 5.6.7.8 still owes a token
        limiter.prune(2000);
        QCOMPARE(limiter.size(), 0);
    }

    // ── Accounts ──────────────────────────────────────────────────────────

    void newAccountsAreStoredHashed()
    {
        QTemporaryDir dir;
        AuthManager auth(dir.filePath("auth.txt"), dir.filePath("stats.txt"));
        QCOMPARE(auth.verify("alice", "secret"), AuthManager::Result::NotFound);
        QCOMPARE(auth.registerUser("alice", "secret"), AuthManager::Result::Authorized);

        const QString text = readFile(dir.filePath("auth.txt"));
        QVERIFY(text.startsWith("alice:pbkdf2_sha256$"));
        QVERIFY(!text.contains("secret"));
        QCOMPARE(auth.verify("alice", "secret"), AuthManager::Result::Authorized);
        QCOMPARE(auth.verify("alice", "wrong"), AuthManager::Result::WrongPassword);
    }

    void plainPasswordIsHashedOnLogin()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("auth.txt");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
        file.write("user1:user1\nazbot:azbot:bot:3\n");
        file.close();

        AuthManager auth(path, dir.filePath("stats.txt"));
        QCOMPARE(auth.verify("azbot", "nope"), AuthManager::Result::WrongPassword);
        QVERIFY(readFile(path).contains("azbot:azbot:bot:3"));  // unchanged after a failed login
        QCOMPARE(auth.verify("azbot", "azbot"), AuthManager::Result::Authorized);

        const QString text = readFile(path);
        QVERIFY(text.startsWith("user1:user1\n"));             // other accounts untouched
        QVERIFY(!text.contains("azbot:azbot"));
        QVERIFY(text.contains(":bot:3\n"));
        QVERIFY(auth.isBot("azbot"));
        QCOMPARE(auth.autoConnectionLimit("azbot"), 3);
        QCOMPARE(auth.verify("azbot", "azbot"), AuthManager::Result::Authorized);
    }

    void registeringAfterALineWithoutNewline()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("auth.txt");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
        file.write("azbot:azbot:bot:3");
        file.close();

        AuthManager auth(path, dir.filePath("stats.txt"));
        QCOMPARE(auth.registerUser("bob", "secret"), AuthManager::Result::Authorized);
        QVERIFY(readFile(path).startsWith("azbot:azbot:bot:3\nbob:pbkdf2_sha256$"));
        QCOMPARE(auth.autoConnectionLimit("azbot"), 3);
        QCOMPARE(auth.verify("bob", "secret"), AuthManager::Result::Authorized);
    }

    // NOLINTEND(readability-convert-member-functions-to-static)
};

QTEST_GUILESS_MAIN(TestServer)
#include "test_server.moc"
