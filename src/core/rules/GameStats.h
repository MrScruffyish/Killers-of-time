#pragma once

#include <QJsonObject>

#include "core/model/GameState.h"

namespace kot::core {

class ContentRegistry;

// The numbers of every player now: armyValue (what all his units cost in gold),
// towns, mines, heroes, gold. {"<player id>": {...}, ...}
QJsonObject playerStats(const GameState& state, const ContentRegistry& content);
// Appends {"week": n, "players": playerStats} to the game's history (at the start of
// every week and of the game; once per week).
void recordWeekStats(GameState& state, const ContentRegistry& content);

}  // namespace kot::core
