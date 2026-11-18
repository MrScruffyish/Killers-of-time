#pragma once

#include <QString>

#include "core/model/GameState.h"
#include "core/rules/ArmyRules.h"

namespace kot::core {

class ContentRegistry;

// Recruits `count` units from a map dwelling you own into an army that stands on
// the dwelling's tile (normally the visiting hero's army). Pays the unit cost.
ActionResult recruitAtDwelling(GameState& state, const ContentRegistry& content,
                               MapDwellingId dwelling, PlayerId player, int count, ArmyId army);

struct SendReport {
    int dwellingsEmptied = 0;
    int unitsSent = 0;
};

// "Send all units to this town": the waiting units of all your map dwellings move
// to the town, where they can be recruited at the normal price.
SendReport sendDwellingUnitsToTown(GameState& state, PlayerId player, TownId town);

// "Send all units to the closest town": each of your map dwellings sends its units to
// the nearest town you own (straight distance). Dwellings with no town stay as they are.
SendReport sendDwellingUnitsToClosestTown(GameState& state, PlayerId player);

}  // namespace kot::core
