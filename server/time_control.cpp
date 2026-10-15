#include "time_control.h"
#include <QRegularExpression>

namespace {
constexpr int MAX_TURN_SECONDS      = 3600;
constexpr int MAX_GAME_SECONDS      = 24 * 3600;
constexpr int MAX_INCREMENT_SECONDS = 3600;
}

std::optional<TimeControl> TimeControl::parse(const QString& text)
{
    if (text == "none")
        return TimeControl{};

    static const QRegularExpression rxTurn(R"(^turn(\d{1,5})$)");
    if (const auto m = rxTurn.match(text); m.hasMatch()) {
        const int seconds = m.captured(1).toInt();
        if (seconds < 1 || seconds > MAX_TURN_SECONDS) return std::nullopt;
        return TimeControl{Mode::Turn, seconds * 1000, 0};
    }

    static const QRegularExpression rxGame(R"(^game(\d{1,6})(?:\+(\d{1,5}))?$)");
    if (const auto m = rxGame.match(text); m.hasMatch()) {
        const int seconds   = m.captured(1).toInt();
        const int increment = m.captured(2).toInt();  // 0 when absent
        if (seconds < 1 || seconds > MAX_GAME_SECONDS || increment > MAX_INCREMENT_SECONDS)
            return std::nullopt;
        return TimeControl{Mode::Game, seconds * 1000, increment * 1000};
    }

    return std::nullopt;
}

QString TimeControl::toString() const
{
    switch (mode) {
    case Mode::None:
        return QStringLiteral("none");
    case Mode::Turn:
        return QStringLiteral("turn%1").arg(baseMs / 1000);
    case Mode::Game:
        return incrementMs > 0
            ? QStringLiteral("game%1+%2").arg(baseMs / 1000).arg(incrementMs / 1000)
            : QStringLiteral("game%1").arg(baseMs / 1000);
    }
    return {};
}
