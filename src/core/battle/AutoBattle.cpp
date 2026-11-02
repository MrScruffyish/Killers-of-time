#include "core/battle/AutoBattle.h"

#include <QJsonArray>
#include <QJsonObject>
#include <algorithm>
#include <cmath>

#include "core/content/ContentRegistry.h"
#include "core/rules/HeroRules.h"

namespace kot::core {

namespace {

struct Fighter {
    int side = 0;  // 0 attacker, 1 defender
    int index = 0; // stack index in its army
    const UnitDef* unit = nullptr;
    int shotsLeft = 0;
    bool retaliated = false;
};

int heroTrait(const GameState& state, HeroId id, const char* trait) {
    const Hero* h = state.hero(id);
    return h ? h->trait(QString::fromLatin1(trait)) : 0;
}

double averageDamage(const UnitStack& s, const UnitDef* u) {
    return s.count() * (u->damageMin + u->damageMax) / 2.0;
}

}  // namespace

BattleReport resolveBattle(GameState& state, const ContentRegistry& content, ArmyId attackerArmy,
                           ArmyId defenderArmy) {
    BattleReport report;
    Army* armies[2] = {state.army(attackerArmy), state.army(defenderArmy)};
    if (!armies[0] || !armies[1]) return report;
    report.attacker.owner = armies[0]->owner;
    report.attacker.hero = armies[0]->hero;
    report.defender.owner = armies[1]->owner;
    report.defender.hero = armies[1]->hero;
    const BattleBalance& bb = content.balance().battle;

    int attack[2], defense[2];
    for (int s = 0; s < 2; ++s) {
        attack[s] = heroTrait(state, armies[s]->hero, "attack");
        defense[s] = heroTrait(state, armies[s]->hero, "defense");
    }

    QList<Fighter> fighters;
    QList<UnitStack>* stacks[2] = {&armies[0]->stacks, &armies[1]->stacks};
    QMap<QString, int> startCount[2];
    QMap<QString, int> startHpPerUnit[2];
    for (int s = 0; s < 2; ++s) {
        for (int i = 0; i < stacks[s]->size(); ++i) {
            const UnitStack& st = (*stacks[s])[i];
            const UnitDef* u = content.unit(st.unitTypeId());
            if (!u || st.isEliminated()) continue;
            fighters.append({s, i, u, u->shots, false});
            startCount[s][st.unitTypeId()] += st.count();
            startHpPerUnit[s][st.unitTypeId()] = st.maxHp();
        }
    }
    // Initiative, then battle speed; attacker first on ties; then army order.
    std::stable_sort(fighters.begin(), fighters.end(), [](const Fighter& a, const Fighter& b) {
        if (a.unit->initiative != b.unit->initiative) return a.unit->initiative > b.unit->initiative;
        if (a.unit->battleSpeed != b.unit->battleSpeed) return a.unit->battleSpeed > b.unit->battleSpeed;
        if (a.side != b.side) return a.side < b.side;
        return a.index < b.index;
    });

    const auto alive = [&](int side) {
        for (const Fighter& f : fighters)
            if (f.side == side && !(*stacks[side])[f.index].isEliminated()) return true;
        return false;
    };
    const auto strike = [&](const Fighter& from, const Fighter& to) {
        const UnitStack& a = (*stacks[from.side])[from.index];
        UnitStack& t = (*stacks[to.side])[to.index];
        const int diff = (from.unit->attack + attack[from.side]) - (to.unit->defense + defense[to.side]);
        double mult = 1.0;
        if (diff > 0) mult += std::min(diff * bb.attackPercentPerPoint, bb.maxAttackPercent) / 100.0;
        if (diff < 0) mult -= std::min(-diff * bb.defensePercentPerPoint, bb.maxDefensePercent) / 100.0;
        const int roll = state.rng().nextInt(from.unit->damageMin, std::max(from.unit->damageMin, from.unit->damageMax));
        const int damage = std::max(1, static_cast<int>(std::lround(a.count() * roll * mult)));
        t.applyDamage(damage);
    };
    // The enemy stack that deals the most damage (first one on ties).
    const auto pickTarget = [&](int enemySide) -> Fighter* {
        Fighter* best = nullptr;
        double bestDamage = -1;
        for (Fighter& f : fighters) {
            if (f.side != enemySide) continue;
            const UnitStack& s = (*stacks[f.side])[f.index];
            if (s.isEliminated()) continue;
            const double d = averageDamage(s, f.unit);
            if (d > bestDamage) {
                bestDamage = d;
                best = &f;
            }
        }
        return best;
    };

    bool decided = !alive(0) || !alive(1);
    while (!decided && report.rounds < bb.maxRounds) {
        ++report.rounds;
        for (Fighter& f : fighters) f.retaliated = false;
        for (Fighter& f : fighters) {
            if ((*stacks[f.side])[f.index].isEliminated()) continue;
            Fighter* target = pickTarget(1 - f.side);
            if (!target) break;
            const bool ranged = f.unit->range > 0 && f.shotsLeft > 0;
            strike(f, *target);
            if (ranged) {
                --f.shotsLeft;
            } else if (!target->retaliated && !(*stacks[target->side])[target->index].isEliminated()) {
                target->retaliated = true;
                strike(*target, f);
            }
            if (!alive(0) || !alive(1)) {
                decided = true;
                break;
            }
        }
    }

    // Losses, experience (killed HP), cleanup.
    int killedHp[2] = {0, 0};  // HP side s lost
    for (int s = 0; s < 2; ++s) {
        QMap<QString, int> left;
        for (const UnitStack& st : *stacks[s]) left[st.unitTypeId()] += st.count();
        BattleSide& bs = s == 0 ? report.attacker : report.defender;
        for (auto it = startCount[s].begin(); it != startCount[s].end(); ++it) {
            const int lost = it.value() - left.value(it.key());
            if (lost > 0) {
                bs.losses.insert(it.key(), lost);
                killedHp[s] += lost * startHpPerUnit[s].value(it.key());
            }
        }
    }
    const bool attackerAlive = alive(0), defenderAlive = alive(1);
    if (attackerAlive && !defenderAlive) report.outcome = BattleReport::Outcome::AttackerWon;
    else if (!attackerAlive) report.outcome = BattleReport::Outcome::DefenderWon;
    else report.outcome = BattleReport::Outcome::Draw;

    for (int s = 0; s < 2; ++s) {
        armies[s]->removeEmptyStacks();
        for (UnitStack& st : armies[s]->stacks) st.restoreFullHealth();
    }
    if (report.outcome == BattleReport::Outcome::Draw) return report;

    const int winner = report.attackerWon() ? 0 : 1;
    const int loser = 1 - winner;
    if (Hero* h = state.hero(armies[winner]->hero)) {
        report.experience = killedHp[loser] * bb.experiencePerHp;
        report.levelsGained = addExperience(*h, content, report.experience);
    }

    Army* lost = armies[loser];
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
    return o;
}

}  // namespace kot::core
