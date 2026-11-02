#pragma once

#include <QMap>
#include <QString>

#include "core/model/GameState.h"

namespace kot::core {

class ContentRegistry;

// Thin first version of battles (breadth first): the whole battle is resolved at
// once by the rules, without a battle field. The hex battle with a UI comes later
// (M6); this keeps the same damage idea so the numbers carry over.
//
// Each round every living stack acts once, by initiative (then battle speed, the
// attacker first on ties). It strikes the most dangerous enemy stack. A melee
// strike (or a ranged unit without shots) gets one retaliation per round from the
// target. Ranged units with shots left shoot without retaliation.
//
// Damage = count * roll(damageMin..damageMax), times
//   1 + attackBonusPercent% per point of (attack - defense) when positive (capped),
//   1 - defenseBonusPercent% per point when negative (capped);
// hero Attack/Defense traits add to the unit values. At least 1 damage.
struct BattleSide {
    PlayerId owner;  // invalid = neutral monsters
    HeroId hero;     // invalid = none
    QMap<QString, int> losses;  // unit id -> units killed
};

struct BattleReport {
    enum class Outcome { AttackerWon, DefenderWon, Draw };
    Outcome outcome = Outcome::Draw;
    int rounds = 0;
    BattleSide attacker;
    BattleSide defender;
    int experience = 0;       // given to the winner's hero
    int levelsGained = 0;
    bool loserHeroKilled = false;

    bool attackerWon() const { return outcome == Outcome::AttackerWon; }
};

// Fights `attackerArmy` against `defenderArmy` and applies the result to the state:
// losses are removed, survivors heal fully (decision D18), the winner's hero gets
// experience (killed enemy HP * experiencePerHp) and the loser army is removed
// together with its hero. A town garrison that loses is only emptied (the town
// keeps its garrison army). A draw (round limit) keeps both armies.
// An army without units loses at once.
BattleReport resolveBattle(GameState& state, const ContentRegistry& content, ArmyId attackerArmy,
                           ArmyId defenderArmy);

// The JSON form sent to clients ("battle" in a move_hero result).
QJsonObject battleReportToJson(const BattleReport& report);

}  // namespace kot::core
