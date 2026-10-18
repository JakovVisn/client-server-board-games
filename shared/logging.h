#pragma once

#include <functional>
#include <QString>
#include <QtGlobal>

// One log format for every program in the project (the Python bots use the same,
// see viruswar/common/log.py):
//
//   2026-10-18 14:03:22.417 INFO  server  game_start id=7 first=alice second=bob
//   <date> <time.ms>        <LEVEL> <component> <event> <key=value ...>
//
// Levels: DEBUG (protocol traffic), INFO (what happened), WARN (unexpected but handled),
// ERROR (an operation failed). Events are snake_case, details are key=value pairs.
namespace Log {

// Routes qDebug/qInfo/qWarning/qCritical to stderr (and to filePath, if given) in the
// format above, dropping messages below minLevel.
void install(const QString& component, QtMsgType minLevel = QtInfoMsg,
             const QString& filePath = QString());

// An extra destination for every line that passes the level filter, e.g. the client's
// log panel. Called on the thread that logged; pass nullptr to remove.
void setSink(std::function<void(const QString& line)> sink);

// "debug", "info", "warn" or "error" (case-insensitive); false for anything else.
bool parseLevel(const QString& text, QtMsgType* level);

void debug(const QString& message);
void info(const QString& message);
void warn(const QString& message);
void error(const QString& message);

// The value itself, or "value" in quotes when it is empty or contains spaces.
QString quoted(const QString& value);

// A protocol line safe to log: the password in mbclient: is replaced by ***.
QString maskPassword(const QString& line);

}  // namespace Log
