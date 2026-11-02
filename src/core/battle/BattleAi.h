#pragma once

#include "core/battle/Battle.h"

namespace kot::core {

// A simple battle AI for the active unit. Its hero (once per round) casts the strongest
// damage spell he can at the most dangerous enemy. The stack shoots the most dangerous enemy when it can,
// else attack the most dangerous enemy it can reach, else walk toward the nearest
// enemy. Used for monsters, AI players and the "auto" button. Deterministic.
BattleAction chooseBattleAction(const Battle& battle);

}  // namespace kot::core
