#include "core/battle/BattleAi.h"

#include <algorithm>

#include "core/content/ContentRegistry.h"
#include "core/magic/Spells.h"

namespace kot::core {

namespace {

double danger(const BattleUnit& u) {
    return u.stack.count() * (u.def->damageMin + u.def->damageMax) / 2.0 * (1.0 + 0.05 * u.def->attack);
}

}  // namespace

BattleAction chooseBattleAction(const Battle& b) {
    BattleAction a;
    const BattleUnit* me = b.activeUnit();
    if (!me) return a;

    // Enemies, most dangerous first.
    QList<const BattleUnit*> enemies;
    for (const BattleUnit& u : b.units())
        if (u.alive() && u.side != me->side) enemies.append(&u);
    std::stable_sort(enemies.begin(), enemies.end(),
                     [](const BattleUnit* x, const BattleUnit* y) { return danger(*x) > danger(*y); });
    if (enemies.isEmpty()) return a;

    // The hero first: the strongest damage spell at the most dangerous enemy.
    if (const Hero* hero = b.heroOf(me->side); hero && !b.heroHasCast(me->side)) {
        const int power = hero->trait(QStringLiteral("spell_power"));
        QString best;
        int bestDamage = 0;
        for (const QString& id : hero->spells) {
            const SpellDef* s = b.content().spell(id);
            if (!s || s->target != SpellDef::Target::Enemy || !b.whyCannotCast(id, 0).isEmpty()) continue;
            int damage = 0;
            for (const SpellEffect& e : s->effects)
                if (e.type == SpellEffect::Type::Damage) damage += effectStrength(e, power);
            if (damage > bestDamage) {
                bestDamage = damage;
                best = id;
            }
        }
        if (!best.isEmpty()) {
            a.type = BattleAction::Type::Cast;
            a.spell = best;
            a.target = enemies.first()->id;
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
        // The most dangerous enemy in range.
        for (const BattleUnit* e : enemies)
            if (b.canShootAt(*me, *e)) {
                a.type = BattleAction::Type::Shoot;
                a.target = e->id;
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
    for (const BattleUnit* e : enemies) {
        Pos from;
        if (b.attackHex(*me, *e, &from)) {
            a.type = BattleAction::Type::Attack;
            a.target = e->id;
            a.hex = from;
            a.hasHex = true;
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
