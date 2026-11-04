#include "core/rules/Dwellings.h"

#include <algorithm>

#include "core/content/ContentRegistry.h"
#include "core/rules/TownRules.h"

namespace kot::core {

namespace {
ActionResult fail(const QString& message) {
    ActionResult r;
    r.error = message;
    return r;
}

int distance(Pos a, Pos b) { return std::max(std::abs(a.x - b.x), std::abs(a.y - b.y)); }

bool sendOne(GameState& state, MapDwelling& d, TownId town) {
    Town* t = state.town(town);
    if (!t || d.available <= 0) return false;
    t->importedUnits[d.unitId] += d.available;
    d.available = 0;
    return true;
}
}  // namespace

ActionResult recruitAtDwelling(GameState& state, const ContentRegistry& content,
                               MapDwellingId dwellingId, PlayerId player, int count,
                               ArmyId armyId) {
    MapDwelling* d = state.mapDwelling(dwellingId);
    if (!d) return fail(QStringLiteral("unknown dwelling"));
    if (!player.isValid() || d->owner != player) return fail(QStringLiteral("this dwelling is not yours"));
    if (count < 1) return fail(QStringLiteral("invalid count"));
    if (count > d->available)
        return fail(QStringLiteral("only %1 units available").arg(d->available));
    const UnitDef* unit = content.unit(d->unitId);
    if (!unit) return fail(QStringLiteral("unknown unit"));
    Army* army = state.army(armyId);
    if (!army) return fail(QStringLiteral("unknown army"));
    if (army->owner != player || army->pos != d->pos)
        return fail(QStringLiteral("the army is not at this dwelling"));

    const ActionResult paid = payAndAddUnits(*state.player(player), *unit, count, *army);
    if (paid.ok) d->available -= count;
    return paid;
}

SendReport sendDwellingUnitsToTown(GameState& state, PlayerId player, TownId townId) {
    SendReport report;
    const Town* town = state.town(townId);
    if (!town || town->owner != player) return report;
    for (MapDwelling& d : state.mapDwellings()) {
        if (d.owner != player) continue;
        const int units = d.available;
        if (sendOne(state, d, townId)) {
            ++report.dwellingsEmptied;
            report.unitsSent += units;
        }
    }
    return report;
}

SendReport sendDwellingUnitsToClosestTown(GameState& state, PlayerId player) {
    SendReport report;
    for (MapDwelling& d : state.mapDwellings()) {
        if (d.owner != player || d.available <= 0) continue;
        TownId best;
        int bestDistance = 0;
        for (const Town& t : state.towns()) {
            if (t.owner != player) continue;
            const int dist = distance(t.pos, d.pos);
            if (!best.isValid() || dist < bestDistance) {
                best = t.id;
                bestDistance = dist;
            }
        }
        const int units = d.available;
        if (best.isValid() && sendOne(state, d, best)) {
            ++report.dwellingsEmptied;
            report.unitsSent += units;
        }
    }
    return report;
}

}  // namespace kot::core
