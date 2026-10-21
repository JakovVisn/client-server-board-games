#pragma once

#include <QHash>
#include <QString>

// A token bucket per key: up to burst attempts at once, then one more every refillMs.
// Time is passed in, so the rules are unit-tested without waiting
class RateLimiter
{
public:
    RateLimiter(int burst, qint64 refillMs);

    [[nodiscard]] bool allow(const QString& key, qint64 nowMs);
    // Forgets keys whose bucket is full again, so the table doesn't grow without bound
    void prune(qint64 nowMs);
    [[nodiscard]] int size() const { return static_cast<int>(buckets_.size()); }

private:
    struct Bucket
    {
        int tokens;
        qint64 refilledAtMs;
    };

    void refill(Bucket& bucket, qint64 nowMs) const;

    int burst_;
    qint64 refillMs_;
    QHash<QString, Bucket> buckets_;
};
