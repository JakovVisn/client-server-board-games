#include "protocol.h"
#include <QRegularExpression>
#include <QStringList>

namespace Protocol {

namespace {

// Non-negative int, at most 9 digits; -1 if the field is not one
int number(const QString& field)
{
    bool ok = false;
    const int value = field.toInt(&ok);
    return (ok && value >= 0 && field.size() <= 9) ? value : -1;
}

bool isName(const QString& field)
{
    static const QRegularExpression rx(R"(^\w+$)");
    return rx.match(field).hasMatch();
}

}  // namespace

ServerMessage parse(const QString& line)
{
    using Type = ServerMessage::Type;
    ServerMessage msg;

    // Every message ends with ':', so the last split field is empty
    if (!line.endsWith(':'))
        return msg;
    QStringList f = line.split(':');
    f.removeLast();
    const QString& cmd = f.first();
    const int n = static_cast<int>(f.size());

    if (cmd == "ping" && n == 1) { msg.type = Type::Ping; return msg; }
    if (cmd == "wrongcmd" && n == 1) { msg.type = Type::CommandError; return msg; }
    if (n == 1 && (cmd == "wrongver" || cmd == "wronguser" || cmd == "alreadyconnected" ||
                   cmd == "badgame" || cmd == "servererror")) {
        msg.type = Type::LoginError;
        msg.text = line;
        return msg;
    }

    if (cmd == "mbserver" && n == 5) {
        // Size 0×0 means any:any
        if (f[3] != "any" || f[4] != "any") {
            msg.width  = number(f[3]);
            msg.height = number(f[4]);
            if (msg.width <= 0 || msg.height <= 0) return msg;
        }
        msg.type = Type::LoginOk;
        return msg;
    }

    if (cmd == "found" && (n == 7 || n == 8)) {
        msg.gameId = number(f[1]);
        msg.width  = number(f[4]);
        msg.height = number(f[5]);
        if (msg.gameId < 0 || msg.width <= 0 || msg.height <= 0 || !isName(f[2])) return msg;
        if (f[6] != "first" && f[6] != "second") return msg;
        msg.text  = f[2];
        msg.first = f[6] == "first";
        if (n == 8) msg.timeControl = f[7];
        msg.type = Type::Found;
        return msg;
    }

    if (cmd == "go" && (n == 4 || n == 6)) {
        static const QRegularExpression rxBoard(R"(^[0-4]+$)");
        msg.movesRemaining = number(f[2]);
        msg.gameId = number(f[3]);
        if (!rxBoard.match(f[1]).hasMatch() || msg.movesRemaining < 0 || msg.gameId < 0) return msg;
        msg.board = f[1];
        if (n == 6) {
            msg.myMs = number(f[4]);
            msg.opponentMs = number(f[5]);
            if (msg.myMs < 0 || msg.opponentMs < 0) return msg;
        }
        msg.type = Type::Go;
        return msg;
    }

    if ((cmd == "field1" || cmd == "field2") && n == 5) {
        msg.x = number(f[2]);
        msg.y = number(f[3]);
        msg.gameId = number(f[4]);
        if (msg.x < 0 || msg.y < 0 || msg.gameId < 0 || !isName(f[1])) return msg;
        msg.text = f[1];
        msg.type = cmd == "field2" ? Type::OwnMove : Type::OpponentMove;
        return msg;
    }

    if (cmd == "wrongstep" && n == 2) {
        msg.gameId = number(f[1]);
        if (msg.gameId >= 0) msg.type = Type::WrongStep;
        return msg;
    }

    if ((cmd == "win" || cmd == "lose") && (n == 2 || n == 3)) {
        msg.gameId = number(f[1]);
        if (msg.gameId < 0) return msg;
        msg.won  = cmd == "win";
        msg.text = n == 3 ? f[2] : QString();
        msg.type = Type::Result;
        return msg;
    }

    return msg;
}

QString loginLine(const QString& login, const QString& password, int width, int height,
                  const QString& preferredOpponent, const QString& timeControl)
{
    QString line = QString("mbclient:2:%1:%2:viruswar:%3:%4:")
        .arg(login, password).arg(width).arg(height);
    if (!timeControl.isEmpty())
        line += preferredOpponent + ':' + timeControl + ':';
    else if (!preferredOpponent.isEmpty())
        line += preferredOpponent + ':';
    return line;
}

}  // namespace Protocol
