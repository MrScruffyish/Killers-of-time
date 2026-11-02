#pragma once

#include <QString>

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
MoveResult moveHero(GameState& state, const ContentRegistry& content, HeroId heroId, Pos target,
                    MoveMode mode = MoveMode::Foot);

}  // namespace kot::core
