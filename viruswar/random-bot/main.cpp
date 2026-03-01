#include <QCoreApplication>
#include <QCommandLineParser>
#include <QTime>
#include <atomic>
#include <csignal>
#include <cstdio>
#include "botclient.h"

static void messageHandler(QtMsgType type, const QMessageLogContext&, const QString& msg)
{
    const char* prefix = (type == QtWarningMsg)  ? "WARN  " :
                         (type == QtCriticalMsg) ? "ERROR " : "";
    fprintf(stderr, "%s  %s%s\n",
            qPrintable(QTime::currentTime().toString("hh:mm:ss")),
            prefix,
            qPrintable(msg));
}

static std::atomic<int> g_signalCount{0};
static void handleSignal(int) { ++g_signalCount; }

int main(int argc, char* argv[])
{
    qInstallMessageHandler(messageHandler);
    QCoreApplication app(argc, argv);
    app.setApplicationName("random-bot");
    app.setApplicationVersion("1.0");

    QCommandLineParser parser;
    parser.setApplicationDescription("VirusWar random bot — plays random valid moves");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({{"H", "host"},     "Server host",    "host",     "127.0.0.1"});
    parser.addOption({{"p", "port"},     "Server port",    "port",     "1234"});
    parser.addOption({{"l", "login"},    "Bot login (required)",    "login"});
    parser.addOption({{"w", "password"}, "Bot password (required)", "password"});
    parser.addOption({{"P", "preferred"},
                      "Preferred opponents, e.g. rltrain/4 or rltrain/4,rlplay/2",
                      "preferred", "rltrain"});
    parser.process(app);

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

    QTimer signalPoller;
    QObject::connect(&signalPoller, &QTimer::timeout, [bot] {
        const int n = g_signalCount.load();
        if (n == 1) bot->initiateShutdown();
        else if (n >= 2) QCoreApplication::exit(0);
    });
    signalPoller.start(200);

    qDebug() << "Started as" << login << "targeting" << host << ":" << port;
    return app.exec();
}
