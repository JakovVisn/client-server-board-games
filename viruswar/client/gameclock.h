#pragma once

#include <QString>
#include <QtGlobal>

// Both players' clocks as the client shows them. The server is the source of truth: every
// go: resyncs them (sync), and between updates the running one is counted down locally.
// Time is passed in (a monotonic ms counter), so the class has no timers and is testable.
class GameClock
{
public:
    enum class Side { None, Mine, Opponent };

    // timeControl as in found: — turnN, gameN or gameN+I (seconds); anything else: no clock
    void start(const QString& timeControl, bool goFirst, qint64 nowMs);
    void reset();

    void sync(qint64 myMs, qint64 opponentMs, qint64 nowMs);  // from go: (my turn, my clock runs)
    void myTurnDone(qint64 nowMs);                            // my last sub-move was accepted
    void stop(qint64 nowMs);                                  // game over: freeze both

    [[nodiscard]] bool active() const { return active_; }
    [[nodiscard]] bool turnLimit() const { return turnLimit_; }  // turnN: only the mover has a limit
    [[nodiscard]] Side running() const { return running_; }
    [[nodiscard]] qint64 remaining(Side side, qint64 nowMs) const;

private:
    void switchTo(Side side, qint64 nowMs);

    bool active_ = false;
    bool turnLimit_ = false;
    qint64 limitMs_ = 0;       // turnN: per turn; gameN: starting clock
    qint64 incrementMs_ = 0;   // gameN+I
    qint64 myMs_ = 0;
    qint64 opponentMs_ = 0;
    Side running_ = Side::None;
    qint64 since_ = 0;         // when the running clock was last updated
};
