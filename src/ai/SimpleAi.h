#pragma once

#include <QHash>
#include <QSet>

#include "ai/IAiPlayer.h"
#include "core/util/Rng.h"

namespace kot::ai {

// First real AI (breadth first: simple, partly random). Each turn, in this order:
//  1. spend pending level-up picks (random battle trait),
//  2. build, then buy units in all towns (trading as needed),
//  3. hire a hero when rich and short of heroes,
//  4. heroes standing in an own town take the garrison along,
//  5. heroes cast a movement spell if they know one, then every hero heads for a target: a visible enemy or neutral thing it can take
//     (mines, dwellings, towns, weaker armies), else the edge of the explored map.
//     A few nearby candidates are chosen from at random.
//  6. end turn.
class SimpleAi : public IAiPlayer {
public:
    explicit SimpleAi(quint64 seed) : m_rng(seed) {}

    QString profileName() const override;
    void beginTurn(const core::GameState& state, const core::ContentRegistry& content,
                   core::PlayerId me) override;
    net::Message nextCommand(const core::GameState& state, const core::ContentRegistry& content,
                             core::PlayerId me) override;
    void commandDone(const net::Message& command, bool ok) override;

private:
    net::Message heroMove(const core::GameState& state, const core::ContentRegistry& content,
                          core::PlayerId me, const core::Hero& hero);

    core::Rng m_rng;
    bool m_built = false;
    bool m_hired = false;
    QSet<quint32> m_garrisonTaken;   // hero ids that took the garrison this turn
    QHash<quint32, int> m_moves;     // hero id -> move commands this turn
    QSet<quint32> m_heroDone;        // heroes with nothing more to do this turn
    QSet<quint32> m_spellCast;       // heroes that considered a movement spell this turn
};

}  // namespace kot::ai
