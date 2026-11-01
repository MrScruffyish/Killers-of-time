#pragma once

#include "core/util/StrongId.h"

namespace kot::core {

struct PlayerTag;
struct TeamTag;
struct HeroTag;
struct TownTag;
struct MineTag;
struct ArmyTag;
struct MapDwellingTag;
struct UnitStackTag;

using PlayerId = StrongId<PlayerTag>;
using TeamId = StrongId<TeamTag>;
using HeroId = StrongId<HeroTag>;
using TownId = StrongId<TownTag>;
using MineId = StrongId<MineTag>;
using ArmyId = StrongId<ArmyTag>;
using MapDwellingId = StrongId<MapDwellingTag>;
using UnitStackId = StrongId<UnitStackTag>;

}  // namespace kot::core
