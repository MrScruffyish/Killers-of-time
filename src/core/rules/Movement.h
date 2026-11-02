#pragma once

#include <QList>
#include <QString>
#include <optional>

#include "core/model/GameState.h"
#include "core/battle/AutoBattle.h"
#include "core/rules/Pathfinding.h"

namespace kot::core {

class ContentRegistry;

// Daily movement of an army: base + perTraitLevel * hero Movement Points trait,
// minus a small penalty for the slowest unit (see balance.json), never below
// the minimum. `hero` may be null (hero-less army).
int armyMovementPoints(const Army& army, const Hero* hero, const ContentRegistry& content);

// Gives every hero of `owner` his full daily movement (start of that player's turn).
void refreshHeroMovement(GameState& state, const ContentRegistry& content, PlayerId owner);

// An attack that stopped before its battles (see moveHero `deferBattles`).
struct PendingAttack {
    HeroId hero;
    PlayerId owner;
    ArmyId army;
    Pos target;
    QList<ArmyId> defenders;  // to fight in this order
};

struct MoveResult {
    bool ok = false;           // false: nothing happened, see `error`
    QString error;
    bool arrived = false;      // reached the target tile
    int stepsTaken = 0;
    int movementSpent = 0;
    MineId capturedMine;       // set when the hero arrived on a mine and took it
    TownId capturedTown;       // set when the hero arrived in a town and took it
    MapDwellingId capturedDwelling;  // set when the hero arrived at a map dwelling and took it
    bool attacked = false;     // the target held hostile armies and battles were fought
    QList<BattleReport> battles;  // in order; stops at the first one not won
    bool heroKilled = false;   // the moving hero lost a battle (he and his army are gone)
    std::optional<PendingAttack> pending;  // deferBattles: the battles still to fight
};

// Moves a hero along the cheapest path toward `target`, as far as his
// movement points allow. Tiles holding another army, a town or a mine cannot
// be walked through. A mine, map dwelling or town can be the target: arriving there visits it,
// and it is captured if it is neutral or belongs to another team.
// A target holding hostile armies (enemy heroes, neutral monsters, a defended
// town's garrison) is attacked when the hero can pay the whole path: he fights
// from the tile before (auto-resolved battles, core/battle/AutoBattle.h). A won
// town is entered and captured; after beating a field army the hero stays where
// he fought from. A friendly army on the target blocks it. The team's explored
// map is updated after the move.
// With `deferBattles` the hero stops next to the target and the battles are left to
// the caller (`pending`; interactive battles): fight them, then call finishAttack.
MoveResult moveHero(GameState& state, const ContentRegistry& content, HeroId heroId, Pos target,
                    MoveMode mode = MoveMode::Foot, bool deferBattles = false);

// After the battles of an attack: enters a won town, captures what is on the
// target and updates the explored map. `won` = every battle was won.
void finishAttack(GameState& state, const ContentRegistry& content, const PendingAttack& attack, bool won,
                  MoveResult* result);
// Takes over the mine, dwelling or town on `target` if it is not the team's.
void captureAt(GameState& state, PlayerId owner, Pos target, MoveResult* result);

}  // namespace kot::core
