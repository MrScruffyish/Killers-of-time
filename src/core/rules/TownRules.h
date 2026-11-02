#pragma once

#include <QList>
#include <QString>

#include "core/model/GameState.h"
#include "core/rules/ArmyRules.h"
#include "core/rules/Trade.h"

namespace kot::core {

class ContentRegistry;

// A place in a town where units can be recruited right now.
struct Dwelling {
    QString poolKey;     // key into Town::recruitPool (root building of the upgrade chain)
    QString buildingId;  // the building that currently provides it (the latest upgrade)
    QString unitId;      // the unit it produces
};

// Dwellings of a town: every built dwelling building that has not been
// replaced by a built upgrade of it.
QList<Dwelling> dwellings(const Town& town, const ContentRegistry& content);

// Empty string if `player` can build `buildingId` in the town now, otherwise the
// reason (not the owner, requirement missing, excluded, already built today,
// not enough resources, ...). Used by the UI to explain greyed-out buildings.
// With `checkResources` false the cost is ignored (to find what is buildable at all).
QString whyCannotBuild(const GameState& state, const ContentRegistry& content, TownId town,
                       const QString& buildingId, PlayerId player, bool checkResources = true);

// Builds it: pays the cost, remembers the day (one building per town per day) and
// stocks a new dwelling with its first week of units.
ActionResult buildBuilding(GameState& state, const ContentRegistry& content, TownId town,
                           const QString& buildingId, PlayerId player);

struct BuildResult : ActionResult {
    QList<TradeStep> trades;  // exchanges made to afford it (empty if none were needed)
};

// "Buy, but first trade": like buildBuilding, but missing resources are bought with
// spare ones automatically (see planTradeFor). Nothing happens if it still cannot be
// afforded.
BuildResult buildBuildingWithTrade(GameState& state, const ContentRegistry& content, TownId town,
                                   const QString& buildingId, PlayerId player);

// Units that can be recruited in a town right now: from its dwellings and from
// units sent there from map dwellings.
struct RecruitOption {
    QString unitId;
    int available = 0;
};
QList<RecruitOption> recruitOptions(const Town& town, const ContentRegistry& content);

// Pays for `count` units and adds them to `army` (checks resources and room first).
ActionResult payAndAddUnits(Player& player, const UnitDef& unit, int count, Army& army);

// Recruits `count` units of `unitId` from the town's dwellings into an army standing
// in the town (the garrison when `target` is invalid). Pays the unit costs.
ActionResult recruit(GameState& state, const ContentRegistry& content, TownId town,
                     PlayerId player, const QString& unitId, int count, ArmyId target = {});

// A new week: every dwelling (in towns and on the map) gets the weekly growth of its unit.
// Units a town dwelling grows per week: the unit's weeklyGrowth with the town race's
// growthPercent (at least 1 for growing units).
int townGrowth(const Town& town, const UnitDef& unit, const ContentRegistry& content);
void applyWeeklyGrowth(GameState& state, const ContentRegistry& content);

// Daily income from the "income" effects of a town's buildings.
ResourceAmounts buildingIncome(const Town& town, const ContentRegistry& content);

// True on the first day of a week (day 8, 15, ...).
inline bool isNewWeek(int day) { return day > 1 && (day - 1) % 7 == 0; }

}  // namespace kot::core
