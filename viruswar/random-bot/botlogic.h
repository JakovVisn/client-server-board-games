#pragma once

#include <QPoint>
#include <QString>
#include <QVector>

namespace BotLogic {

[[nodiscard]] bool isValidMove(const QString& board, int w, int h, int x, int y);
[[nodiscard]] QPoint randomMove(const QString& board, int w, int h);

}
