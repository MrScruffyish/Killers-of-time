#pragma once

#include "core/model/GameState.h"

namespace kot::core {

class ContentRegistry;

// What a player earns at the start of each of his turns: gold from every owned
// town (balance.json) plus the daily amount of every owned mine.
ResourceAmounts dailyIncome(const GameState& state, const ContentRegistry& content, PlayerId player);

// Start of a player's turn: income (from day 2 on), heroes recover spell points,
// learn spells in a town with a mage guild and get their full movement, and the
// team's explored map is updated.
void startTurn(GameState& state, const ContentRegistry& content, PlayerId player);

// Starts the game: the first player in turn order begins day 1.
void beginGame(GameState& state, const ContentRegistry& content);

struct TurnInfo {
    PlayerId player;   // whose turn it is now
    int day = 0;
    bool newDay = false;
};

// Ends the current player's turn and starts the next one. When the last player
// has finished, a new day begins (turns are sequential, decision D1). Eliminated
// players (core/rules/Victory.h) are skipped.
TurnInfo endTurn(GameState& state, const ContentRegistry& content);

}  // namespace kot::core
