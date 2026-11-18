#include "core/rules/TownRules.h"

#include <QSet>
#include <algorithm>

#include "core/content/ContentRegistry.h"
#include "core/rules/Boats.h"
#include "core/rules/Tavern.h"

namespace kot::core {

namespace {

ActionResult fail(const QString& message) {
    ActionResult r;
    r.error = message;
    return r;
}

UnitTypeId dwellingUnit(const BuildingDef& b) {
    for (const BuildingEffect& e : b.effects) {
        if (e.type == BuildingEffect::Type::Dwelling) return e.unit;
    }
    return {};
}

// The first resource there is not enough of (invalid: enough of all).
ResourceId missingResource(const ResourceAmounts& have, const ResourceAmounts& cost, int times = 1) {
    for (const auto [r, amount] : cost)
        if (have.value(r) < amount * times) return r;
    return {};
}

void spend(ResourceAmounts& have, const ResourceAmounts& cost, int times = 1) {
    for (const auto [r, amount] : cost) have[r] -= amount * times;
}

}  // namespace

QList<Dwelling> dwellings(const Town& town, const ContentRegistry& content) {
    QSet<BuildingTypeId> replaced;
    for (const BuildingTypeId id : town.buildings) {
        const BuildingDef* b = content.building(id);
        if (b && b->upgradeOf.isValid()) replaced.insert(b->upgradeOf);
    }
    QList<Dwelling> result;
    for (const BuildingTypeId id : town.buildings) {
        const BuildingDef* b = content.building(id);
        if (!b || replaced.contains(id)) continue;
        const UnitTypeId unit = dwellingUnit(*b);
        if (!unit.isValid()) continue;
        BuildingTypeId root = id;
        for (int guard = 0; guard < 16; ++guard) {
            const BuildingDef* r = content.building(root);
            if (!r || !r->upgradeOf.isValid()) break;
            root = r->upgradeOf;
        }
        result.append({root, id, unit});
    }
    return result;
}

QString whyCannotBuild(const GameState& state, const ContentRegistry& content, TownId townId,
                       BuildingTypeId buildingId, PlayerId player, bool checkResources) {
    const Town* town = state.town(townId);
    if (!town) return QStringLiteral("unknown town");
    if (!player.isValid() || town->owner != player)
        return QStringLiteral("this is not your town");
    const BuildingDef* b = content.building(buildingId);
    if (!b) return QStringLiteral("unknown building %1").arg(buildingId.index());
    if (!b->fitsRace(town->race))
        return QStringLiteral("%1 cannot be built in a %2 town").arg(b->name, content.race(town->race).name);
    if (town->buildings.contains(buildingId)) return QStringLiteral("already built");
    for (const BuildingEffect& e : b->effects)
        if (e.type == BuildingEffect::Type::Shipyard && !coastalAt(state, content, town->pos))
            return QStringLiteral("%1 needs a town at the coast").arg(b->name);
    if (town->lastBuildDay == state.day())
        return QStringLiteral("a building was already built here today");

    QList<BuildingTypeId> needed = b->requiredBuildings;
    if (b->upgradeOf.isValid()) needed.append(b->upgradeOf);
    for (const BuildingTypeId req : needed) {
        if (!town->buildings.contains(req)) {
            const BuildingDef* rb = content.building(req);
            return QStringLiteral("requires %1").arg(rb ? rb->name : QString());
        }
    }
    for (const BuildingTypeId other : town->buildings) {
        const BuildingDef* ob = content.building(other);
        if (b->excludes.contains(other) || (ob && ob->excludes.contains(buildingId)))
            return QStringLiteral("excluded by %1").arg(ob ? ob->name : QString());
    }
    if (!checkResources) return {};
    const Player* p = state.player(player);
    const ResourceId missing = p ? missingResource(p->resources, b->cost) : ResourceId();
    if (missing.isValid()) return QStringLiteral("not enough %1").arg(content.resource(missing).key);
    return {};
}

ActionResult buildBuilding(GameState& state, const ContentRegistry& content, TownId townId,
                           BuildingTypeId buildingId, PlayerId player) {
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
        town->recruitPool[d.poolKey] = unit ? townGrowth(state, *town, *unit, content) : 0;
    }
    // A new tavern offers its first heroes.
    if (hasTavern(*town, content)) refreshTavernOffers(state, content, townId);

    ActionResult ok;
    ok.ok = true;
    return ok;
}

BuildResult buildBuildingWithTrade(GameState& state, const ContentRegistry& content,
                                   TownId townId, BuildingTypeId buildingId, PlayerId player) {
    BuildResult result;
    const QString why = whyCannotBuild(state, content, townId, buildingId, player, false);
    if (!why.isEmpty()) {
        result.error = why;
        return result;
    }
    Player* p = state.player(player);
    const BuildingDef* b = content.building(buildingId);
    const auto plan = planTradeFor(content, p->resources, b->cost, tradeEfficiency(state, content, player));
    if (!plan) {
        result.error = QStringLiteral("cannot be afforded, even by trading");
        return result;
    }
    applyTradeSteps(p->resources, *plan);
    const ActionResult built = buildBuilding(state, content, townId, buildingId, player);
    result.ok = built.ok;
    result.error = built.error;
    result.trades = *plan;
    return result;
}

QList<RecruitOption> recruitOptions(const Town& town, const ContentRegistry& content) {
    QList<RecruitOption> options;
    const auto add = [&](UnitTypeId unit, int count) {
        for (RecruitOption& o : options) {
            if (o.unitId == unit) {
                o.available += count;
                return;
            }
        }
        options.append({unit, count});
    };
    for (const Dwelling& d : dwellings(town, content)) add(d.unitId, town.recruitPool.value(d.poolKey));
    for (auto it = town.importedUnits.begin(); it != town.importedUnits.end(); ++it) {
        if (it.value() > 0) add(it.key(), it.value());
    }
    return options;
}

