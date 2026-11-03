#include "core/battle/AutoBattle.h"

#include <QJsonArray>
#include <QJsonObject>
#include <algorithm>
#include <cmath>

#include "core/battle/BattleAi.h"
#include "core/content/ContentRegistry.h"
#include "core/rules/HeroRules.h"

namespace kot::core {

std::uint64_t newBattleSeed(GameState& state) {
    return (static_cast<std::uint64_t>(state.rng().nextU32()) << 32) | state.rng().nextU32();
}

BattleReport applyBattleResult(GameState& state, const ContentRegistry& content, const Battle& battle) {
    BattleReport report;
    report.rounds = battle.round();
    BattleSide* sides[2] = {&report.attacker, &report.defender};
    int killedHp[2] = {0, 0};  // HP side s lost
    // Veterancy: survivors gain experience from the enemy HP their side destroyed.
    int lostHp[2] = {0, 0}, survivors[2] = {0, 0};
    for (const BattleUnit& u : battle.units()) {
        if (u.armyIndex < 0) continue;  // the catapult
        const Army* army = state.army(battle.army(u.side));
        const int before = (army && u.armyIndex >= 0 && u.armyIndex < army->stacks.size()) ? army->stacks[u.armyIndex].count() : 0;
        lostHp[u.side] += std::max(0, before - u.stack.count()) * u.baseMaxHp;
        survivors[u.side] += u.stack.count();
    }
    int gain[2];
    for (int s = 0; s < 2; ++s)
        gain[s] = survivors[s] > 0 ? lostHp[1 - s] * content.balance().veterancy.xpPercent / 100 / survivors[s] : 0;
    for (int s = 0; s < 2; ++s) {
        sides[s]->owner = battle.owner(s);
        sides[s]->hero = battle.hero(s);
        Army* army = state.army(battle.army(s));
        if (!army) continue;
        for (const BattleUnit& u : battle.units()) {
            if (u.side != s || u.armyIndex < 0 || u.armyIndex >= army->stacks.size()) continue;  // the catapult is not in the army
            UnitStack& stack = army->stacks[u.armyIndex];
            const int lost = stack.count() - u.stack.count();
            if (lost > 0) {
                sides[s]->losses[stack.unitTypeId()] += lost;
                killedHp[s] += lost * stack.maxHp();
            }
            // Back to the army's own max HP (the hero's bonus only counts in battle).
            stack = u.alive() ? UnitStack(stack.unitTypeId(), u.stack.count(), u.baseMaxHp)
                              : UnitStack::fromTotalHp(stack.unitTypeId(), 0, u.baseMaxHp);
            stack.setExperience(u.stack.experience() + gain[s]);
        }
        army->removeEmptyStacks();
        for (UnitStack& st : army->stacks) st.restoreFullHealth();
        if (Hero* h = state.hero(battle.hero(s)); h && battle.heroOf(s)) h->spellPoints = battle.heroSpellPoints(s);
    }
    switch (battle.outcome()) {
    case Battle::Outcome::AttackerWon: report.outcome = BattleReport::Outcome::AttackerWon; break;
    case Battle::Outcome::DefenderWon: report.outcome = BattleReport::Outcome::DefenderWon; break;
    default: report.outcome = BattleReport::Outcome::Draw; break;
    }
    if (report.outcome == BattleReport::Outcome::Draw) return report;

    const int winner = report.attackerWon() ? 0 : 1;
    const int loser = 1 - winner;
    if (Hero* h = state.hero(battle.hero(winner))) {
        report.experience = killedHp[loser] * content.balance().battle.experiencePerHp;
        report.levelsGained = addExperience(*h, content, report.experience);
    }
    // The winner's race ability: the undead raise some of the fallen, orcs loot gold.
    if (Player* pl = state.player(battle.owner(winner))) {
        const RaceDef& race = content.race(pl->race);
        if (race.battleLootPercent > 0) {
            report.lootGold = killedHp[loser] * race.battleLootPercent / 100;
            pl->resources[QStringLiteral("gold")] += report.lootGold;
        }
        const UnitDef* raised = race.raiseDeadPercent > 0 ? content.unit(race.raiseDeadUnit) : nullptr;
        Army* army = state.army(battle.army(winner));
        if (raised && army) {
            const int count = killedHp[loser] * race.raiseDeadPercent / 100 / std::max(1, raised->maxHp);
            if (count > 0 && army->addUnits(raised->id, count, raised->maxHp)) {
                report.raisedUnit = raised->id;
                report.raisedCount = count;
            }
        }
    }
    Army* lost = state.army(battle.army(loser));
    if (!lost) return report;
    if (battle.surrendered(loser)) {  // he paid: hero and army stay
        const int price = battle.surrenderCost(loser);
        Player* payer = state.player(battle.owner(loser));
        Player* payee = state.player(battle.owner(winner));
        const int paid = payer ? std::min(price, payer->resources.value(QStringLiteral("gold"))) : 0;
        if (payer) payer->resources[QStringLiteral("gold")] -= paid;
        if (payee) payee->resources[QStringLiteral("gold")] += paid;
        report.surrendered = true;
        report.surrenderGold = paid;
        return report;
    }
    if (battle.retreated(loser)) {  // the hero got away; his army is lost
        for (const UnitStack& st : lost->stacks) {
            sides[loser]->losses[st.unitTypeId()] += st.count();
            killedHp[loser] += st.count() * st.maxHp();
        }
        lost->stacks.clear();
        report.retreated = true;
        // He flees to the nearest own town with no hero in it, and rests there today.
        const Town* best = nullptr;
        int bestDist = 0;
        for (const Town& t : state.towns()) {
            if (t.owner != lost->owner || t.pos == lost->pos) continue;
            bool taken = false;
            for (const Army& a : state.armies()) taken = taken || (a.id != lost->id && a.hasHero() && a.pos == t.pos);
            if (taken) continue;
            const int d = std::abs(t.pos.x - lost->pos.x) + std::abs(t.pos.y - lost->pos.y);
            if (!best || d < bestDist) {
                best = &t;
                bestDist = d;
            }
        }
        if (best) {
            lost->pos = best->pos;
            report.retreatedTo = best->name;
            if (Hero* h = state.hero(lost->hero)) {
                h->movementPoints = 0;
                h->destination.reset();
                h->inBoat = false;
            }
        }
        return report;
    }
    bool isGarrison = false;
    for (const Town& t : state.towns()) isGarrison = isGarrison || t.garrison == lost->id;
    if (lost->hero.isValid()) {
        report.loserHeroKilled = true;
        state.heroes().remove(lost->hero);
        lost->hero = HeroId();
    }
    if (isGarrison) lost->stacks.clear();
    else state.armies().remove(lost->id);
    return report;
}

QString buySiegeEngine(GameState& state, const ContentRegistry& content, Battle& battle, QList<BattleEvent>* events) {
    const QString why = battle.whyNoSiegeEngine();
    if (!why.isEmpty()) return why;
    Player* owner = state.player(battle.owner(0));
    if (!owner) return QStringLiteral("only a player's army can build one");
    const ResourceAmounts& cost = content.balance().siege.engineCost;
    for (auto it = cost.begin(); it != cost.end(); ++it)
        if (owner->resources.value(it.key()) < it.value())
            return QStringLiteral("a catapult costs %1 %2, you have %3").arg(it.value()).arg(it.key()).arg(owner->resources.value(it.key()));
    for (auto it = cost.begin(); it != cost.end(); ++it) owner->resources[it.key()] -= it.value();
    return battle.buildSiegeEngine(events);
}

BattleReport resolveBattle(GameState& state, const ContentRegistry& content, ArmyId attackerArmy,
                           ArmyId defenderArmy) {
    if (!state.army(attackerArmy) || !state.army(defenderArmy)) return {};
    Battle battle(state, content, attackerArmy, defenderArmy, newBattleSeed(state));
    buySiegeEngine(state, content, battle, nullptr);  // against walls, if the attacker can pay
    // Every action ends a stack's turn, so this always finishes (round limit).
    while (!battle.isOver()) battle.act(chooseBattleAction(battle));
    return applyBattleResult(state, content, battle);
}

QJsonObject battleReportToJson(const BattleReport& report) {
    const auto side = [](const BattleSide& s) {
        QJsonObject o;
        o.insert("owner", static_cast<int>(s.owner.value()));
        o.insert("hero", static_cast<int>(s.hero.value()));
        QJsonObject losses;
        for (auto it = s.losses.begin(); it != s.losses.end(); ++it) losses.insert(it.key(), it.value());
        o.insert("losses", losses);
        return o;
    };
    QJsonObject o;
    o.insert("outcome", report.outcome == BattleReport::Outcome::AttackerWon   ? "attacker"
                        : report.outcome == BattleReport::Outcome::DefenderWon ? "defender"
                                                                               : "draw");
    o.insert("rounds", report.rounds);
    o.insert("attacker", side(report.attacker));
    o.insert("defender", side(report.defender));
    o.insert("experience", report.experience);
    o.insert("levelsGained", report.levelsGained);
    o.insert("loserHeroKilled", report.loserHeroKilled);
    if (report.retreated) o.insert("retreated", true);
    if (report.raisedCount > 0) o.insert("raised", QJsonObject{{"unit", report.raisedUnit}, {"count", report.raisedCount}});
    if (report.lootGold > 0) o.insert("lootGold", report.lootGold);
    if (!report.retreatedTo.isEmpty()) o.insert("retreatedTo", report.retreatedTo);
    if (report.surrendered) {
        o.insert("surrendered", true);
        o.insert("surrenderGold", report.surrenderGold);
    }
    return o;
}

}  // namespace kot::core
