#include "core/rules/GameSetup.h"

#include <QSet>
#include <algorithm>

#include "core/content/ContentRegistry.h"
#include "core/rules/HeroRules.h"
#include "core/rules/Movement.h"
#include "core/rules/Vision.h"
#include "core/rules/MapGenerator.h"
#include "core/rules/Pathfinding.h"
#include "core/rules/Tavern.h"
#include "core/rules/TurnCycle.h"

namespace kot::core {

namespace {

bool fail(QString* error, const QString& message) {
    if (error) *error = message;
    return false;
}

int distance(Pos a, Pos b) { return std::max(std::abs(a.x - b.x), std::abs(a.y - b.y)); }

// Fisher-Yates with the game's own random numbers (reproducible).
template <typename T>
void shuffle(QList<T>& list, Rng& rng) {
    for (int i = static_cast<int>(list.size()) - 1; i > 0; --i)
        std::swap(list[i], list[rng.nextInt(0, i)]);
}

// Walkable tiles of the biggest connected land area, in a fixed order.
QList<Pos> largestLandRegion(const GameMap& map, const MoveCostGrid& grid) {
    QSet<Pos> seen;
    QList<Pos> best;
    for (int y = 0; y < map.height(); ++y) {
        for (int x = 0; x < map.width(); ++x) {
            const Pos p{x, y};
            if (grid.cost(p) < 0 || seen.contains(p)) continue;
            const QHash<Pos, int> region = reachableTiles(grid, p, 1000000000);
            QList<Pos> tiles = region.keys();
            for (const Pos& t : tiles) seen.insert(t);
            if (tiles.size() > best.size()) best = tiles;
        }
    }
    std::sort(best.begin(), best.end());
    return best;
}

// Every walkable piece of land with at least `minSize` tiles, biggest first.
QList<QList<Pos>> landRegions(const GameMap& map, const MoveCostGrid& grid, int minSize) {
    QSet<Pos> seen;
    QList<QList<Pos>> regions;
    for (int y = 0; y < map.height(); ++y)
        for (int x = 0; x < map.width(); ++x) {
            const Pos p{x, y};
            if (grid.cost(p) < 0 || seen.contains(p)) continue;
            QList<Pos> tiles = reachableTiles(grid, p, 1000000000).keys();
            for (const Pos& t : tiles) seen.insert(t);
            std::sort(tiles.begin(), tiles.end());
            if (tiles.size() >= minSize) regions.append(tiles);
        }
    std::stable_sort(regions.begin(), regions.end(), [](const QList<Pos>& a, const QList<Pos>& b) { return a.size() > b.size(); });
    return regions;
}

bool roomAround(const MoveCostGrid& grid, Pos p) {
    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx) {
            const Pos n{p.x + dx, p.y + dy};
            if (!grid.inBounds(n) || grid.cost(n) < 0) return false;
        }
    return true;
}

}  // namespace

