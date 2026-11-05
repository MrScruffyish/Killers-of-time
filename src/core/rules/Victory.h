#pragma once

#include "core/model/GameState.h"

namespace kot::core {

class ContentRegistry;

// Victory condition "last team standing" (PROJECT.md 5.13). A player is out by the
// game's defeat rule (GameState::defeatRule): "week" (the default) when he has
// been without a town for kTownlessGraceDays days (Player::townlessSince), "town"
// as soon as he has no town, "all" only with no towns and no heroes left (also
// out under every rule). The game is over when at most one team still has a player in it.
inline constexpr int kTownlessGraceDays = 7;
bool isEliminated(const GameState& state, PlayerId player);
// Days a player without a town has left to take one (1..7, rule "week"), -1 when
// he has a town, is out already or the rule has no countdown.
int townlessDaysLeft(const GameState& state, PlayerId player);
// At every turn change: notes who has just lost his last town (townlessSince) and
// who took one back, and takes the heroes of players whose time ran out off the map.
void updateTownless(GameState& state);

struct GameOutcome {
    bool over = false;
    TeamId winner;  // invalid when nobody is left (or the game is not over)
};
// Also the scenario's victory condition (GameState::victory): holding a town,
// reaching a gold amount, or the most towns when the last day is over.
GameOutcome gameOutcome(const GameState& state, const ContentRegistry& content);
// A short text for players: what wins this game.
QString describeVictory(const GameState& state);
// A short text for players: when a player is out.
QString describeDefeat(const GameState& state);

}  // namespace kot::core
