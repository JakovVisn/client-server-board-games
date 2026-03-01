#include <QCoreApplication>
#include <QTime>
#include <cstdio>
#include "server.h"

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

    Server server;
    if (!server.start())
        return 1;

    return app.exec();
}
