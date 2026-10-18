#include "logging.h"
#include <cstdio>
#include <QDateTime>
#include <QFile>
#include <QMutex>
#include <QRegularExpression>

namespace {

QMutex g_mutex;
QString g_component;
int g_minRank = 1;
QFile* g_file = nullptr;
std::function<void(const QString&)> g_sink;

// QtMsgType values are not ordered by severity
int rank(QtMsgType type)
{
    switch (type) {
    case QtDebugMsg:    return 0;
    case QtInfoMsg:     return 1;
    case QtWarningMsg:  return 2;
    case QtCriticalMsg: return 3;
    case QtFatalMsg:    return 4;
    }
    return 1;
}

const char* levelName(QtMsgType type)
{
    switch (type) {
    case QtDebugMsg:    return "DEBUG";
    case QtInfoMsg:     return "INFO ";
    case QtWarningMsg:  return "WARN ";
    case QtCriticalMsg: return "ERROR";
    case QtFatalMsg:    return "FATAL";
    }
    return "INFO ";
}

void handler(QtMsgType type, const QMessageLogContext&, const QString& message)
{
    if (rank(type) < g_minRank)
        return;
    const QString line = QString("%1 %2 %3 %4")
        .arg(QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz"),
             QString::fromLatin1(levelName(type)),
             g_component.leftJustified(7),
             message);

    QMutexLocker lock(&g_mutex);
    fprintf(stderr, "%s\n", qPrintable(line));
    if (g_file) {
        g_file->write(line.toUtf8() + '\n');
        g_file->flush();
    }
    if (g_sink)
        g_sink(line);
}

}  // namespace

namespace Log {

void install(const QString& component, QtMsgType minLevel, const QString& filePath)
{
    {
        QMutexLocker lock(&g_mutex);
        g_component = component;
        g_minRank = rank(minLevel);
        if (!filePath.isEmpty()) {
            auto* file = new QFile(filePath);  // lives until the process exits
            if (file->open(QIODevice::Append | QIODevice::Text)) {
                g_file = file;
            } else {
                fprintf(stderr, "Cannot open log file %s\n", qPrintable(filePath));
                delete file;
            }
        }
    }
    qInstallMessageHandler(handler);
}

void setSink(std::function<void(const QString&)> sink)
{
    QMutexLocker lock(&g_mutex);
    g_sink = std::move(sink);
}

bool parseLevel(const QString& text, QtMsgType* level)
{
    const QString t = text.toLower();
    if (t == "debug")                   *level = QtDebugMsg;
    else if (t == "info")               *level = QtInfoMsg;
    else if (t == "warn" || t == "warning") *level = QtWarningMsg;
    else if (t == "error")              *level = QtCriticalMsg;
    else return false;
    return true;
}

void debug(const QString& message) { qDebug().noquote() << message; }
void info(const QString& message)  { qInfo().noquote() << message; }
void warn(const QString& message)  { qWarning().noquote() << message; }
void error(const QString& message) { qCritical().noquote() << message; }

QString quoted(const QString& value)
{
    if (!value.isEmpty() && !value.contains(' '))
        return value;
    QString escaped = value;
    escaped.replace('"', "\\\"");
    return '"' + escaped + '"';
}

QString maskPassword(const QString& line)
{
    // mbclient:VERSION:LOGIN:PASSWORD:...
    static const QRegularExpression rx(R"(^(\s*mbclient:[^:]*:[^:]*:)[^:]*)");
    QString masked = line;
    masked.replace(rx, R"(\1***)");
    return masked;
}

}  // namespace Log
