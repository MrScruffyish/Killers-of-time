#pragma once

#include <QMap>
#include <QString>

#include "core/battle/Battle.h"
#include "core/model/GameState.h"

namespace kot::core {

class ContentRegistry;

struct BattleSide {
    PlayerId owner;  // invalid = neutral monsters
    HeroId hero;     // invalid = none
    QMap<UnitTypeId, int> losses;  // unit -> units killed
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
    bool retreated = false;   // the loser fled: army lost, hero alive
    QString retreatedTo;      // the town he fled to (the nearest own town without a hero), empty = stayed
    bool surrendered = false; // the loser surrendered: paid surrenderGold to the winner, kept hero and army
    UnitTypeId raisedUnit;    // the winner's race raised the fallen (undead) ...
    int raisedCount = 0;      // ... this many joined his army
    int lootGold = 0;         // the winner's race took this much gold (orcs)
    int surrenderGold = 0;

    bool attackerWon() const { return outcome == Outcome::AttackerWon; }
};

// Seed for a new battle, drawn from the game's Rng (keeps games deterministic).
std::uint64_t newBattleSeed(GameState& state);

// Writes a finished battle back into the game state: losses are removed, survivors
// heal fully (decision D18), the winner's hero gets experience (killed enemy HP *
// experiencePerHp) and the loser army is removed together with its hero. A town
// garrison that loses is only emptied (the town keeps its garrison army). A draw
// keeps both armies.
BattleReport applyBattleResult(GameState& state, const ContentRegistry& content, const Battle& battle);

// A whole battle at once: both sides are played by the battle AI (BattleAi.h) on
// the hex field, then the result is applied. Used when no human takes part.
// An army without units loses at once.
// The attacker pays balance.json siege.engineCost and gets a catapult for this
// battle. Returns why not (no walls, already built, cannot pay), else empty.
QString buySiegeEngine(GameState& state, const ContentRegistry& content, Battle& battle, QList<BattleEvent>* events = nullptr);

BattleReport resolveBattle(GameState& state, const ContentRegistry& content, ArmyId attackerArmy,
                           ArmyId defenderArmy);

// The JSON form sent to clients ("battle" reports in move/battle results).
QJsonObject battleReportToJson(const BattleReport& report);

}  // namespace kot::core
