#pragma once

#include <QHash>
#include <QList>
#include <QMap>
#include <QString>
#include <functional>

#include "core/model/GameMap.h"

namespace kot::core {

class ContentRegistry;

// How an army travels: on foot over walkable terrain, or by boat over
// sailable terrain (boarding/disembarking comes with shipyards later).
enum class MoveMode { Foot, Boat };

// Moves are in 8 directions. A diagonal step costs 1.41 times the cost of the
// tile entered (rounded).
int stepCost(int terrainMoveCost, bool diagonal);

// Movement cost per tile for one MoveMode, computed once and reused for many
// path searches. cost() is -1 for tiles that cannot be entered in this mode.
class MoveCostGrid {
public:
    // `terrainPercent`: terrain id -> percent of the normal cost (race bonuses).
    MoveCostGrid(const GameMap& map, const ContentRegistry& content, MoveMode mode,
                 const QMap<QString, int>& terrainPercent = {});

    int width() const { return m_width; }
    int height() const { return m_height; }
    bool inBounds(Pos p) const { return p.x >= 0 && p.y >= 0 && p.x < m_width && p.y < m_height; }
    int cost(Pos p) const { return m_costs[p.y * m_width + p.x]; }
    // Cheapest enterable tile (used by the search heuristic); 1 if none.
    int minCost() const { return m_minCost; }

private:
    int m_width = 0;
    int m_height = 0;
    int m_minCost = 1;
    QList<int> m_costs;
};

// Tiles for which this returns true cannot be walked through (other armies,
// towns, ...). The goal tile itself is exempt, so a path can end ON an object.
using BlockedFn = std::function<bool(Pos)>;

struct PathResult {
    bool found = false;
    QList<Pos> steps;            // tiles to enter in order, excluding the start
    QList<int> cumulativeCost;   // cumulativeCost[i] = total cost after steps[i]
    int totalCost = 0;
};

// A* search for the cheapest path. Deterministic (ties are broken by tile index).
PathResult findPath(const MoveCostGrid& grid, Pos from, Pos to, const BlockedFn& blocked = {});

// All tiles reachable with at most `maxCost` movement, with their cost
// (includes `from` with cost 0). Blocked tiles are not reachable.
QHash<Pos, int> reachableTiles(const MoveCostGrid& grid, Pos from, int maxCost,
                               const BlockedFn& blocked = {});

// How many steps of the path can be taken with `movementPoints`. A step is
// only taken if its full cost can be paid.
int affordableSteps(const PathResult& path, int movementPoints);

}  // namespace kot::core