bool createGame(const ContentRegistry& content, const GameSetupParams& params, GameState* out,
                QList<PlayerId>* playerIds, QString* error) {
    if (params.players.size() < 2) return fail(error, QStringLiteral("a game needs at least two players"));
    for (const PlayerSetup& p : params.players) {
        bool hasClass = false;
        for (const HeroClassDef& c : content.heroClasses()) hasClass = hasClass || c.race == p.race;
        if (!hasClass) return fail(error, QStringLiteral("race '%1' has no hero class").arg(p.race));
    }

    MapGenParams mapParams;
    mapParams.width = params.mapWidth;
    mapParams.height = params.mapHeight;
    mapParams.seed = params.seed;
    if (params.islands) mapParams.islands = static_cast<int>(params.players.size());
    GameMap map;
    QString mapError;
    if (!generateMap(content, mapParams, &map, &mapError)) return fail(error, mapError);

    GameState gs(params.seed ^ 0xA5A5A5A5A5A5A5A5ULL);
    gs.setMap(map);
    const MoveCostGrid grid(gs.map(), content, MoveMode::Foot);
    const int count = static_cast<int>(params.players.size());
    // Islands: all the bigger land pieces, one start on each of the biggest.
    QList<QList<Pos>> regions = params.islands ? landRegions(gs.map(), grid, 30) : QList<QList<Pos>>{largestLandRegion(gs.map(), grid)};
    if (params.islands && regions.size() < count)
        return fail(error, QStringLiteral("the islands map has only %1 islands for %2 players").arg(regions.size()).arg(count));
    QList<Pos> land;
    for (const QList<Pos>& r : regions) land << r;
    std::sort(land.begin(), land.end());

    // Start positions: spread out over land that has room around it (on islands: near
    // the shore of the player's own island, so a shipyard can be built).
    const auto nearWater = [&](Pos p) {
        for (int dy = -2; dy <= 2; ++dy)
            for (int dx = -2; dx <= 2; ++dx) {
                const Pos n{p.x + dx, p.y + dy};
                if (grid.inBounds(n) && content.terrainType(gs.map().terrainAt(n)) &&
                    content.terrainType(gs.map().terrainAt(n))->sailable)
                    return true;
            }
        return false;
    };
    QList<Pos> candidates;
    for (const Pos& p : land) {
        if (p.x >= 2 && p.y >= 2 && p.x < map.width() - 2 && p.y < map.height() - 2 && roomAround(grid, p))
            candidates.append(p);
    }
    if (candidates.size() < count) return fail(error, QStringLiteral("this map has too little land for %1 players").arg(count));
    QList<Pos> starts;
    if (params.islands) {
        for (int i = 0; i < count; ++i) {
            QList<Pos> own;
            for (const Pos& p : regions[i])
                if (p.x >= 2 && p.y >= 2 && p.x < map.width() - 2 && p.y < map.height() - 2 && roomAround(grid, p) && nearWater(p))
                    own.append(p);
            if (own.isEmpty()) return fail(error, QStringLiteral("an island has no room for a town"));
            std::sort(own.begin(), own.end());
            starts.append(own[gs.rng().nextInt(0, static_cast<int>(own.size()) - 1)]);
        }
    } else {
        starts.append(candidates[gs.rng().nextInt(0, static_cast<int>(candidates.size()) - 1)]);
    }
    while (starts.size() < count) {  // (continent maps)
        Pos best = candidates.first();
        int bestDistance = -1;
        for (int i = 0; i < 300; ++i) {
            const Pos c = candidates[gs.rng().nextInt(0, static_cast<int>(candidates.size()) - 1)];
            int nearest = 1000000;
            for (const Pos& s : starts) nearest = std::min(nearest, distance(c, s));
            if (nearest > bestDistance) {
                bestDistance = nearest;
                best = c;
            }
        }
        starts.append(best);
    }

    gs.setDifficulty(params.difficulty);
    DifficultyLevel level = content.balance().level(params.difficulty);
    if (!content.balance().difficulty.contains(params.difficulty)) level = DifficultyLevel();

    // Players, towns, heroes.
    QMap<int, TeamId> teams;
    QList<PlayerId> ids;
    for (int i = 0; i < count; ++i) {
        const PlayerSetup& ps = params.players[i];
        if (!teams.contains(ps.team)) teams.insert(ps.team, gs.newTeam());
        const PlayerId id = gs.addPlayer(ps.name, teams[ps.team],
                                         ps.isAi ? Player::Controller::Ai : Player::Controller::Human,
                                         ps.race, ps.color);
        gs.player(id)->aiProfile = ps.aiProfile;
        Player* player = gs.player(id);
        player->resources = content.balance().economy.startingResources;
        if (!ps.isAi) {
            for (auto it = player->resources.begin(); it != player->resources.end(); ++it)
                it.value() = it.value() * level.humanStartPercent / 100;
        } else {
            const DifficultyLevel slot = content.balance().aiSlot.value(ps.aiDifficulty);
            player->incomePercent = level.aiIncomePercent + slot.aiIncomePercent;
            player->growthPercent = level.aiGrowthPercent + slot.aiGrowthPercent;
        }
        ids.append(id);

        gs.addTown(QStringLiteral("%1's capital").arg(ps.name), ps.race, starts[i], id);

        const QStringList free = freeHeroTemplates(gs, content, ps.race);
        const HeroTemplateDef* tmpl = free.isEmpty() ? nullptr
            : content.heroTemplate(free[gs.rng().nextInt(0, static_cast<int>(free.size()) - 1)]);
        QString classId;
        for (const HeroClassDef& c : content.heroClasses()) {
            if (c.race == ps.race && classId.isEmpty()) classId = c.id;
        }
        const HeroId hero = createHero(gs, content, id, tmpl ? tmpl->name : ps.name, tmpl ? tmpl->classId : classId, starts[i], error);
        if (!hero.isValid()) return false;
        Hero* h = gs.hero(hero);
        if (tmpl) h->templateId = tmpl->id;
        const HeroClassDef* cls = content.heroClass(h->classId);
        Army* army = gs.army(h->army);
        for (auto it = cls->startingArmy.begin(); it != cls->startingArmy.end(); ++it) {
            const UnitDef* unit = content.unit(it.key());
            if (unit) army->addUnits(unit->id, it.value(), unit->maxHp);
        }
    }

    // Neutral mines and dwellings on land away from the start positions.
    QList<Pos> spots;
    for (const Pos& p : land) {
        bool near = false;
        for (const Pos& s : starts) near = near || distance(p, s) < 4;
        if (!near) spots.append(p);
    }
    shuffle(spots, gs.rng());
    QList<Pos> used = starts;
    const auto place = [&](Pos* where) {
        for (const Pos& p : spots) {
            bool free = true;
            for (const Pos& u : used) free = free && distance(p, u) >= 3;
            if (free) {
                *where = p;
                used.append(p);
                return true;
            }
        }
        return false;
    };
    const int area = static_cast<int>(land.size());
    const QString currency = content.currency() ? content.currency()->id : QString();
    for (const ResourceDef& r : content.resources()) {
        const int mines = std::max(1, area / (r.id == currency ? 500 : 900));
        const int perDay = (r.id == currency) ? 1000 : (r.value >= 300 ? 1 : 2);
        for (int i = 0; i < mines; ++i) {
            Pos where;
            if (place(&where)) gs.addMine(r.id, perDay, where);
        }
    }
    QStringList growers;
    for (const UnitDef& u : content.units()) {
        if (u.weeklyGrowth > 0) growers.append(u.id);
    }
    for (int i = 0, n = std::max(2, area / 700); i < n && !growers.isEmpty(); ++i) {
        Pos where;
        if (!place(&where)) break;
        const UnitDef* unit = content.unit(growers[gs.rng().nextInt(0, static_cast<int>(growers.size()) - 1)]);
        gs.addMapDwelling(unit->id, where, unit->weeklyGrowth);
    }

    // Neutral monsters. `strength` 1 (weak) .. 3 (strong) picks the tiers and the size.
    QStringList monsterUnits;
    for (const UnitDef& u : content.units()) monsterUnits.append(u.id);
    const QSet<Pos> landSet(land.begin(), land.end());
    const auto addMonsters = [&](Pos where, int strength) {
        QStringList fitting;
        for (const QString& id : monsterUnits) {
            const int tier = content.unit(id)->tier;
            if (tier >= strength && tier <= strength + 1) fitting.append(id);
        }
        if (fitting.isEmpty()) fitting = monsterUnits;
        if (fitting.isEmpty()) return;
        const UnitDef* unit = content.unit(fitting[gs.rng().nextInt(0, static_cast<int>(fitting.size()) - 1)]);
        const int count = std::max(1, gs.rng().nextInt(10, 20) * strength * level.monsterPercent / (200 * unit->tier));
        gs.army(gs.addArmy(PlayerId(), where))->addUnits(unit->id, count, unit->maxHp);
    };
    // A guard on a free land tile next to `object`.
    const auto guard = [&](Pos object, int strength) {
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) {
                const Pos p{object.x + dx, object.y + dy};
                if ((dx || dy) && landSet.contains(p) && !used.contains(p)) {
                    used.append(p);
                    addMonsters(p, strength);
                    return;
                }
            }
    };
    // Every other non-gold mine and every gold mine is guarded.
    int mineIndex = 0;
    for (const Mine& m : QList<Mine>(gs.mines().values()))
        if (m.resourceId == currency || (mineIndex++ % 2 == 0)) guard(m.pos, m.resourceId == currency ? 2 : 1);

    // Neutral towns to conquer, each with a garrison and a few buildings.
    const QStringList raceList = content.raceIds();
    const int neutralTowns = std::max(1, static_cast<int>(params.players.size()) / 2) + area / 2500;
    for (int i = 0; i < neutralTowns && !raceList.isEmpty(); ++i) {
        Pos where;
        if (!place(&where)) break;
        const QString race = raceList[gs.rng().nextInt(0, static_cast<int>(raceList.size()) - 1)];
        const TownId t = gs.addTown(QStringLiteral("Free town %1").arg(i + 1), race, where);
        Town* town = gs.town(t);
        // The cheapest dwelling of the race and the village hall, so it is worth taking.
        const BuildingDef* dwelling = nullptr;
        for (const BuildingDef& b : content.buildings()) {
            if (b.race != race || !b.requiredBuildings.isEmpty() || !b.upgradeOf.isEmpty()) continue;
            for (const BuildingEffect& e : b.effects)
                if (e.type == BuildingEffect::Type::Dwelling && !dwelling) dwelling = &b;
        }
        if (content.building(QStringLiteral("village_hall"))) town->buildings << QStringLiteral("village_hall");
        if (dwelling) {
            town->buildings << dwelling->id;
            for (const BuildingEffect& e : dwelling->effects)
                if (const UnitDef* u = content.unit(e.unit)) {
                    const int count = std::max(4, u->weeklyGrowth * 2 * level.monsterPercent / 100);
                    gs.army(town->garrison)->addUnits(u->id, count, u->maxHp);
                }
        }
    }

    // Treasure piles and guarded artifacts.
    QStringList resources;
    for (const ResourceDef& r : content.resources()) resources.append(r.id);
    for (int i = 0, n = std::max(4, area / 250); i < n; ++i) {
        Pos where;
        if (!place(&where)) break;
        MapObject o;
        o.kind = MapObject::Kind::Treasure;
        o.pos = where;
        const QString res = resources[gs.rng().nextInt(0, static_cast<int>(resources.size()) - 1)];
        const ResourceDef* rd = content.resource(res);
        o.resources[res] = res == currency ? 100 * gs.rng().nextInt(5, 15)
                                           : (rd && rd->value >= 300 ? gs.rng().nextInt(2, 4) : gs.rng().nextInt(4, 10));
        gs.addMapObject(o);
    }
    const QStringList artifactIds = content.artifacts().keys();
    for (int i = 0, n = std::max(2, area / 600); i < n && !artifactIds.isEmpty(); ++i) {
        Pos where;
        if (!place(&where)) break;
        MapObject o;
        o.kind = MapObject::Kind::Artifact;
        o.pos = where;
        o.artifact = artifactIds[gs.rng().nextInt(0, static_cast<int>(artifactIds.size()) - 1)];
        gs.addMapObject(o);
        guard(where, std::clamp(content.artifact(o.artifact)->value + 1, 1, 3));
    }

    // Wandering monsters.
    for (int i = 0, n = std::max(3, area / 400); i < n && !monsterUnits.isEmpty(); ++i) {
        Pos where;
        if (!place(&where)) break;
        addMonsters(where, 1 + gs.rng().nextInt(0, 1));
    }

    // Everybody starts with full movement (armies now exist) and a look around.
    for (const PlayerId id : ids) {
        refreshHeroMovement(gs, content, id);
        updateExplored(gs, content, gs.teamOf(id));
    }
    beginGame(gs, content);
    *out = std::move(gs);
    if (playerIds) *playerIds = ids;
    return true;
}

}  // namespace kot::core
