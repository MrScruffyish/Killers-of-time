#include "core/rules/Automation.h"

#include <algorithm>

#include "core/content/ContentRegistry.h"
#include "core/rules/TownRules.h"
#include "core/rules/Trade.h"

namespace kot::core {

namespace {

QList<TownId> townsOf(const GameState& state, PlayerId player, TownId only) {
    QList<TownId> result;
    if (only.isValid()) {
        const Town* t = state.town(only);
        if (t && t->owner == player) result.append(only);
        return result;
    }
    for (const Town& t : state.towns()) {
        if (t.owner == player) result.append(t.id);
    }
    return result;
}

int tierOfDwelling(const BuildingDef& b, const ContentRegistry& content) {
    for (const BuildingEffect& e : b.effects) {
        if (e.type != BuildingEffect::Type::Dwelling) continue;
        const UnitDef* u = content.unit(e.unit);
        return u ? u->tier : 0;
    }
    return 0;
}

// Rival buildings that building `id` would shut out for good.
QStringList liveRivals(const Town& town, const QString& id, const ContentRegistry& content) {
    QStringList rivals;
    const BuildingDef* b = content.building(id);
    if (!b) return rivals;
    for (const BuildingDef& other : content.buildings()) {
        if (other.id == id || !other.fitsRace(town.race) || town.buildings.contains(other.id)) continue;
        if (b->excludes.contains(other.id) || other.excludes.contains(id)) rivals.append(other.id);
    }
    return rivals;
}

int totalCost(const BuildingDef& b) {
    int sum = 0;
    for (int v : b.cost) sum += v;
    return sum;
}

}  // namespace

BuyReport buyAllUnits(GameState& state, const ContentRegistry& content, PlayerId playerId,
                      TownId onlyTown, BuyStrategy strategy, bool useTrade) {
    BuyReport report;
    Player* player = state.player(playerId);
    if (!player) return report;

    for (const TownId townId : townsOf(state, playerId, onlyTown)) {
        QList<RecruitOption> options = recruitOptions(*state.town(townId), content);
        std::stable_sort(options.begin(), options.end(),
                         [&](const RecruitOption& a, const RecruitOption& b) {
                             const UnitDef* ua = content.unit(a.unitId);
                             const UnitDef* ub = content.unit(b.unitId);
                             if (!ua || !ub) return false;
                             if (strategy == BuyStrategy::BestFirst) return ua->tier > ub->tier;
                             const auto price = [&](const UnitDef* u) {
                                 qint64 sum = 0;
                                 for (auto it = u->cost.begin(); it != u->cost.end(); ++it)
                                     sum += static_cast<qint64>(it.value()) *
                                            (content.resource(it.key()) ? content.resource(it.key())->value : 1);
                                 return sum;
                             };
                             return price(ua) < price(ub);
                         });
        bool boughtHere = false;
        for (const RecruitOption& option : options) {
            const UnitDef* unit = content.unit(option.unitId);
            if (!unit || option.available <= 0) continue;
            int affordable = option.available;
            for (auto it = unit->cost.begin(); it != unit->cost.end(); ++it)
                affordable = std::min(affordable, player->resources.value(it.key()) / it.value());
            if (useTrade && affordable < option.available) {
                // The most units a trade can still make affordable (binary search).
                const int eff = tradeEfficiency(state, content, playerId);
                const auto costOf = [&](int n) {
                    ResourceAmounts c;
                    for (auto it = unit->cost.begin(); it != unit->cost.end(); ++it) c[it.key()] = it.value() * n;
                    return c;
                };
                int lo = affordable, hi = option.available;
                while (lo < hi) {
                    const int mid = (lo + hi + 1) / 2;
                    if (planTradeFor(content, player->resources, costOf(mid), eff)) lo = mid;
                    else hi = mid - 1;
                }
                if (lo > affordable) {
                    applyTradeSteps(player->resources, *planTradeFor(content, player->resources, costOf(lo), eff));
                    affordable = lo;
                }
            }
            if (affordable <= 0) continue;
            if (recruit(state, content, townId, playerId, option.unitId, affordable).ok) {
                report.unitsBought += affordable;
                for (auto it = unit->cost.begin(); it != unit->cost.end(); ++it)
                    report.spent[it.key()] += it.value() * affordable;
                boughtHere = true;
            }
        }
        if (boughtHere) ++report.townsServed;
    }
    return report;
}

AutoBuildReport autoBuild(GameState& state, const ContentRegistry& content, PlayerId playerId,
                          TownId onlyTown, bool useTrade) {
    AutoBuildReport report;
    for (const TownId townId : townsOf(state, playerId, onlyTown)) {
        const Town& town = *state.town(townId);
        if (town.lastBuildDay == state.day()) continue;

        QList<const BuildingDef*> plain;
        AutoBuildReport::Choice choice;
        choice.town = townId;
        for (const BuildingDef& b : content.buildings()) {
            if (!whyCannotBuild(state, content, townId, b.id, playerId, false).isEmpty()) continue;
            if (liveRivals(town, b.id, content).isEmpty()) plain.append(&b);
            else choice.buildings.append(b.id);
        }
        std::stable_sort(plain.begin(), plain.end(), [&](const BuildingDef* a, const BuildingDef* b) {
            const int ta = tierOfDwelling(*a, content), tb = tierOfDwelling(*b, content);
            if (ta != tb) return ta > tb;
            return totalCost(*a) < totalCost(*b);
        });

        bool built = false;
        for (const BuildingDef* b : plain) {
            const bool affordable = whyCannotBuild(state, content, townId, b->id, playerId).isEmpty();
            const bool ok = affordable ? buildBuilding(state, content, townId, b->id, playerId).ok
                            : useTrade ? buildBuildingWithTrade(state, content, townId, b->id, playerId).ok
                                       : false;
            if (ok) {
                report.built.append({townId, b->id});
                built = true;
                break;
            }
        }
        if (!built && !plain.isEmpty()) report.cannotAfford.append(townId);
        if (!choice.buildings.isEmpty()) report.needsChoice.append(choice);
    }
    return report;
}

BuildAndBuyReport buildThenBuyUnits(GameState& state, const ContentRegistry& content,
                                    PlayerId player, TownId town, bool useTrade,
                                    BuyStrategy strategy) {
    BuildAndBuyReport report;
    report.build = autoBuild(state, content, player, town, useTrade);
    report.buy = buyAllUnits(state, content, player, town, strategy, useTrade);
    return report;
}

}  // namespace kot::core
