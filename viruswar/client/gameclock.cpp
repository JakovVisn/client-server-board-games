#include "gameclock.h"
#include <QRegularExpression>

void GameClock::start(const QString& timeControl, bool goFirst, qint64 nowMs)
{
    static const QRegularExpression rx(R"(^(turn|game)(\d+)(?:\+(\d+))?$)");
    const auto m = rx.match(timeControl);
    if (!m.hasMatch()) {
        reset();
        return;
    }
    active_      = true;
    turnLimit_   = m.captured(1) == "turn";
    limitMs_     = m.captured(2).toLongLong() * 1000;
    incrementMs_ = m.captured(3).toLongLong() * 1000;
    myMs_ = opponentMs_ = limitMs_;
    running_ = goFirst ? Side::Mine : Side::Opponent;
    since_ = nowMs;
}

void GameClock::reset()
{
    *this = GameClock();
}

void GameClock::sync(qint64 myMs, qint64 opponentMs, qint64 nowMs)
{
    if (!active_) {  // times without a known time control: show them as game clocks
        active_ = true;
        turnLimit_ = false;
    }
    myMs_ = myMs;
    opponentMs_ = opponentMs;
    running_ = Side::Mine;
    since_ = nowMs;
}

void GameClock::myTurnDone(qint64 nowMs)
{
    switchTo(Side::Opponent, nowMs);
}

void GameClock::stop(qint64 nowMs)
{
    switchTo(Side::None, nowMs);
}

qint64 GameClock::remaining(Side side, qint64 nowMs) const
{
    const qint64 base = side == Side::Mine ? myMs_ : opponentMs_;
    if (side != running_)
        return base;
    return qMax<qint64>(0, base - (nowMs - since_));
}

void GameClock::switchTo(Side side, qint64 nowMs)
{
    if (!active_ || running_ == side)
        return;
    const qint64 spent = nowMs - since_;
    if (running_ == Side::Mine) {
        myMs_ = turnLimit_ ? limitMs_ : qMax<qint64>(0, myMs_ - spent);
        if (side == Side::Opponent && !turnLimit_)
            myMs_ += incrementMs_;  // my turn is complete
    } else if (running_ == Side::Opponent) {
        opponentMs_ = turnLimit_ ? limitMs_ : qMax<qint64>(0, opponentMs_ - spent);
    }
    running_ = side;
    since_ = nowMs;
}
