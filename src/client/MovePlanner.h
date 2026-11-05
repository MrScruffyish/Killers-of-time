#pragma once

#include <QHash>
#include <QList>
#include <QSet>

#include "client/ViewModel.h"
#include "core/model/GameMap.h"
#include "core/rules/Pathfinding.h"

namespace kot::core { class ContentRegistry; }

namespace kot::client {

// The planned way of the selected hero, worked out on the client with the same
// pathfinding as the server, on what the player can see (unexplored tiles and
// objects block, a target object may be the last step). Only a preview: the
// server decides.
struct MovePlan {
    bool found = false;
    QList<core::Pos> steps;
    int affordable = 0;   // steps the hero can walk this turn
    int totalCost = 0;    // in hundredths of a movement point
    int ambushAt = -1;    // step where a visible monster or enemy attacks (zone of control), -1 = none
};

class MovePlanner {
public:
    // Prepares for `heroId` (no hero: plans nothing).
    void prepare(const GameView& view, const core::ContentRegistry& content, int heroId);
    MovePlan plan(core::Pos target) const;
    // Tiles the hero can still reach this turn.
    const QSet<core::Pos>& reachableToday() const { return m_today; }
    // Tiles where a visible hostile army would attack the hero (zone of control).
    const QSet<core::Pos>& dangerTiles() const { return m_danger; }
    bool ready() const { return m_heroId != 0; }

private:
    const core::ContentRegistry* m_content = nullptr;
    int m_heroId = 0;
    core::Pos m_from;
    int m_movement = 0;
    core::MoveMode m_mode = core::MoveMode::Foot;
    core::GameMap m_map;
    QVector<int> m_terrainPercent;
    QSet<core::Pos> m_blocked;
    QSet<core::Pos> m_unexplored;
    QSet<core::Pos> m_today;
    QSet<core::Pos> m_danger;
    QList<core::Pos> m_townGates;
    QHash<core::Pos, QList<core::Pos>> m_dangerFrom;  // danger tile -> armies whose zone it is in
};

}  // namespace kot::client
