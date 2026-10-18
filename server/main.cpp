#include <QCommandLineParser>
#include <QCoreApplication>
#include <cstdio>
#include "logging.h"
#include "server.h"
#include "server_constants.h"
#include "time_control.h"

int main(int argc, char* argv[])
{
    const QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("viruswar-server");
    QCoreApplication::setApplicationVersion("1.0");

    QCommandLineParser parser;
    parser.setApplicationDescription("VirusWar game server");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({{"p", "port"},       "Port to listen on",   "port",  QString::number(ServerConfig::PORT)});
    parser.addOption({{"H", "host"},       "Address to bind to",  "host",  ServerConfig::DEFAULT_HOST});
    parser.addOption({{"a", "auth-file"},  "Auth file path",      "file",  AuthConfig::FILE_PATH});
    parser.addOption({{"s", "stats-file"}, "Stats file path",     "file",  AuthConfig::STATS_FILE_PATH});
    parser.addOption({{"t", "time-control"},
                      "Time control when neither player asks for one: none, turnN, gameN or gameN+I (seconds)",
                      "spec", ServerConfig::DEFAULT_TIME_CONTROL});
    parser.addOption({"log-level", "debug (adds protocol traffic), info, warn or error", "level", "info"});
    parser.addOption({"log-file", "Also append log lines to this file", "file"});
    parser.process(app);

    QtMsgType logLevel;
    if (!Log::parseLevel(parser.value("log-level"), &logLevel)) {
        fprintf(stderr, "Invalid --log-level: %s\n", qPrintable(parser.value("log-level")));
        return 1;
    }
    Log::install("server", logLevel, parser.value("log-file"));

    const auto timeControl = TimeControl::parse(parser.value("time-control"));
    if (!timeControl) {
        fprintf(stderr, "Invalid --time-control: %s\n", qPrintable(parser.value("time-control")));
        return 1;
    }

    const quint16 port      = parser.value("port").toUShort();
    const QHostAddress addr(parser.value("host"));
    const QString authFile  = parser.value("auth-file");
    const QString statsFile = parser.value("stats-file");

    Server server(authFile, statsFile);
    server.setDefaultTimeControl(*timeControl);
    if (!server.start(addr, port))
        return 1;

    return QCoreApplication::exec();
}
