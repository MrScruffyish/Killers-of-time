#pragma once

#include <QByteArray>

#include "core/model/GameState.h"

namespace kot::core {

class ContentRegistry;

enum class Visibility { Unexplored, Explored, Visible };

// Tiles the team can see right now (one byte per tile, row-major, 1 = visible).
// Sources: heroes (radius = Scouting Radius trait), and towns and mines owned
// by any player of the team (radius from balance.json). Teammates share vision.
// Note: terrain that blocks vision is not taken into account yet.
QByteArray visibleTiles(const GameState& state, TeamId team, const ContentRegistry& content);

// Adds everything currently visible to the team's explored memory.
void updateExplored(GameState& state, const ContentRegistry& content, TeamId team);

// Unexplored (black), Explored (remembered but not visible now) or Visible.
// `visible` is the result of visibleTiles() for the same team.
Visibility tileVisibility(const GameState& state, TeamId team, const QByteArray& visible, Pos p);

}  // namespace kot::core
