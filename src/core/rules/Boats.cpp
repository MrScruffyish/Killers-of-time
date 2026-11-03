#include "core/rules/Boats.h"

#include <optional>

#include "core/content/ContentRegistry.h"
#include "core/rules/Vision.h"

namespace kot::core {

std::optional<Pos> freeWaterNext(const GameState& state, const ContentRegistry& content, Pos p) {
    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx) {
            const Pos n{p.x + dx, p.y + dy};
            if ((!dx && !dy) || !state.map().inBounds(n)) continue;
            const TerrainDef* t = content.terrainType(state.map().terrainAt(n));
            if (!t || !t->sailable) continue;
            bool free = true;
            for (const Army& a : state.armies()) free = free && a.pos != n;
            if (free) return n;
        }
    return std::nullopt;
}

bool hasShipyard(const Town& town, const ContentRegistry& content) {
    for (const QString& b : town.buildings)
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
    for (auto it = cost.begin(); it != cost.end(); ++it)
        if (p->resources.value(it.key()) < it.value()) {
            r.error = QStringLiteral("not enough %1").arg(it.key());
            return r;
        }
    for (auto it = cost.begin(); it != cost.end(); ++it) p->resources[it.key()] -= it.value();
    army->pos = *water;
    hero->inBoat = true;
    updateExplored(state, content, state.teamOf(hero->owner));
    r.ok = true;
    return r;
}

}  // namespace kot::core
