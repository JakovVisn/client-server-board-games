#include <QCommandLineParser>
#include <QCoreApplication>
#include <QTime>
#include <cstdio>
#include "server.h"
#include "server_constants.h"

static void messageHandler(QtMsgType type, const QMessageLogContext&, const QString& msg)
{
    const char* prefix = (type == QtWarningMsg)  ? "WARN  " :
                         (type == QtCriticalMsg) ? "ERROR " : "";
    fprintf(stderr, "%s  %s%s\n",
            qPrintable(QTime::currentTime().toString("hh:mm:ss")),
            prefix,
            qPrintable(msg));
}

int main(int argc, char* argv[])
{
    qInstallMessageHandler(messageHandler);
    QCoreApplication app(argc, argv);
    app.setApplicationName("viruswar-server");
    app.setApplicationVersion("1.0");

    QCommandLineParser parser;
    parser.setApplicationDescription("VirusWar game server");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({{"p", "port"},       "Port to listen on",   "port",  QString::number(ServerConfig::PORT)});
    parser.addOption({{"H", "host"},       "Address to bind to",  "host",  ServerConfig::DEFAULT_HOST});
    parser.addOption({{"a", "auth-file"},  "Auth file path",      "file",  AuthConfig::FILE_PATH});
    parser.addOption({{"s", "stats-file"}, "Stats file path",     "file",  AuthConfig::STATS_FILE_PATH});
    parser.process(app);

    const quint16 port      = parser.value("port").toUShort();
    const QHostAddress addr(parser.value("host"));
    const QString authFile  = parser.value("auth-file");
    const QString statsFile = parser.value("stats-file");

    Server server(authFile, statsFile);
    if (!server.start(addr, port))
        return 1;

    return app.exec();
}
