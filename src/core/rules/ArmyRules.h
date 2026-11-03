#pragma once

#include <QString>

#include "core/model/GameState.h"

namespace kot::core {

struct ActionResult {
    bool ok = false;
    QString error;
};

// Moves `count` units of stack `stackIndex` from one army to another. Both armies
// must have the same owner and stand on the same tile (a hero meeting his town
// garrison, two heroes side by side). Units join an existing stack of the same
// type or take a free slot (kMaxArmyStacks). Moving a whole stack keeps its wounds; moving
// part of a stack moves fresh units and the rest keeps the wounds.
ActionResult moveUnits(GameState& state, ArmyId from, ArmyId to, int stackIndex, int count);

// Moves every stack that fits (joining stacks of the same unit, else free slots);
// what does not fit stays. `moved` (optional) gets the number of units moved.
// Fails only when the armies cannot exchange at all or nothing could move.
ActionResult moveAllUnits(GameState& state, ArmyId from, ArmyId to, int* moved = nullptr);

}  // namespace kot::core
