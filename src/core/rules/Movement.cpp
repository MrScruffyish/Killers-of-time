#include "core/rules/Movement.h"

#include <QSet>
#include <algorithm>
#include <climits>

#include "core/content/ContentRegistry.h"

namespace kot::core {

int armyMovementPoints(const Army& army, const Hero* hero, const ContentRegistry& content) {
    const MovementBalance& mb = content.balance().movement;
    int points = mb.base;
    if (hero) points += mb.perTraitLevel * hero->trait(QStringLiteral("movement_points"));

    int slowest = INT_MAX;
    for (const UnitStack& stack : army.stacks) {
        const UnitDef* unit = content.unit(stack.unitTypeId());
        if (unit && !stack.isEliminated()) slowest = std::min(slowest, unit->mapSpeed);
    }
    if (slowest != INT_MAX) {
        for (const SlowPenaltyStep& step : mb.slowPenalties) {
            if (slowest <= step.mapSpeedAtMost) {
                points -= step.penalty;
                break;
            }
        }
    }
    return std::max(points, mb.minimum);
}

void refreshHeroMovement(GameState& state, const ContentRegistry& content, PlayerId owner) {
    for (Hero& hero : state.heroes()) {
        if (hero.owner != owner) continue;
        const Army* army = state.army(hero.army);
        if (army) hero.movementPoints = armyMovementPoints(*army, &hero, content);
    }
}

MoveResult moveHero(GameState& state, const ContentRegistry& content, HeroId heroId, Pos target,
                    MoveMode mode) {
    MoveResult result;
    Hero* hero = state.hero(heroId);
    Army* army = hero ? state.army(hero->army) : nullptr;
    if (!hero || !army) {
        result.error = QStringLiteral("unknown hero");
        return result;
    }
    if (!state.map().inBounds(target)) {
        result.error = QStringLiteral("target is outside the map");
        return result;
    }

    QSet<Pos> occupied;
    for (const Army& a : state.armies()) {
        if (a.id != army->id) occupied.insert(a.pos);
    }
    for (const Town& t : state.towns()) occupied.insert(t.pos);
    for (const Mine& m : state.mines()) occupied.insert(m.pos);
    if (occupied.contains(target)) {
        result.error = QStringLiteral("target tile is occupied");
        return result;
    }

    const MoveCostGrid grid(state.map(), content, mode);
    const PathResult path =
        findPath(grid, army->pos, target, [&](Pos p) { return occupied.contains(p); });
    if (!path.found) {
        result.error = QStringLiteral("no path to target");
        return result;
    }
    if (path.steps.isEmpty()) {
        result.error = QStringLiteral("already there");
        return result;
    }

    const int steps = affordableSteps(path, hero->movementPoints);
    if (steps == 0) {
        result.error = QStringLiteral("not enough movement points");
        return result;
    }
    result.ok = true;
    result.stepsTaken = steps;
    result.movementSpent = path.cumulativeCost[steps - 1];
    result.arrived = (steps == path.steps.size());
    army->pos = path.steps[steps - 1];
    hero->movementPoints -= result.movementSpent;
    return result;
}

}  // namespace kot::core
