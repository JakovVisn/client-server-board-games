#pragma once

#include <QHostAddress>

namespace ServerConfig {
    enum class ProtocolVersion : int { V1 = 1 };
    inline constexpr ProtocolVersion PROTOCOL_VERSION = ProtocolVersion::V1;

    inline constexpr quint16 PORT = 1234;
    inline const QHostAddress ADDRESS = QHostAddress::Any;
    inline constexpr int SEARCH_INTERVAL = 3000;
    inline constexpr int INACTIVE_MIN_MS = 10000;
    inline constexpr int INACTIVE_MAX_MS = 60000;
}

namespace AuthConfig {
    inline const QString FILE_PATH = "auth.txt";

    inline constexpr int LOGIN_LENGTH_MIN    = 3;
    inline constexpr int LOGIN_LENGTH_MAX    = 20;
    inline constexpr int PASSWORD_LENGTH_MIN = 3;
    inline constexpr int PASSWORD_LENGTH_MAX = 20;
}
