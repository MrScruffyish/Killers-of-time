#pragma once

#include <optional>

#include "core/model/GameState.h"
#include "core/rules/ArmyRules.h"

namespace kot::core {

class ContentRegistry;

// The nearest water tile (within two tiles of `p`) a boat can be put on (sailable,
// nobody there), if any.
std::optional<Pos> freeWaterNext(const GameState& state, const ContentRegistry& content, Pos p);
bool hasShipyard(const Town& town, const ContentRegistry& content);

// A hero standing in his town with a shipyard buys a boat (balance.json economy
// boatCost) and boards it on the water next to the town; he keeps his movement.
ActionResult buyBoat(GameState& state, const ContentRegistry& content, HeroId hero);

}  // namespace kot::core
