#pragma once

#include <QList>
#include <QString>

#include "core/content/Defs.h"

namespace kot::core { class ContentRegistry; }

namespace kot::tools {

// Balance numbers shared by kot_balance (command line) and kot_balance_gui.
// Power is a rough fighting value: HP x average damage x (1 + 5% per attack+defense
// point), with extra weight for ranged, flying, double attack and no retaliation.
double unitPower(const core::UnitDef& unit);
// A cost in gold, other resources at their trade value.
double goldValue(const core::ContentRegistry& content, const core::ResourceAmounts& cost);

struct UnitRow {
    QString id, race;
    int tier = 1;
    double power = 0, cost = 0, powerPer1000 = 0, weekPower = 0;
    int growth = 0;
    QString note;  // "STRONG for its cost" / "weak for its cost" (1.5x off the tier median)
};
QList<UnitRow> unitRows(const core::ContentRegistry& content, const QString& race = {});

struct RaceRow {
    QString race;
    double weekPower = 0, weekCost = 0, powerPer1000 = 0;
    int townGold = 0;
};
// The best version of each of the race's dwellings, a week of growth.
QList<RaceRow> raceRows(const core::ContentRegistry& content);

// Equal budgets (at least 6 units per side), no heroes, real battle rules and
// battle AI, half of the runs with each side attacking. Returns A's share of wins.
int duelBudget(const core::ContentRegistry& content, const core::UnitDef& a, const core::UnitDef& b, int gold);
double duel(const core::ContentRegistry& content, const core::UnitDef& a, const core::UnitDef& b, int gold, int runs);

struct DuelRow {
    QString a, b;
    double share = 0;  // A's wins
    QString note;      // lopsided (> 75%)
};
// Every pair of the same tier (tier 0 = all tiers, still pairs of one tier).
QList<DuelRow> duelRows(const core::ContentRegistry& content, int tier, int gold, int runs);

}  // namespace kot::tools
