#include "core/battle/BattleAi.h"

#include <algorithm>

#include "core/content/ContentRegistry.h"
#include "core/magic/Spells.h"

namespace kot::core {

namespace {

double danger(const BattleUnit& u) {
    return u.stack.count() * (u.def->damageMin + u.def->damageMax) / 2.0 * (1.0 + 0.05 * u.def->attack);
}

// How good it is for `me` to strike `target` now: the danger of the units it would
// kill (shooters count more: they hurt from afar), minus what strikes back.
double strikeValue(const Battle& b, const BattleUnit& me, const BattleUnit& target, bool ranged) {
    const BattleBalance& bb = b.content().balance().battle;
    const int diff = (me.def->attack + me.modifier(QStringLiteral("attack"))) - (target.def->defense + target.modifier(QStringLiteral("defense")));
    double mult = 1.0;
    if (diff > 0) mult += std::min(diff * bb.attackPercentPerPoint, bb.maxAttackPercent) / 100.0;
    if (diff < 0) mult -= std::min(-diff * bb.defensePercentPerPoint, bb.maxDefensePercent) / 100.0;
    const double damage = me.stack.count() * (me.def->damageMin + me.def->damageMax) / 2.0 * mult * (me.def->has(QStringLiteral("double_attack")) ? 2 : 1);
    const double kills = std::min<double>(target.stack.count(), damage / std::max(1, target.stack.maxHp()));
    const double perUnit = danger(target) / std::max(1, target.stack.count());
    double value = kills * perUnit * (target.def->range > 0 && target.shotsLeft > 0 ? 1.5 : 1.0);
    if (!ranged && !target.retaliated && !me.def->has(QStringLiteral("no_retaliation"))) {
        const double left = std::max(0.0, target.stack.count() - kills);
        value -= 0.35 * left * perUnit;  // the counter-blow
    }
    return value;
}

// What casting `spell` at `target` is worth, in the same units as strikeValue
// (the danger of enemies killed or held back, or of allies made stronger):
// damage on the target and on everyone in its area, curses on enemies without
// that curse yet, blessings on allies without them, heals on hurt allies.
double spellValue(const Battle& b, const SpellDef& spell, int power, const BattleUnit& target) {
    QList<const BattleUnit*> hit{&target};
    if (spell.area > 0)
        for (const BattleUnit& u : b.units())
            if (&u != &target && u.alive() && u.side == target.side && hex::distance(u.pos, target.pos) <= spell.area) hit.append(&u);
    double value = 0;
    for (const SpellEffect& e : spell.effects) {
        const int strength = effectStrength(e, power);
        for (const BattleUnit* t : hit) {
            const double perUnit = danger(*t) / std::max(1, t->stack.count());
            switch (e.type) {
            case SpellEffect::Type::Damage: {
                const double kills = std::min<double>(t->stack.count(), double(strength) / std::max(1, t->stack.maxHp()));
                value += kills * perUnit * (t->def->range > 0 && t->shotsLeft > 0 ? 1.5 : 1.0);
                break;
            }
            case SpellEffect::Type::Heal: {
                const int missing = t->stack.count() * t->stack.maxHp() - t->stack.totalHp();
                value += std::min(missing, std::max(0, strength)) / double(std::max(1, t->stack.maxHp())) * perUnit;
                break;
            }
            case SpellEffect::Type::ModifyStat: {
                // Already under such a spell: little to gain.
                bool has = false;
                for (const StatModifier& m : t->modifiers) has = has || m.stat == e.stat;
                if (has) break;
                const bool helps = (t->side == target.side) == (spell.target == SpellDef::Target::Ally) ? strength > 0 : strength < 0;
                if (!helps) break;
                const double weight = e.stat == QLatin1String("battleSpeed") ? 0.02 : 0.025;  // about 5% per point, half of it felt (tested against the old AI)
                value += danger(*t) * std::abs(strength) * weight * std::min(3, std::max(1, e.rounds)) / 2.0;
                break;
            }
            default:
                break;
            }
        }
    }
    return value;
}

// Ranged punch still in the stacks of one side (shots left).
double rangedDanger(const Battle& b, int side) {
    double d = 0;
    for (const BattleUnit& u : b.units())
        if (u.alive() && u.side == side && u.def->range > 0 && u.shotsLeft > 0) d += danger(u);
    return d;
}

}  // namespace

BattleAction chooseBattleAction(const Battle& b) {
    BattleAction a;
    const BattleUnit* me = b.activeUnit();
    if (!me) return a;
    if (me->def->has(QStringLiteral("siege_engine"))) {
        // The catapult: the gate first, then the weakest segment near the middle.
        if (b.walls().isEmpty()) return a;  // nothing left to break: wait it out
        Pos best = b.walls().firstKey();
        int bestScore = 1 << 30;
        for (auto it = b.walls().cbegin(); it != b.walls().cend(); ++it) {
            const int score = (it.key() == b.gate() ? -100000 : 0) + it.value() * 4 + std::abs(it.key().y - b.gate().y);
            if (score < bestScore) {
                bestScore = score;
                best = it.key();
            }
        }
        a.type = BattleAction::Type::Shoot;
        a.hex = best;
        a.hasHex = true;
        return a;
    }

    // Enemies, most dangerous first.
    QList<const BattleUnit*> enemies;
    for (const BattleUnit& u : b.units())
        if (u.alive() && u.side != me->side) enemies.append(&u);
    std::stable_sort(enemies.begin(), enemies.end(),
                     [](const BattleUnit* x, const BattleUnit* y) { return danger(*x) > danger(*y); });
    if (enemies.isEmpty()) return a;

    // The hero first: of every spell and target, the one worth most (area damage
    // counts every stack it hits; curses, blessings and heals count too).
    if (const Hero* hero = b.heroOf(me->side); hero && !b.heroHasCast(me->side)) {
        const int power = hero->trait(QStringLiteral("spell_power"));
        QString bestSpell;
        int bestTarget = 0;
        double bestValue = 0;
        for (const QString& id : hero->spells) {
            const SpellDef* s = b.content().spell(id);
            if (!s || s->scope != SpellDef::Scope::Battle || !b.whyCannotCast(id, 0).isEmpty()) continue;
            for (const BattleUnit& t : b.units()) {
                if (!t.alive()) continue;
                if ((s->target == SpellDef::Target::Enemy) != (t.side != me->side)) continue;
                if (!b.whyCannotCast(id, t.id).isEmpty()) continue;
                const double v = spellValue(b, *s, power, t);  // one cast a round: the round's cast is what is scarce
                if (v > bestValue) {
                    bestValue = v;
                    bestSpell = id;
                    bestTarget = t.id;
                }
            }
        }
        if (!bestSpell.isEmpty()) {
            a.type = BattleAction::Type::Cast;
            a.spell = bestSpell;
            a.target = bestTarget;
            return a;
        }
    }

    // The stack's own spells: a damage spell at the most dangerous enemy, a heal on a
    // badly hurt own stack, a curse at the strongest enemy while it has none.
    for (const QString& id : me->def->spells) {
        const SpellDef* s = b.content().spell(id);
        if (!s || !b.whyUnitCannotCast(id, 0).isEmpty()) continue;
        int target = 0;
        for (const SpellEffect& e : s->effects) {
            if (e.type == SpellEffect::Type::Damage && s->target == SpellDef::Target::Enemy) target = enemies.first()->id;
            if (e.type == SpellEffect::Type::ModifyStat && s->target == SpellDef::Target::Enemy && enemies.first()->modifiers.isEmpty())
                target = enemies.first()->id;
            if (e.type == SpellEffect::Type::Heal)
                for (const BattleUnit& u : b.units())
                    if (u.alive() && u.side == me->side && u.stack.totalHp() < u.stack.count() * u.stack.maxHp() - u.stack.maxHp())
                        target = u.id;
        }
        if (target) {
            a.type = BattleAction::Type::UnitCast;
            a.spell = id;
            a.target = target;
            return a;
        }
    }

    if (b.canShoot(*me)) {
        // The enemy in range it hurts most.
        const BattleUnit* bestShot = nullptr;
        double bestShotValue = -1e18;
        for (const BattleUnit* e : enemies)
            if (b.canShootAt(*me, *e)) {
                const double v = strikeValue(b, *me, *e, true);
                if (v > bestShotValue) {
                    bestShotValue = v;
                    bestShot = e;
                }
            }
        if (bestShot) {
            a.type = BattleAction::Type::Shoot;
            a.target = bestShot->id;
            return a;
        }
        // None in range: step to where one is, as far from the enemies as possible.
        const int range = b.shootRange(*me);
        const QHash<Pos, int> reach = b.reachable(*me);
        QList<Pos> hexes = reach.keys();
        std::sort(hexes.begin(), hexes.end());
        Pos best = me->pos;
        int bestSafety = -1;
        for (const Pos p : hexes) {
            int nearest = 1 << 30;
            bool inRange = false;
            for (const BattleUnit* e : enemies) {
                const int d = hex::distance(p, e->pos);
                nearest = std::min(nearest, d);
                inRange = inRange || d <= range;
            }
            if (inRange && nearest > 1 && nearest > bestSafety) {
                bestSafety = nearest;
                best = p;
            }
        }
        if (best != me->pos) {
            a.type = BattleAction::Type::Move;
            a.hex = best;
            a.hasHex = true;
            return a;
        }
    }
    // Of the enemies it can reach, the best blow (a bad trade is still taken when
    // it is the only one: standing still does not win battles).
    const BattleUnit* bestTarget = nullptr;
    Pos bestFrom;
    double bestValue = -1e18;
    for (const BattleUnit* e : enemies) {
        Pos from;
        if (!b.attackHex(*me, *e, &from)) continue;
        const double v = strikeValue(b, *me, *e, false);
        if (v > bestValue) {
            bestValue = v;
            bestTarget = e;
            bestFrom = from;
        }
    }
    if (bestTarget) {
        a.type = BattleAction::Type::Attack;
        a.target = bestTarget->id;
        a.hex = bestFrom;
        a.hasHex = true;
        return a;
    }
    // No blow this turn. When this side out-shoots the other, melee stacks hold
    // back for a few rounds and let the enemy come through the arrows (not when
    // storming walls, and not against an army of shooters, which never comes).
    if (me->def->range <= 0 && b.round() <= 4 && !(b.wallLevel() > 0 && me->side == 0)) {
        const double mine = rangedDanger(b, me->side), theirs = rangedDanger(b, 1 - me->side);
        bool theyHaveMelee = false;
        for (const BattleUnit* e : enemies) theyHaveMelee = theyHaveMelee || e->def->range <= 0;
        if (mine > 0 && mine > theirs * 1.3 && theyHaveMelee) {
            a.type = BattleAction::Type::Defend;
            return a;
        }
    }
    // Ranged units out of shots or blocked still walk in; everybody closes in.
    const QHash<Pos, int> reach = b.reachable(*me);
    Pos best = me->pos;
    int bestDistance = 1 << 30;
    QList<Pos> hexes = reach.keys();
    std::sort(hexes.begin(), hexes.end());
    for (const Pos p : hexes) {
        int d = 1 << 30;
        for (const BattleUnit* e : enemies) d = std::min(d, hex::distance(p, e->pos));
        if (d < bestDistance) {
            bestDistance = d;
            best = p;
        }
    }
    if (best != me->pos) {
        a.type = BattleAction::Type::Move;
        a.hex = best;
        a.hasHex = true;
    }
    return a;  // Defend when it cannot get closer
}

}  // namespace kot::core
