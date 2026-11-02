#include "core/battle/BattleAi.h"

#include <algorithm>

#include "core/content/ContentRegistry.h"

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

    if (b.canShoot(*me)) {
        a.type = BattleAction::Type::Shoot;
        a.target = enemies.first()->id;
        return a;
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
