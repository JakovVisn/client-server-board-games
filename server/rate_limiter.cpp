#include "rate_limiter.h"
#include <iterator>

RateLimiter::RateLimiter(int burst, qint64 refillMs)
    : burst_(burst), refillMs_(refillMs)
{
}

void RateLimiter::refill(Bucket& bucket, qint64 nowMs) const
{
    const qint64 earned = (nowMs - bucket.refilledAtMs) / refillMs_;
    if (bucket.tokens + earned >= burst_) {
        bucket.tokens = burst_;
        bucket.refilledAtMs = nowMs;
    } else {
        bucket.tokens += static_cast<int>(earned);
        bucket.refilledAtMs += earned * refillMs_;  // keep the part of a token already earned
    }
}

bool RateLimiter::allow(const QString& key, qint64 nowMs)
{
    auto it = buckets_.find(key);
    if (it == buckets_.end())
        it = buckets_.insert(key, {burst_, nowMs});
    refill(*it, nowMs);
    if (it->tokens == 0) return false;
    --it->tokens;
    return true;
}

void RateLimiter::prune(qint64 nowMs)
{
    for (auto it = buckets_.begin(); it != buckets_.end(); ) {
        refill(*it, nowMs);
        it = (it->tokens == burst_) ? buckets_.erase(it) : std::next(it);
    }
}
