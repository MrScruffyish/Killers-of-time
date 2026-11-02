#pragma once

#include "core/model/GameState.h"

namespace kot::core {

// Victory condition "last team standing" (PROJECT.md 5.13). Nothing is stored:
// a player is out when he has no towns and no heroes left, and the game is over
// when at most one team still has a player in it.
bool isEliminated(const GameState& state, PlayerId player);

struct GameOutcome {
    bool over = false;
    TeamId winner;  // invalid when nobody is left (or the game is not over)
};
GameOutcome gameOutcome(const GameState& state);

}  // namespace kot::core
