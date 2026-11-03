#pragma once

#include <QByteArray>

#include "core/model/GameState.h"

namespace kot::core {

class ContentRegistry;

enum class Visibility { Unexplored, Explored, Visible };

// Tiles the team can see right now (one byte per tile, row-major, 1 = visible).
// Sources: heroes (radius = Scouting Radius trait), and towns and mines owned
// by any player of the team (radius from balance.json). Teammates share vision.
// Note: terrain that blocks vision is not taken into account yet. Without fog of
// war (GameState::fogOfWar) every tile is visible to everyone.
QByteArray visibleTiles(const GameState& state, TeamId team, const ContentRegistry& content);

// How far a hero sees: Scouting Radius plus the Scouting trait's bonus.
int heroVisionRadius(const Hero& hero, const ContentRegistry& content);
// The roughly round area a radius covers (the same shape visibleTiles uses).
inline bool withinVision(Pos centre, Pos p, int radius) {
    radius = radius < 1 ? 1 : radius;
    const int dx = p.x - centre.x, dy = p.y - centre.y;
    return dx >= -radius && dx <= radius && dy >= -radius && dy <= radius && dx * dx + dy * dy <= radius * radius + radius;
}

// Adds everything currently visible to the team's explored memory.
void updateExplored(GameState& state, const ContentRegistry& content, TeamId team);

// Unexplored (black), Explored (remembered but not visible now) or Visible.
// `visible` is the result of visibleTiles() for the same team.
Visibility tileVisibility(const GameState& state, TeamId team, const QByteArray& visible, Pos p);

}  // namespace kot::core
