#pragma once

#include <QString>

// Server protocol V2 as the client needs it (docs/PROTOCOL.md). Pure functions, no I/O,
// so they can be unit-tested with the example sessions from the docs.
namespace Protocol {

struct ServerMessage
{
    enum class Type {
        Unknown,
        Ping,          // ping:
        CommandError,  // wrongcmd:
        LoginError,    // wrongver: wronguser: alreadyconnected: badgame: servererror:  (text = the line)
        LoginOk,       // mbserver:VERSION:GAME:W:H:        (width/height 0 for any:any)
        Found,         // found:ID:OPPONENT:GAME:W:H:ROLE:[TIME:]
        Go,            // go:BOARD:MOVES:ID:[MYTIME:OPPTIME:]
        OwnMove,       // field2:TYPE:X:Y:ID:
        OpponentMove,  // field1:TYPE:X:Y:ID:
        WrongStep,     // wrongstep:ID:
        Result,        // win:ID:[CAUSE:] / lose:ID:[CAUSE:]
    };

    Type type = Type::Unknown;
    int gameId = -1;
    QString text;          // LoginError: the line; Found: opponent; Own/OpponentMove: move type; Result: cause
    QString timeControl;   // Found (empty if the server sent none)
    QString board;         // Go
    int movesRemaining = 0;
    int width = 0;
    int height = 0;
    int x = 0;
    int y = 0;
    bool first = false;    // Found: this client moves first
    bool won = false;      // Result
    qint64 myMs = -1;      // Go with a time control
    qint64 opponentMs = -1;
};

ServerMessage parse(const QString& line);

// mbclient:2:LOGIN:PASSWORD:viruswar:W:H[:TARGET[:TIME]]:  (TIME needs TARGET, which may be empty)
QString loginLine(const QString& login, const QString& password, int width, int height,
                  const QString& preferredOpponent, const QString& timeControl);

}  // namespace Protocol
