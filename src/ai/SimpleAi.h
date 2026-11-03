#pragma once

#include <QHash>
#include <QSet>

#include "ai/IAiPlayer.h"
#include "core/util/Rng.h"

namespace kot::ai {

// The standard AI (simple but purposeful). Each turn, in this order:
//  1. level-up picks that fit the hero's class (might or magic traits),
//  2. units from captured map dwellings are sent to the closest town,
//  3. build, then buy units in all towns (trading as needed),
//  4. hire heroes while rich enough (more as the game goes on),
//  5. heroes in an own town take the garrison along, unless the town is threatened,
//  6. movement spells, then every hero goes for the best target (heroes with nothing
//     left on their island buy a boat at a shipyard and sail to other shores):
//     value / distance over defending a threatened town, picking up reinforcements,
//     enemy and neutral things it can take (towns, mines, artifacts, treasures,
//     dwellings, weaker armies; never into a clearly stronger army) and exploring,
//  7. end turn.
// It only looks at what its team can see (AiView) and uses its own seeded RNG.
// Weights that make AI personalities (profiles "simple", "aggressive", "economic",
// "explorer").
struct AiPersonality {
    QString name = QStringLiteral("simple");
    double attackMargin = 1.3;   // attack armies this much weaker than us
    double townValue = 1.0;      // weights on the target values
    double armyValue = 1.0;
    double mineValue = 1.0;
    double treasureValue = 1.0;
    double exploreValue = 1.0;
    int extraHeroes = 0;         // hire more (or fewer) heroes than usual
};
AiPersonality personalityFor(const QString& profile);

class SimpleAi : public IAiPlayer {
public:
    explicit SimpleAi(quint64 seed, AiPersonality personality = {}) : m_rng(seed), m_p(std::move(personality)) {}

    QString profileName() const override;
    void beginTurn(const core::GameState& state, const core::ContentRegistry& content,
                   core::PlayerId me) override;
    net::Message nextCommand(const core::GameState& state, const core::ContentRegistry& content,
                             core::PlayerId me) override;
    void commandDone(const net::Message& command, bool ok) override;

private:
    net::Message heroMove(const core::GameState& state, const core::ContentRegistry& content,
                          core::PlayerId me, const core::Hero& hero);
    net::Message boatPlan(const core::GameState& state, const core::ContentRegistry& content, core::PlayerId me,
                          const core::Hero& hero);
    net::Message sailMove(const core::GameState& state, const core::ContentRegistry& content, core::PlayerId me,
                          const core::Hero& hero);

    core::Rng m_rng;
    AiPersonality m_p;
    bool m_built = false;
    bool m_hired = false;
    bool m_sentUnits = false;
    QSet<quint32> m_garrisonTaken;   // hero ids that took the garrison this turn
    QHash<quint32, int> m_moves;     // hero id -> move commands this turn
    QSet<quint32> m_heroDone;        // heroes with nothing more to do this turn
    QSet<quint32> m_resting;  // heroes that stopped short of their destination today
    QList<QSet<core::Pos>> m_visitedIslands;  // islands left by boat (not landed on again)
    bool m_savingForBoat = false;  // the island is done: keep gold and wood for a boat (kept across turns)
    QSet<quint32> m_spellCast;       // heroes that considered a movement spell this turn
    QSet<std::uint64_t> m_failedTargets;  // hero/target pairs refused this turn
    std::uint64_t m_lastTarget = 0;
    QSet<quint32> m_boatTried;                // heroes that bought a boat this turn
    QHash<quint32, QSet<core::Pos>> m_homeLand;  // the island a hero sailed from
};

}  // namespace kot::ai
