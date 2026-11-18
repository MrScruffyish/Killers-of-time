#pragma once

#include "core/model/GameState.h"

namespace kot::core {

class ContentRegistry;

// Treasure chests: a hero who walks onto one finds it, and its player chooses
// gold or experience (no passwords, just a choice). The chest stays on the map
// until the choice is made, so the player may walk on and come back.
inline constexpr int kChestMinGold = 300;
inline constexpr int kChestMinExperience = 300;

// The gold a chest offers (in the currency resource).
int chestGold(const MapObject& chest, const ContentRegistry& content);

// Opens the chest for `hero`: `takeExperience` chooses the experience over the
// gold. Returns false when the chest is gone (or is not a chest).
bool openChest(GameState& state, const ContentRegistry& content, HeroId hero, MapObjectId chest,
               bool takeExperience);

}  // namespace kot::core
