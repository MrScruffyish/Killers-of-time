#include "client/MovePlanner.h"

#include "core/content/ContentRegistry.h"
#include "core/rules/Movement.h"
#include "core/rules/Pathfinding.h"

namespace kot::client {

void MovePlanner::prepare(const GameView& view, const core::ContentRegistry& content, int heroId) {
    m_content = &content;
    m_heroId = 0;
    m_today.clear();
    m_blocked.clear();
    m_unexplored.clear();
    m_danger.clear();
    m_dangerFrom.clear();
    const ViewHero* hero = view.hero(heroId);
    if (!hero || view.width <= 0 || view.terrainPalette.isEmpty()) return;
    m_heroId = heroId;
    m_from = hero->pos;
    m_movement = hero->movementPoints;
    const core::HeroClassDef* cls = content.heroClass(hero->classId);
    m_terrainPercent = cls ? content.race(cls->race).terrainCostPercent : QMap<QString, int>();

    m_map = core::GameMap(view.width, view.height, view.terrainPalette.first());
    for (int y = 0; y < view.height; ++y)
        for (int x = 0; x < view.width; ++x) {
            const int index = view.tiles.value(y * view.width + x, -1);
            if (index < 0) {  // never explored
                m_blocked.insert({x, y});
                m_unexplored.insert({x, y});
            }
            else m_map.setTerrain({x, y}, view.terrainPalette.value(index));
        }
    for (const ViewTown& t : view.towns)
        for (const core::Pos p : core::townTiles(t.pos, view.width, view.height)) m_blocked.insert(p);
    m_townGates.clear();
    for (const ViewTown& t : view.towns) m_townGates.append(t.pos);
    for (const ViewMine& m : view.mines) m_blocked.insert(m.pos);
    for (const ViewDwelling& d : view.dwellings) m_blocked.insert(d.pos);
    for (const ViewObject& o : view.objects) m_blocked.insert(o.pos);
    for (const ViewArmy& a : view.armies)
        if (a.id != hero->army) m_blocked.insert(a.pos);
    m_blocked.remove(m_from);

    // Zones of control of the hostile field armies the player can see.
    if (!hero->inBoat) {
        const ViewPlayer* me = view.player(view.you);
        const int radius = core::zoneOfControlRadius(hero->traits.value(QStringLiteral("scouting")), content);
        for (const ViewArmy& a : view.armies) {
            if (a.id == hero->army || view.townAt(a.pos)) continue;
            const ViewPlayer* owner = view.player(a.owner);
            if (a.owner != 0 && (!owner || !me || owner->team == me->team)) continue;
            const ViewHero* h = a.hero ? view.hero(a.hero) : nullptr;
            if (h && h->inBoat) continue;
            const auto inZone = [&](core::Pos p) { return std::max(std::abs(p.x - a.pos.x), std::abs(p.y - a.pos.y)) <= radius; };
            if (inZone(m_from)) continue;  // already next to it: no ambush when moving on
            for (int dy = -radius; dy <= radius; ++dy)
                for (int dx = -radius; dx <= radius; ++dx) {
                    const core::Pos p{a.pos.x + dx, a.pos.y + dy};
                    if (!m_map.inBounds(p) || p == a.pos) continue;
                    m_danger.insert(p);
                    m_dangerFrom[p].append(a.pos);
                }
        }
    }

    m_mode = hero->inBoat ? core::MoveMode::Boat : core::MoveMode::Foot;
    const core::MoveCostGrid grid(m_map, content, m_mode, m_terrainPercent);
    const QHash<core::Pos, int> reach =
        core::reachableTiles(grid, m_from, m_movement, [&](core::Pos p) { return m_blocked.contains(p); });
    for (auto it = reach.cbegin(); it != reach.cend(); ++it)
        if (it.key() != m_from) m_today.insert(it.key());
}

MovePlan MovePlanner::plan(core::Pos target) const {
    MovePlan plan;
    for (const core::Pos gate : m_townGates)
        if (core::townCovers(gate, target)) target = gate;  // walls lead to the gate
    if (!m_heroId || !m_map.inBounds(target) || target == m_from || m_unexplored.contains(target)) return plan;
    const core::MoveCostGrid grid(m_map, *m_content, m_mode, m_terrainPercent);
    // Around visible enemies when possible (as the server does), else straight on.
    const auto dangerous = [&](core::Pos p) {
        const auto it = m_dangerFrom.constFind(p);
        if (it == m_dangerFrom.cend() || p == target) return false;
        for (const core::Pos a : *it)
            if (a != target) return true;
        return false;
    };
    core::PathResult path = core::findPath(grid, m_from, target, [&](core::Pos p) { return m_blocked.contains(p) || dangerous(p); });
    if (!path.found) path = core::findPath(grid, m_from, target, [&](core::Pos p) { return m_blocked.contains(p); });
    if (!path.found || path.steps.isEmpty()) return plan;
    plan.found = true;
    plan.steps = path.steps;
    plan.affordable = core::affordableSteps(path, m_movement);
    plan.totalCost = path.cumulativeCost.last();
    for (int i = 0; i < plan.steps.size(); ++i) {
        const auto it = m_dangerFrom.constFind(plan.steps[i]);
        if (it == m_dangerFrom.cend()) continue;
        bool onlyTheTarget = true;
        for (const core::Pos a : *it) onlyTheTarget = onlyTheTarget && a == target;
        if (!onlyTheTarget) plan.ambushAt = i;
        break;
    }
    return plan;
}

}  // namespace kot::client
