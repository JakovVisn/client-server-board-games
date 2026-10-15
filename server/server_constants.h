#pragma once

namespace ServerConfig {
    enum class ProtocolVersion : int { V1 = 1, V2 = 2 };
    inline constexpr ProtocolVersion PROTOCOL_VERSION = ProtocolVersion::V2;

    inline const QString DEFAULT_GAME_NAME = "viruswar";

    inline constexpr quint16 PORT = 1234;
    inline const QString DEFAULT_HOST = QStringLiteral("0.0.0.0");
    inline constexpr int SEARCH_INTERVAL = 200;
    inline constexpr int INACTIVE_MIN_MS = 10000;
    inline constexpr int INACTIVE_MAX_MS = 300000;
    // Used when neither player asks for a time control (see time_control.h)
    inline const QString DEFAULT_TIME_CONTROL = QStringLiteral("turn60");
}

namespace AuthConfig {
    inline const QString FILE_PATH = "auth.txt";
    inline const QString STATS_FILE_PATH = "stats.txt";

    inline constexpr int LOGIN_LENGTH_MIN    = 3;
    inline constexpr int LOGIN_LENGTH_MAX    = 20;
    inline constexpr int PASSWORD_LENGTH_MIN = 3;
    inline constexpr int PASSWORD_LENGTH_MAX = 20;
}
