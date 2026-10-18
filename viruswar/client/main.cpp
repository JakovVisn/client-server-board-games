#include <QApplication>
#include "logging.h"
#include "mainwindow.h"

int main(int argc, char* argv[])
{
    const QApplication app(argc, argv);
    // DEBUG too: the log panel exists to show the protocol traffic
    Log::install("client", QtDebugMsg);
    QApplication::setApplicationName("VirusWar");

    MainWindow w;
    w.show();

    return QApplication::exec();
}