ActionResult payAndAddUnits(const ContentRegistry& content, Player& player, const UnitDef& unit, int count, Army& army) {
    const ResourceId missing = missingResource(player.resources, unit.cost, count);
    if (missing.isValid()) return fail(QStringLiteral("not enough %1").arg(content.resource(missing).key));
    Army trial = army;  // check there is room before paying anything
    if (!trial.addUnits(unit.id, count, unit.maxHp))
        return fail(QStringLiteral("the army has no free slot"));
    spend(player.resources, unit.cost, count);
    army = trial;
    ActionResult ok;
    ok.ok = true;
    return ok;
}

ActionResult recruit(GameState& state, const ContentRegistry& content, TownId townId,
                     PlayerId player, UnitTypeId unitId, int count, ArmyId target) {
    Town* town = state.town(townId);
    if (!town) return fail(QStringLiteral("unknown town"));
    if (!player.isValid() || town->owner != player)
        return fail(QStringLiteral("this is not your town"));
    if (count < 1) return fail(QStringLiteral("invalid count"));
    const UnitDef* unit = content.unit(unitId);
    if (!unit) return fail(QStringLiteral("unknown unit %1").arg(unitId.index()));

    const Dwelling* found = nullptr;
    const QList<Dwelling> all = dwellings(*town, content);
    for (const Dwelling& d : all) {
        if (d.unitId == unitId) found = &d;
    }
    const int imported = town->importedUnits.value(unitId);
    if (!found && imported == 0)
        return fail(QStringLiteral("%1 cannot be recruited in this town").arg(unit->name));
    const int poolAvailable = found ? town->recruitPool.value(found->poolKey) : 0;
    const int available = poolAvailable + imported;
    if (count > available)
        return fail(QStringLiteral("only %1 %2 available").arg(available).arg(unit->name));

    Army* army = state.army(target.isValid() ? target : town->garrison);
    if (!army) return fail(QStringLiteral("no army to recruit into"));
    if (army->owner != player || army->pos != town->pos)
        return fail(QStringLiteral("the army is not in this town"));

    const ActionResult paid = payAndAddUnits(content, *state.player(player), *unit, count, *army);
    if (!paid.ok) return paid;

    // Units sent from map dwellings are used first.
    const int fromImported = std::min(count, imported);
    if (fromImported > 0) {
        town->importedUnits[unitId] -= fromImported;
        if (town->importedUnits[unitId] == 0) town->importedUnits.remove(unitId);
    }
    if (count > fromImported) town->recruitPool[found->poolKey] = poolAvailable - (count - fromImported);
    return paid;
}

int townGrowth(const GameState& state, const Town& town, const UnitDef& unit, const ContentRegistry& content) {
    const Player* owner = state.player(town.owner);
    const WeekEventDef* event = content.weekEvent(state.weekEvent());
    int buildings = 0;  // granaries and the like
    for (const BuildingTypeId id : town.buildings)
        if (const BuildingDef* b = content.building(id))
            for (const BuildingEffect& e : b->effects)
                if (e.type == BuildingEffect::Type::Growth) buildings += e.amount;
    const int percent = 100 + content.race(town.race).growthPercent + (owner ? owner->growthPercent : 0) +
                        (event ? event->growthPercent : 0) + buildings;
    return std::max(unit.weeklyGrowth > 0 ? 1 : 0, (unit.weeklyGrowth * percent + 50) / 100);
}

void applyWeeklyGrowth(GameState& state, const ContentRegistry& content) {
    const WeekEventDef* event = content.weekEvent(state.weekEvent());
    const auto eventExtra = [event](const UnitDef& u) { return event ? event->unitGrowth.value(u.id) : 0; };
    const NeutralTownBalance& nb = content.balance().neutralTowns;
    for (Town& town : state.towns()) {
        Army* garrison = town.owner.isValid() ? nullptr : state.army(town.garrison);
        for (const Dwelling& d : dwellings(town, content)) {
            const UnitDef* unit = content.unit(d.unitId);
            if (!unit) continue;
            const int week = townGrowth(state, town, *unit, content) + eventExtra(*unit);
            if (town.owner.isValid()) {
                town.recruitPool[d.poolKey] += week;
                continue;
            }
            // A neutral town: a little more garrison instead of recruits piling up.
            if (!garrison || nb.growthPercent <= 0 || week <= 0) continue;
            int have = 0;
            for (const UnitStack& st : garrison->stacks) have += st.unitTypeId() == unit->id ? st.count() : 0;
            const int add = std::min(std::max(1, week * nb.growthPercent / 100), nb.capWeeks * week - have);
            if (add > 0) garrison->addUnits(unit->id, add, unit->maxHp);
        }
    }
    for (MapDwelling& d : state.mapDwellings()) {
        const UnitDef* unit = content.unit(d.unitId);
        if (unit) d.available += unit->weeklyGrowth + eventExtra(*unit);
    }
}

ResourceAmounts buildingIncome(const Town& town, const ContentRegistry& content) {
    ResourceAmounts income;
    for (const BuildingTypeId id : town.buildings) {
        const BuildingDef* b = content.building(id);
        if (!b) continue;
        for (const BuildingEffect& e : b->effects) {
            if (e.type == BuildingEffect::Type::Income) income[e.resource] += e.amount;
        }
    }
    return income;
}

}  // namespace kot::core
