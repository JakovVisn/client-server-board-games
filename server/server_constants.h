#pragma once

#include <QHostAddress>

namespace ServerConfig {
    inline constexpr quint16 PORT = 1234;
    inline const QHostAddress ADDRESS = QHostAddress::Any;
    inline constexpr int SEARCH_INTERVAL = 3000;
    inline constexpr int INACTIVE_MIN_MS = 10000;
    inline constexpr int INACTIVE_MAX_MS = 60000;
}
