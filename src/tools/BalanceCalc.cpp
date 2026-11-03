#include "tools/BalanceCalc.h"

#include <QHash>
#include <algorithm>

#include "core/battle/Battle.h"
#include "core/battle/BattleAi.h"
#include "core/content/ContentRegistry.h"

namespace kot::tools {

using namespace kot::core;

double unitPower(const UnitDef& u) {
    return u.maxHp * (u.damageMin + u.damageMax) / 2.0 * (1.0 + 0.05 * (u.attack + u.defense)) *
           (u.range > 0 ? 1.25 : 1.0) * (u.has("flying") ? 1.1 : 1.0) * (u.has("double_attack") ? 1.6 : 1.0) *
           (u.has("no_retaliation") ? 1.15 : 1.0);
}

double goldValue(const ContentRegistry& c, const ResourceAmounts& cost) {
    double v = 0;
    for (auto it = cost.begin(); it != cost.end(); ++it) {
        const ResourceDef* r = c.resource(it.key());
        v += it.value() * (r ? (r->isCurrency ? 1 : r->value) : 100);
    }
    return v;
}

namespace {
double median(QList<double> v) {
    if (v.isEmpty()) return 0;
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}
}  // namespace

QList<UnitRow> unitRows(const ContentRegistry& c, const QString& race) {
    QHash<int, QList<double>> byTier;
    for (const UnitDef& u : c.units())
        if (goldValue(c, u.cost) > 0 && !u.has("siege_engine")) byTier[u.tier].append(unitPower(u) / goldValue(c, u.cost) * 1000);
    QList<UnitRow> rows;
    for (const UnitDef& u : c.units()) {
        if ((!race.isEmpty() && u.race != race) || u.has("siege_engine")) continue;  // machines do not fight
        UnitRow r;
        r.id = u.id;
        r.race = u.race;
        r.tier = u.tier;
        r.power = unitPower(u);
        r.cost = goldValue(c, u.cost);
        r.powerPer1000 = r.cost > 0 ? r.power / r.cost * 1000 : 0;
        r.growth = u.weeklyGrowth;
        r.weekPower = r.power * u.weeklyGrowth;
        const double mid = median(byTier.value(u.tier));
        if (mid > 0 && r.powerPer1000 > mid * 1.5) r.note = QStringLiteral("STRONG for its cost");
        if (mid > 0 && r.powerPer1000 < mid / 1.5) r.note = QStringLiteral("weak for its cost");
        rows << r;
    }
    return rows;
}

QList<RaceRow> raceRows(const ContentRegistry& c) {
    QList<RaceRow> rows;
    for (const QString& race : c.raceIds()) {
        const RaceDef& rd = c.race(race);
        QStringList units;
        for (const BuildingDef& b : c.buildings()) {
            if (b.race != race) continue;
            for (const BuildingEffect& e : b.effects)
                if (e.type == BuildingEffect::Type::Dwelling) units << e.unit;
        }
        for (const QString& id : QStringList(units)) {
            const UnitDef* u = c.unit(id);
            if (u && !u->upgradesTo.isEmpty() && units.contains(u->upgradesTo)) units.removeAll(id);
        }
        RaceRow r;
        r.race = race;
        for (const QString& id : units) {
            const UnitDef* u = c.unit(id);
            if (!u) continue;
            const double growth = std::max(1.0, u->weeklyGrowth * (100.0 + rd.growthPercent) / 100.0);
            r.weekPower += growth * unitPower(*u);
            r.weekCost += growth * goldValue(c, u->cost);
        }
        r.powerPer1000 = r.weekCost > 0 ? r.weekPower / r.weekCost * 1000 : 0;
        r.townGold = c.balance().economy.townDailyGold * (100 + rd.incomePercent) / 100;
        rows << r;
    }
    return rows;
}

int duelBudget(const ContentRegistry& c, const UnitDef& a, const UnitDef& b, int gold) {
    return std::max<int>(gold, int(6 * std::max(goldValue(c, a.cost), goldValue(c, b.cost))));
}

double duel(const ContentRegistry& c, const UnitDef& a, const UnitDef& b, int gold, int runs) {
    gold = duelBudget(c, a, b, gold);
    const int countA = std::max(1, int(gold / std::max(1.0, goldValue(c, a.cost))));
    const int countB = std::max(1, int(gold / std::max(1.0, goldValue(c, b.cost))));
    int winsA = 0;
    for (int i = 0; i < runs; ++i) {
        GameState gs(1000 + i);
        gs.setMap(GameMap(4, 4, c.terrain().firstKey()));
        const ArmyId aa = gs.addArmy(PlayerId(), {0, 0});
        const ArmyId bb = gs.addArmy(PlayerId(), {1, 0});
        gs.army(aa)->addUnits(a.id, countA, a.maxHp);
        gs.army(bb)->addUnits(b.id, countB, b.maxHp);
        const bool aAttacks = (i % 2 == 0);
        Battle battle(gs, c, aAttacks ? aa : bb, aAttacks ? bb : aa, 77 + i);
        while (!battle.isOver()) battle.act(chooseBattleAction(battle));
        const bool attackerWon = battle.outcome() == Battle::Outcome::AttackerWon;
        const bool defenderWon = battle.outcome() == Battle::Outcome::DefenderWon;
        if ((aAttacks && attackerWon) || (!aAttacks && defenderWon)) ++winsA;
    }
    return double(winsA) / runs;
}

QList<DuelRow> duelRows(const ContentRegistry& c, int tier, int gold, int runs) {
    QList<const UnitDef*> units;
    for (const UnitDef& u : c.units())
        if ((tier == 0 || u.tier == tier) && !u.has("siege_engine")) units << &u;
    QList<DuelRow> rows;
    for (int i = 0; i < units.size(); ++i)
        for (int j = i + 1; j < units.size(); ++j) {
            if (units[i]->tier != units[j]->tier) continue;
            DuelRow r;
            r.a = units[i]->id;
            r.b = units[j]->id;
            r.share = duel(c, *units[i], *units[j], gold, runs);
            if (r.share > 0.75) r.note = QStringLiteral("favours the first");
            if (r.share < 0.25) r.note = QStringLiteral("favours the second");
            rows << r;
        }
    return rows;
}

QList<QPair<QString, int>> raceWeekArmy(const ContentRegistry& c, const QString& race, int weeks, int budgetPerWeek) {
    const RaceDef& rd = c.race(race);
    QStringList units;
    for (const BuildingDef& b : c.buildings()) {
        if (b.race != race) continue;
        for (const BuildingEffect& e : b.effects)
            if (e.type == BuildingEffect::Type::Dwelling) units << e.unit;
    }
    for (const QString& id : QStringList(units)) {  // only the best version of each dwelling
        const UnitDef* u = c.unit(id);
        if (u && !u->upgradesTo.isEmpty() && units.contains(u->upgradesTo)) units.removeAll(id);
    }
    std::sort(units.begin(), units.end(), [&](const QString& x, const QString& y) {  // best first
        const UnitDef *ux = c.unit(x), *uy = c.unit(y);
        return ux && uy && (ux->tier != uy->tier ? ux->tier > uy->tier : x < y);
    });
    double gold = budgetPerWeek > 0 ? double(budgetPerWeek) * weeks * (100 + rd.incomePercent) / 100.0 : 1e18;
    QList<QPair<QString, int>> army;
    for (const QString& id : units) {
        const UnitDef* u = c.unit(id);
        if (!u) continue;
        const int perWeek = std::max(1, int(std::lround(u->weeklyGrowth * (100.0 + rd.growthPercent) / 100.0)));
        const double price = std::max(1.0, goldValue(c, u->cost));
        const int n = std::min(perWeek * weeks, int(gold / price));
        if (n <= 0) continue;
        gold -= n * price;
        army << qMakePair(id, n);
    }
    return army;
}

WeekRow weekBattle(const ContentRegistry& c, const QString& a, const QString& b, int weeks, int runs, int budgetPerWeek) {
    WeekRow r;
    r.a = a;
    r.b = b;
    const auto armyA = raceWeekArmy(c, a, weeks, budgetPerWeek), armyB = raceWeekArmy(c, b, weeks, budgetPerWeek);
    for (const auto& [id, n] : armyA) r.costA += n * goldValue(c, c.unit(id)->cost);
    for (const auto& [id, n] : armyB) r.costB += n * goldValue(c, c.unit(id)->cost);
    int winsA = 0;
    for (int i = 0; i < runs; ++i) {
        GameState gs(2000 + i);
        gs.setMap(GameMap(4, 4, c.terrain().firstKey()));
        const ArmyId aa = gs.addArmy(PlayerId(), {0, 0});
        const ArmyId bb = gs.addArmy(PlayerId(), {1, 0});
        for (const auto& [id, n] : armyA) gs.army(aa)->addUnits(id, n, c.unit(id)->maxHp);
        for (const auto& [id, n] : armyB) gs.army(bb)->addUnits(id, n, c.unit(id)->maxHp);
        const bool aAttacks = (i % 2 == 0);
        Battle battle(gs, c, aAttacks ? aa : bb, aAttacks ? bb : aa, 99 + i);
        for (int guard = 0; guard < 5000 && !battle.isOver(); ++guard) battle.act(chooseBattleAction(battle));
        const bool attackerWon = battle.outcome() == Battle::Outcome::AttackerWon;
        const bool defenderWon = battle.outcome() == Battle::Outcome::DefenderWon;
        if ((aAttacks && attackerWon) || (!aAttacks && defenderWon)) ++winsA;
    }
    r.share = double(winsA) / runs;
    if (r.share > 0.75) r.note = QStringLiteral("favours the first");
    if (r.share < 0.25) r.note = QStringLiteral("favours the second");
    return r;
}

QList<WeekRow> weekRows(const ContentRegistry& c, int weeks, int runs, int budgetPerWeek) {
    QList<WeekRow> rows;
    const QStringList races = c.raceIds();
    for (int i = 0; i < races.size(); ++i)
        for (int j = i + 1; j < races.size(); ++j) rows << weekBattle(c, races[i], races[j], weeks, runs, budgetPerWeek);
    return rows;
}

}  // namespace kot::tools
