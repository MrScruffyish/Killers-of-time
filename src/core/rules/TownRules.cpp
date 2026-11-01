#include "core/rules/TownRules.h"

#include <QSet>

#include "core/content/ContentRegistry.h"

namespace kot::core {

namespace {

ActionResult fail(const QString& message) {
    ActionResult r;
    r.error = message;
    return r;
}

QString dwellingUnit(const BuildingDef& b) {
    for (const BuildingEffect& e : b.effects) {
        if (e.type == BuildingEffect::Type::Dwelling) return e.unit;
    }
    return {};
}

QString missingResource(const ResourceAmounts& have, const ResourceAmounts& cost, int times = 1) {
    for (auto it = cost.begin(); it != cost.end(); ++it) {
        if (have.value(it.key()) < it.value() * times) return it.key();
    }
    return {};
}

void spend(ResourceAmounts& have, const ResourceAmounts& cost, int times = 1) {
    for (auto it = cost.begin(); it != cost.end(); ++it) have[it.key()] -= it.value() * times;
}

}  // namespace

QList<Dwelling> dwellings(const Town& town, const ContentRegistry& content) {
    QSet<QString> built(town.buildings.begin(), town.buildings.end());
    QSet<QString> replaced;
    for (const QString& id : town.buildings) {
        const BuildingDef* b = content.building(id);
        if (b && !b->upgradeOf.isEmpty()) replaced.insert(b->upgradeOf);
    }
    QList<Dwelling> result;
    for (const QString& id : town.buildings) {
        const BuildingDef* b = content.building(id);
        if (!b || replaced.contains(id)) continue;
        const QString unit = dwellingUnit(*b);
        if (unit.isEmpty()) continue;
        QString root = id;
        for (int guard = 0; guard < 16; ++guard) {
            const BuildingDef* r = content.building(root);
            if (!r || r->upgradeOf.isEmpty()) break;
            root = r->upgradeOf;
        }
        result.append({root, id, unit});
    }
    return result;
}

QString whyCannotBuild(const GameState& state, const ContentRegistry& content, TownId townId,
                       const QString& buildingId, PlayerId player) {
    const Town* town = state.town(townId);
    if (!town) return QStringLiteral("unknown town");
    if (!player.isValid() || town->owner != player)
        return QStringLiteral("this is not your town");
    const BuildingDef* b = content.building(buildingId);
    if (!b) return QStringLiteral("unknown building '%1'").arg(buildingId);
    if (b->race != town->race)
        return QStringLiteral("%1 cannot be built in a %2 town").arg(b->name, town->race);
    if (town->buildings.contains(buildingId)) return QStringLiteral("already built");
    if (town->lastBuildDay == state.day())
        return QStringLiteral("a building was already built here today");

    QStringList needed = b->requiredBuildings;
    if (!b->upgradeOf.isEmpty()) needed.append(b->upgradeOf);
    for (const QString& req : needed) {
        if (!town->buildings.contains(req)) {
            const BuildingDef* rb = content.building(req);
            return QStringLiteral("requires %1").arg(rb ? rb->name : req);
        }
    }
    for (const QString& other : town->buildings) {
        const BuildingDef* ob = content.building(other);
        if (b->excludes.contains(other) || (ob && ob->excludes.contains(buildingId)))
            return QStringLiteral("excluded by %1").arg(ob ? ob->name : other);
    }
    const Player* p = state.player(player);
    const QString missing = p ? missingResource(p->resources, b->cost) : QString();
    if (!missing.isEmpty()) return QStringLiteral("not enough %1").arg(missing);
    return {};
}

ActionResult buildBuilding(GameState& state, const ContentRegistry& content, TownId townId,
                           const QString& buildingId, PlayerId player) {
    const QString why = whyCannotBuild(state, content, townId, buildingId, player);
    if (!why.isEmpty()) return fail(why);

    Town* town = state.town(townId);
    const BuildingDef* b = content.building(buildingId);
    spend(state.player(player)->resources, b->cost);
    town->buildings.append(buildingId);
    town->lastBuildDay = state.day();

    // A new dwelling starts with its first week of units.
    for (const Dwelling& d : dwellings(*town, content)) {
        if (d.buildingId != buildingId || town->recruitPool.contains(d.poolKey)) continue;
        const UnitDef* unit = content.unit(d.unitId);
        town->recruitPool[d.poolKey] = unit ? unit->weeklyGrowth : 0;
    }
    ActionResult ok;
    ok.ok = true;
    return ok;
}

ActionResult recruit(GameState& state, const ContentRegistry& content, TownId townId,
                     PlayerId player, const QString& unitId, int count, ArmyId target) {
    Town* town = state.town(townId);
    if (!town) return fail(QStringLiteral("unknown town"));
    if (!player.isValid() || town->owner != player)
        return fail(QStringLiteral("this is not your town"));
    if (count < 1) return fail(QStringLiteral("invalid count"));
    const UnitDef* unit = content.unit(unitId);
    if (!unit) return fail(QStringLiteral("unknown unit '%1'").arg(unitId));

    const Dwelling* found = nullptr;
    const QList<Dwelling> all = dwellings(*town, content);
    for (const Dwelling& d : all) {
        if (d.unitId == unitId) found = &d;
    }
    if (!found) return fail(QStringLiteral("%1 cannot be recruited in this town").arg(unit->name));
    const int available = town->recruitPool.value(found->poolKey);
    if (count > available)
        return fail(QStringLiteral("only %1 %2 available").arg(available).arg(unit->name));

    Player* p = state.player(player);
    const QString missing = missingResource(p->resources, unit->cost, count);
    if (!missing.isEmpty()) return fail(QStringLiteral("not enough %1").arg(missing));

    Army* army = state.army(target.isValid() ? target : town->garrison);
    if (!army) return fail(QStringLiteral("no army to recruit into"));
    if (army->owner != player || army->pos != town->pos)
        return fail(QStringLiteral("the army is not in this town"));
    Army trial = *army;  // check there is room before paying anything
    if (!trial.addUnits(unitId, count, unit->maxHp))
        return fail(QStringLiteral("the army has no free slot"));

    spend(p->resources, unit->cost, count);
    town->recruitPool[found->poolKey] = available - count;
    *army = trial;

    ActionResult ok;
    ok.ok = true;
    return ok;
}

void applyWeeklyGrowth(GameState& state, const ContentRegistry& content) {
    for (Town& town : state.towns()) {
        for (const Dwelling& d : dwellings(town, content)) {
            const UnitDef* unit = content.unit(d.unitId);
            if (unit) town.recruitPool[d.poolKey] += unit->weeklyGrowth;
        }
    }
}

ResourceAmounts buildingIncome(const Town& town, const ContentRegistry& content) {
    ResourceAmounts income;
    for (const QString& id : town.buildings) {
        const BuildingDef* b = content.building(id);
        if (!b) continue;
        for (const BuildingEffect& e : b->effects) {
            if (e.type == BuildingEffect::Type::Income) income[e.resource] += e.amount;
        }
    }
    return income;
}

}  // namespace kot::core
