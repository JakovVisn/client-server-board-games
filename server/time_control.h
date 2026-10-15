#pragma once

#include <optional>
#include <QString>

// How much time a player may spend, written as in the protocol:
//   none        no limit
//   turnN       N seconds for each turn; unused time is lost
//   gameN       N seconds per player for the whole game
//   gameN+I     same, plus I seconds after each completed turn (Fischer increment)
struct TimeControl
{
    enum class Mode { None, Turn, Game };

    Mode mode = Mode::None;
    int baseMs = 0;       // Turn: limit per turn; Game: each player's starting clock
    int incrementMs = 0;  // Game only

    [[nodiscard]] static std::optional<TimeControl> parse(const QString& text);
    [[nodiscard]] QString toString() const;

    bool operator==(const TimeControl& other) const
    {
        return mode == other.mode && baseMs == other.baseMs && incrementMs == other.incrementMs;
    }
    bool operator!=(const TimeControl& other) const { return !(*this == other); }
};
