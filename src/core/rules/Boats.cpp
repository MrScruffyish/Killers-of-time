#include "core/rules/Boats.h"

#include <optional>

#include "core/content/ContentRegistry.h"
#include "core/rules/Vision.h"

namespace kot::core {

std::optional<Pos> freeWaterNext(const GameState& state, const ContentRegistry& content, Pos p) {
    // The nearest free water within two tiles.
    for (int ring = 1; ring <= 2; ++ring)
    for (int dy = -ring; dy <= ring; ++dy)
        for (int dx = -ring; dx <= ring; ++dx) {
            if (std::max(std::abs(dx), std::abs(dy)) != ring) continue;
            const Pos n{p.x + dx, p.y + dy};
            if ((!dx && !dy) || !state.map().inBounds(n)) continue;
            const TerrainDef* t = &content.terrain(state.map().terrainAt(n));
            if (!t || !t->sailable) continue;
            bool free = true;
            for (const Army& a : state.armies()) free = free && a.pos != n;
            if (free) return n;
        }
    return std::nullopt;
}

bool coastalAt(const GameState& state, const ContentRegistry& content, Pos p) {
    for (int dy = -2; dy <= 2; ++dy)
        for (int dx = -2; dx <= 2; ++dx) {
            const Pos n{p.x + dx, p.y + dy};
            if ((!dx && !dy) || !state.map().inBounds(n)) continue;
            const TerrainDef* t = &content.terrain(state.map().terrainAt(n));
            if (t && t->sailable) return true;
        }
    return false;
}

bool hasShipyard(const Town& town, const ContentRegistry& content) {
    for (const BuildingTypeId b : town.buildings)
        if (const BuildingDef* def = content.building(b))
            for (const BuildingEffect& e : def->effects)
                if (e.type == BuildingEffect::Type::Shipyard) return true;
    return false;
}

ActionResult buyBoat(GameState& state, const ContentRegistry& content, HeroId heroId) {
    ActionResult r;
    Hero* hero = state.hero(heroId);
    Army* army = hero ? state.army(hero->army) : nullptr;
    if (!army) {
        r.error = QStringLiteral("unknown hero");
        return r;
    }
    if (hero->inBoat) {
        r.error = QStringLiteral("the hero is already in a boat");
        return r;
    }
    const Town* town = nullptr;
    for (const Town& t : state.towns())
        if (t.pos == army->pos && !state.isHostile(t.owner, hero->owner)) town = &t;
    if (!town || !hasShipyard(*town, content)) {
        r.error = QStringLiteral("the hero must be in a town with a shipyard");
        return r;
    }
    const std::optional<Pos> water = freeWaterNext(state, content, town->pos);
    if (!water) {
        r.error = QStringLiteral("there is no free water next to the town");
        return r;
    }
    Player* p = state.player(hero->owner);
    const ResourceAmounts& cost = content.balance().economy.boatCost;
    for (const auto [res, amount] : cost)
        if (p->resources.value(res) < amount) {
            r.error = QStringLiteral("not enough %1").arg(content.resource(res).key);
            return r;
        }
    for (const auto [res, amount] : cost) p->resources[res] -= amount;
    army->pos = *water;
    hero->inBoat = true;
    updateExplored(state, content, state.teamOf(hero->owner));
    r.ok = true;
    return r;
}

}  // namespace kot::core
