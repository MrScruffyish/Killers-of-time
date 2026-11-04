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
// Also the scenario's victory condition (GameState::victory): holding a town,
// reaching a gold amount, or the most towns when the last day is over.
GameOutcome gameOutcome(const GameState& state);
// A short text for players: what wins this game.
QString describeVictory(const GameState& state);

}  // namespace kot::core
