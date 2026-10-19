#include "matchmaking.h"
#include <algorithm>
#include <map>
#include <set>
#include <utility>

namespace Matchmaking {

std::vector<Pairing> findPairings(const std::vector<Candidate>& c,
                                  const std::function<bool(int, int)>& compatible,
                                  const std::function<int(int, int)>& activeGames)
{
    const int n = static_cast<int>(c.size());
    std::vector<Pairing> pairings;
    std::vector<bool> busy(n, false);       // in a game that takes all their attention
    std::vector<int> autoGames(n);
    std::map<std::pair<int, int>, int> added;  // games paired in this run, per pair
    for (int i = 0; i < n; ++i)
        autoGames[i] = c[i].autoGames;

    auto pair = [&](int a, int b, bool autoMatch) {
        pairings.push_back({a, b, autoMatch});
        ++added[{std::min(a, b), std::max(a, b)}];
        if (!c[a].bot || autoMatch) busy[a] = true;  // humans and pass-3 seekers
        if (!c[b].bot) busy[b] = true;
        if (autoMatch) ++autoGames[b];
    };
    auto together = [&](int a, int b) {
        const auto it = added.find({std::min(a, b), std::max(a, b)});
        return activeGames(a, b) + (it == added.end() ? 0 : it->second);
    };

    // Pass 1: preferred opponents, at most one new game per requester
    std::set<int> matched;
    for (int i = 0; i < n; ++i) {
        if (busy[i] || matched.count(i)) continue;
        for (auto it = c[i].prefs.constBegin(); it != c[i].prefs.constEnd() && !matched.count(i); ++it) {
            for (int j = 0; j < n; ++j) {
                if (j == i || busy[j] || matched.count(j)) continue;
                if (c[j].login != it.key()) continue;
                if (!c[j].prefs.isEmpty() && !c[j].prefs.contains(c[i].login)) continue;
                if (!compatible(i, j)) continue;
                const int limit = std::min(it.value(), c[j].prefs.value(c[i].login, it.value()));
                if (together(i, j) >= limit) continue;

                pair(i, j, false);
                matched.insert(i);
                if (!c[j].bot)
                    matched.insert(j);
                break;
            }
        }
    }

    // Pass 2: random human vs human, for those with no preference
    for (int i = 0; i < n; ++i) {
        if (busy[i] || c[i].bot || !c[i].prefs.isEmpty()) continue;
        for (int j = i + 1; j < n; ++j) {
            if (busy[j] || c[j].bot || !c[j].prefs.isEmpty()) continue;
            if (!compatible(i, j)) continue;
            pair(i, j, false);
            break;
        }
    }

    // Pass 3: seekers against league bots with a free auto slot
    for (int i = 0; i < n; ++i) {
        if (busy[i] || !c[i].prefs.isEmpty() || c[i].autoLimit > 0) continue;
        for (int j = 0; j < n; ++j) {
            if (j == i || !c[j].bot || c[j].autoLimit <= 0) continue;
            if (autoGames[j] >= c[j].autoLimit) continue;
            if (c[j].login == c[i].login) continue;
            if (!compatible(i, j)) continue;
            pair(i, j, true);
            break;
        }
    }
    return pairings;
}

}  // namespace Matchmaking
