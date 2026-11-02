#include "core/rules/Pathfinding.h"

#include <algorithm>
#include <climits>
#include <queue>

#include "core/content/ContentRegistry.h"

namespace kot::core {

namespace {

constexpr int kDx[8] = {0, 1, 1, 1, 0, -1, -1, -1};
constexpr int kDy[8] = {-1, -1, 0, 1, 1, 1, 0, -1};

struct Node {
    int f;
    int idx;
    int g;
    bool operator>(const Node& o) const { return f != o.f ? f > o.f : idx > o.idx; }
};
using MinQueue = std::priority_queue<Node, std::vector<Node>, std::greater<Node>>;

}  // namespace

int stepCost(int terrainMoveCost, bool diagonal) {
    return diagonal ? (terrainMoveCost * 141 + 50) / 100 : terrainMoveCost;
}

MoveCostGrid::MoveCostGrid(const GameMap& map, const ContentRegistry& content, MoveMode mode,
                           const QMap<QString, int>& terrainPercent)
    : m_width(map.width()), m_height(map.height()) {
    QHash<QString, int> byTerrain;
    m_costs.reserve(map.tiles().size());
    int minCost = INT_MAX;
    for (const QString& id : map.tiles()) {
        auto it = byTerrain.find(id);
        if (it == byTerrain.end()) {
            const TerrainDef* t = content.terrainType(id);
            int c = -1;
            if (t) {
                const bool enterable = (mode == MoveMode::Foot) ? t->walkable : t->sailable;
                if (enterable) c = std::max(1, t->moveCost * terrainPercent.value(id, 100) / 100);
            }
            it = byTerrain.insert(id, c);
        }
        m_costs.append(it.value());
        if (it.value() >= 0) minCost = std::min(minCost, it.value());
    }
    m_minCost = (minCost == INT_MAX) ? 1 : std::max(1, minCost);
}

PathResult findPath(const MoveCostGrid& grid, Pos from, Pos to, const BlockedFn& blocked) {
    PathResult result;
    if (!grid.inBounds(from) || !grid.inBounds(to)) return result;
    if (grid.cost(to) < 0) return result;
    if (from == to) {
        result.found = true;
        return result;
    }

    const int w = grid.width();
    const int n = w * grid.height();
    const int start = from.y * w + from.x;
    const int goal = to.y * w + to.x;
    QList<int> best(n, INT_MAX);
    QList<int> parent(n, -1);

    // Chebyshev distance times the cheapest tile cost never overestimates.
    const auto heuristic = [&](int idx) {
        const int dx = std::abs(idx % w - to.x);
        const int dy = std::abs(idx / w - to.y);
        return grid.minCost() * std::max(dx, dy);
    };

    MinQueue queue;
    best[start] = 0;
    queue.push({heuristic(start), start, 0});
    while (!queue.empty()) {
        const Node cur = queue.top();
        queue.pop();
        if (cur.g > best[cur.idx]) continue;
        if (cur.idx == goal) break;
        const int cx = cur.idx % w;
        const int cy = cur.idx / w;
        for (int d = 0; d < 8; ++d) {
            const Pos np{cx + kDx[d], cy + kDy[d]};
            if (!grid.inBounds(np)) continue;
            const int c = grid.cost(np);
            if (c < 0) continue;
            const int nidx = np.y * w + np.x;
            if (nidx != goal && blocked && blocked(np)) continue;
            const int g = cur.g + stepCost(c, kDx[d] != 0 && kDy[d] != 0);
            if (g < best[nidx]) {
                best[nidx] = g;
                parent[nidx] = cur.idx;
                queue.push({g + heuristic(nidx), nidx, g});
            }
        }
    }
    if (best[goal] == INT_MAX) return result;

    QList<int> reversed;
    for (int i = goal; i != start; i = parent[i]) reversed.append(i);
    std::reverse(reversed.begin(), reversed.end());
    for (int idx : reversed) {
        result.steps.append(Pos{idx % w, idx / w});
        result.cumulativeCost.append(best[idx]);
    }
    result.found = true;
    result.totalCost = best[goal];
    return result;
}

QHash<Pos, int> reachableTiles(const MoveCostGrid& grid, Pos from, int maxCost,
                               const BlockedFn& blocked) {
    QHash<Pos, int> reached;
    if (!grid.inBounds(from) || grid.cost(from) < 0) return reached;
    const int w = grid.width();
    QList<int> best(w * grid.height(), INT_MAX);
    MinQueue queue;
    const int start = from.y * w + from.x;
    best[start] = 0;
    queue.push({0, start, 0});
    while (!queue.empty()) {
        const Node cur = queue.top();
        queue.pop();
        if (cur.g > best[cur.idx]) continue;
        reached.insert(Pos{cur.idx % w, cur.idx / w}, cur.g);
        for (int d = 0; d < 8; ++d) {
            const Pos np{cur.idx % w + kDx[d], cur.idx / w + kDy[d]};
            if (!grid.inBounds(np)) continue;
            const int c = grid.cost(np);
            if (c < 0 || (blocked && blocked(np))) continue;
            const int g = cur.g + stepCost(c, kDx[d] != 0 && kDy[d] != 0);
            const int nidx = np.y * w + np.x;
            if (g <= maxCost && g < best[nidx]) {
                best[nidx] = g;
                queue.push({g, nidx, g});
            }
        }
    }
    return reached;
}

int affordableSteps(const PathResult& path, int movementPoints) {
    int steps = 0;
    while (steps < path.cumulativeCost.size() && path.cumulativeCost[steps] <= movementPoints)
        ++steps;
    return steps;
}

}  // namespace kot::core
