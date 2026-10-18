#include <QCommandLineParser>
#include <atomic>
#include <csignal>
#include <cstdio>
#include "botclient.h"
#include "logging.h"

static std::atomic g_signalCount{0};
static void handleSignal(int) { ++g_signalCount; }

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("random-bot");
    QCoreApplication::setApplicationVersion("1.0");

    QCommandLineParser parser;
    parser.setApplicationDescription("VirusWar random bot — plays random valid moves");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({{"H", "host"},     "Server host",    "host",     "127.0.0.1"});
    parser.addOption({{"p", "port"},     "Server port",    "port",     "1234"});
    parser.addOption({{"l", "login"},    "Bot login (required)",    "login"});
    parser.addOption({{"w", "password"}, "Bot password (required)", "password"});
    parser.addOption({{"P", "preferred"},
                      "Preferred opponents, e.g. alice or rltrain_1/4,rltrain_2/4 (default: none, then it "
                      "plays league bots, one game at a time, and humans who ask for it)",
                      "preferred"});
    parser.addOption({"log-level", "debug (adds protocol traffic), info, warn or error", "level", "info"});
    parser.addOption({"log-file", "Also append log lines to this file", "file"});
    parser.process(app);

    QtMsgType logLevel;
    if (!Log::parseLevel(parser.value("log-level"), &logLevel)) {
        fprintf(stderr, "Invalid --log-level: %s\n", qPrintable(parser.value("log-level")));
        return 1;
    }
    Log::install("randbot", logLevel, parser.value("log-file"));

    if (!parser.isSet("login") || !parser.isSet("password")) {
        fprintf(stderr, "Error: --login and --password are required\n\n");
        parser.showHelp(1);
    }

    const QString host      = parser.value("host");
    const quint16 port      = parser.value("port").toUShort();
    const QString login     = parser.value("login");
    const QString password  = parser.value("password");
    const QString preferred = parser.value("preferred");

    std::signal(SIGINT,  handleSignal);
    std::signal(SIGTERM, handleSignal);

    auto* bot = new BotClient(host, port, login, password, preferred, &app);
    bot->start();

    // The handler only counts signals; 1st: finish active games, 2nd: quit at once
    QTimer signalPoller;
    QObject::connect(&signalPoller, &QTimer::timeout, [bot] {
        const int n = g_signalCount.load();
        if (n == 1) bot->initiateShutdown();
        else if (n >= 2) QCoreApplication::exit(0);
    });
    signalPoller.start(200);

    Log::info(QString("start login=%1 host=%2 port=%3 preferred=%4")
                  .arg(login, host).arg(port).arg(preferred.isEmpty() ? "-" : preferred));
    return QCoreApplication::exec();
}
