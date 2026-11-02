#pragma once

#include "core/battle/Battle.h"

namespace kot::core {

// A simple battle AI for the active unit: shoot the most dangerous enemy when it can,
// else attack the most dangerous enemy it can reach, else walk toward the nearest
// enemy. Used for monsters, AI players and the "auto" button. Deterministic.
BattleAction chooseBattleAction(const Battle& battle);

}  // namespace kot::core
