#pragma once

#include <QString>

#include "core/model/GameState.h"

namespace kot::core {

class ContentRegistry;

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

// A rough fighting value: per unit HP x average damage x (1 + 5% per attack and
// defense point), times 5% per point of the hero's Attack and Defense.
double armyStrength(const GameState& state, const ContentRegistry& content, const Army& army);

// Neutral monsters (no owner, no hero) that are at least joinStrengthRatio times
// weaker than the hero's army and all fit into it would rather join than fight.
bool wouldJoin(const GameState& state, const ContentRegistry& content, ArmyId heroArmy, ArmyId monsters);
// What the monsters ask for joining: joinCostPercent of their recruit price in gold.
int joinCost(const GameState& state, const ContentRegistry& content, ArmyId monsters);
// The monsters join the hero's army (his owner pays joinCost) and leave the map.
ActionResult acceptJoin(GameState& state, const ContentRegistry& content, HeroId hero, ArmyId monsters);

}  // namespace kot::core
