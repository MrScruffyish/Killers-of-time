#pragma once

#include <QString>

#include "core/model/GameState.h"
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
};

// Moves a hero along the cheapest path toward `target`, as far as his
// movement points allow. Tiles holding another army, a town or a mine cannot
// be walked through, nor entered as the target (visiting them comes with the
// object interaction rules).
MoveResult moveHero(GameState& state, const ContentRegistry& content, HeroId heroId, Pos target,
                    MoveMode mode = MoveMode::Foot);

}  // namespace kot::core
