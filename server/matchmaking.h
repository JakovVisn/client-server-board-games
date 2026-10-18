#pragma once

#include <functional>
#include <vector>
#include <QMap>
#include <QString>

// Who plays whom: a pure function of the waiting clients, so the rules can be unit-tested
// without sockets. Server::matchClients() builds the candidates and starts the games.
namespace Matchmaking {

struct Candidate
{
    QString login;
    bool bot = false;
    int autoLimit = 0;           // league bot: games it accepts from pass 3 (0 = none)
    int autoGames = 0;           // pass-3 games it is playing now
    QMap<QString, int> prefs;    // TARGET from mbclient: login -> max simultaneous games
};

struct Pairing
{
    int a;          // indexes into the candidates
    int b;
    bool autoMatch; // pass 3: b is the league bot whose auto slot this uses
};

// compatible(i, j): same game, board size and time control (Server::sameGame).
// activeGames(i, j): games i and j are playing against each other now.
//
// Pass 1, preferred opponents: i lists j's login, j agrees (no preferences of its own, or
//   lists i), and they play fewer than the limit together.
// Pass 2, human vs human: humans without preferences.
// Pass 3, seekers vs league bots: clients without preferences and without an auto limit,
//   against bots with a free auto slot.
// Humans and pass-3 seekers play one game at a time; other bots can be paired many times.
std::vector<Pairing> findPairings(const std::vector<Candidate>& candidates,
                                  const std::function<bool(int, int)>& compatible,
                                  const std::function<int(int, int)>& activeGames);

}  // namespace Matchmaking
