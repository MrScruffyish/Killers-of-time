#include "core/rules/Movement.h"

#include <QSet>
#include <algorithm>
#include <climits>

#include "core/content/ContentRegistry.h"
#include "core/battle/AutoBattle.h"
#include "core/magic/Spells.h"
#include "core/rules/Vision.h"

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
                    MoveMode mode, bool deferBattles) {
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
    if (hero->inBoat) {
        mode = MoveMode::Boat;
        // Landing: step onto the land next to the boat; that ends today's movement.
        const TerrainDef* t = content.terrainType(state.map().terrainAt(target));
        if (t && t->walkable && !t->sailable) {
            if (std::max(std::abs(target.x - army->pos.x), std::abs(target.y - army->pos.y)) != 1) {
                result.error = QStringLiteral("sail next to the shore to land");
                return result;
            }
            for (const Army& a : state.armies())
                if (a.id != army->id && a.pos == target) {
                    result.error = QStringLiteral("cannot land there: the tile is occupied");
                    return result;
                }
            if (hero->movementPoints <= 0) {
                result.error = QStringLiteral("not enough movement points");
                return result;
            }
            army->pos = target;
            hero->inBoat = false;
            hero->movementPoints = 0;
            result.ok = result.arrived = true;
            result.stepsTaken = 1;
            const PlayerId owner = hero->owner;
            captureAt(state, owner, target, &result);
            pickUpAt(state, content, heroId, target, &result);
            result.landed = true;
            updateExplored(state, content, state.teamOf(owner));
            return result;
        }
    }

    // Things that block walking through a tile.
    QSet<Pos> occupied;
    for (const Army& a : state.armies()) {
        if (a.id != army->id) occupied.insert(a.pos);
    }
    for (const Town& t : state.towns()) occupied.insert(t.pos);
    for (const Mine& m : state.mines()) occupied.insert(m.pos);
    for (const MapDwelling& d : state.mapDwellings()) occupied.insert(d.pos);
    for (const MapObject& o : state.mapObjects()) occupied.insert(o.pos);

    // What stands on the target tile?
    Town* targetTown = nullptr;

    for (Town& t : state.towns()) {
        if (t.pos == target) targetTown = &t;
    }
    // Hostile armies on the target are attacked: visiting heroes first, then a town's garrison.
    QList<ArmyId> defenders;
    for (const Army& a : state.armies()) {
        if (a.id == army->id || a.pos != target) continue;
        if (targetTown && a.id == targetTown->garrison) continue;  // checked below
        if (!state.isHostile(a.owner, hero->owner)) {
            result.error = QStringLiteral("target tile is occupied");
            return result;
        }
        defenders.append(a.id);
    }
    if (targetTown && state.isHostile(targetTown->owner, hero->owner)) {
        const Army* garrison = state.army(targetTown->garrison);
        if (garrison && garrison->totalUnits() > 0) defenders.append(garrison->id);
    }
    if (!defenders.isEmpty() && army->totalUnits() == 0) {
        result.error = QStringLiteral("the hero has no units to fight with");
        return result;
    }

    const HeroClassDef* cls = content.heroClass(hero->classId);
    const MoveCostGrid grid(state.map(), content, mode,
                            cls ? content.race(cls->race).terrainCostPercent : QMap<QString, int>());
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
    const bool reached = (steps == path.steps.size());
    hero->movementPoints -= result.movementSpent;
    const PlayerId owner = hero->owner;
    const HeroId heroIdCopy = hero->id;
    const ArmyId armyId = army->id;

    if (reached && !defenders.isEmpty()) {
        // Attacking costs the last step; the attacker fights from the tile before.
        if (steps >= 2) army->pos = path.steps[steps - 2];
        result.attacked = true;
        PendingAttack attack{heroIdCopy, owner, armyId, target, defenders};
        if (deferBattles) {
            result.pending = attack;
            updateExplored(state, content, state.teamOf(owner));
            return result;
        }
        bool won = true;
        for (const ArmyId d : defenders) {
            const BattleReport b = resolveBattle(state, content, armyId, d);
            result.battles.append(b);
            if (!b.attackerWon()) {
                won = false;
                break;
            }
        }
        finishAttack(state, content, attack, won, &result);
        return result;
    }
    army->pos = path.steps[steps - 1];
    result.arrived = reached;
    if (result.arrived) {
        captureAt(state, owner, target, &result);
        pickUpAt(state, content, heroIdCopy, target, &result);
        result.learnedSpells = learnSpellsInTown(state, content, heroIdCopy);
    }
    updateExplored(state, content, state.teamOf(owner));
    return result;
}

void captureAt(GameState& state, PlayerId owner, Pos target, MoveResult* result) {
    for (Mine& m : state.mines()) {
        if (m.pos == target && state.isHostile(m.owner, owner)) {
            m.owner = owner;
            result->capturedMine = m.id;
        }
    }
    for (MapDwelling& d : state.mapDwellings()) {
        if (d.pos == target && state.isHostile(d.owner, owner)) {
            d.owner = owner;
            result->capturedDwelling = d.id;
        }
    }
    for (Town& t : state.towns()) {
        if (t.pos == target && state.isHostile(t.owner, owner)) {
            t.owner = owner;
            if (Army* garrison = state.army(t.garrison)) garrison->owner = owner;
            result->capturedTown = t.id;
        }
    }
}

void pickUpAt(GameState& state, const ContentRegistry& content, HeroId heroId, Pos target, MoveResult* result) {
    Hero* hero = state.hero(heroId);
    Player* player = hero ? state.player(hero->owner) : nullptr;
    if (!player) return;
    QList<MapObjectId> taken;
    for (const MapObject& o : state.mapObjects()) {
        if (o.pos != target) continue;
        taken.append(o.id);
        for (auto it = o.resources.begin(); it != o.resources.end(); ++it) {
            player->resources[it.key()] += it.value();
            result->foundResources[it.key()] += it.value();
        }
        if (const ArtifactDef* a = content.artifact(o.artifact)) {
            hero->artifacts.append(a->id);
            for (auto it = a->traits.begin(); it != a->traits.end(); ++it) hero->traits[it.key()] += it.value();
            result->foundArtifacts.append(a->id);
        }
    }
    for (const MapObjectId id : taken) state.mapObjects().remove(id);
}

void finishAttack(GameState& state, const ContentRegistry& content, const PendingAttack& attack, bool won,
                  MoveResult* result) {
    Army* army = state.army(attack.army);
    result->heroKilled = (state.hero(attack.hero) == nullptr);
    bool town = false;
    for (const Town& t : state.towns()) town = town || t.pos == attack.target;
    // A field army stays where it fought from; a won town is entered.
    if (won && army && town) army->pos = attack.target;
    result->arrived = won && army && army->pos == attack.target;
    if (result->arrived) {
        captureAt(state, attack.owner, attack.target, result);
        pickUpAt(state, content, attack.hero, attack.target, result);
        result->learnedSpells = learnSpellsInTown(state, content, attack.hero);
    }
    updateExplored(state, content, state.teamOf(attack.owner));
}

}  // namespace kot::core
