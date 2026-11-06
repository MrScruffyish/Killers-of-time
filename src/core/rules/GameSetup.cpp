#include "core/rules/GameSetup.h"

#include <climits>

#include <QSet>
#include <algorithm>

#include "core/content/ContentRegistry.h"
#include "core/rules/HeroRules.h"
#include "core/rules/MapSites.h"
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

// Steps (8 directions over walkable land) from the nearest of `sources`, and which
// source that is: -1 where none can be reached.
struct Reach {
    QVector<int> steps, owner;
};
Reach walkFrom(const MoveCostGrid& grid, int width, int height, const QList<Pos>& sources) {
    Reach r{QVector<int>(width * height, -1), QVector<int>(width * height, -1)};
    QList<Pos> queue;
    for (int i = 0; i < sources.size(); ++i) {
        const Pos s = sources[i];
        r.steps[s.y * width + s.x] = 0;
        r.owner[s.y * width + s.x] = i;
        queue.append(s);
    }
    for (int head = 0; head < queue.size(); ++head) {
        const Pos p = queue[head];
        const int at = p.y * width + p.x;
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) {
                const Pos n{p.x + dx, p.y + dy};
                if ((!dx && !dy) || !grid.inBounds(n) || grid.cost(n) < 0) continue;
                const int ni = n.y * width + n.x;
                if (r.steps[ni] >= 0) continue;
                r.steps[ni] = r.steps[at] + 1;
                r.owner[ni] = r.owner[at];
                queue.append(n);
            }
    }
    return r;
}

// How fair a set of starts is: the shortest walk between two of them, times how
// evenly the land falls to them (smallest share / largest share), times how evenly far
// each one is from its rivals (nobody in the middle with everyone around).
double startFairness(const MoveCostGrid& grid, int width, int height, const QList<Pos>& starts) {
    int closest = 1000000;
    QVector<double> rivals(starts.size(), 0);  // summed walks to the others
    for (int i = 0; i < starts.size(); ++i) {
        const Reach r = walkFrom(grid, width, height, {starts[i]});
        for (int j = 0; j < starts.size(); ++j) {
            if (j == i) continue;
            int d = r.steps[starts[j].y * width + starts[j].x];
            if (d < 0) d = 1000;
            rivals[i] += d;
            if (j > i) closest = std::min(closest, d);
        }
    }
    const double nearestRivals = *std::min_element(rivals.cbegin(), rivals.cend()), farthestRivals = *std::max_element(rivals.cbegin(), rivals.cend());
    const Reach all = walkFrom(grid, width, height, starts);
    QVector<int> share(starts.size(), 0);
    for (const int o : all.owner)
        if (o >= 0) ++share[o];
    const int least = *std::min_element(share.cbegin(), share.cend()), most = *std::max_element(share.cbegin(), share.cend());
    return closest * (most > 0 ? double(least) / most : 0.0) * (farthestRivals > 0 ? nearestRivals / farthestRivals : 0.0);
}

}  // namespace

bool createGame(const ContentRegistry& content, const GameSetupParams& params, GameState* out,
                QList<PlayerId>* playerIds, QString* error) {
    if (params.players.size() < 2) return fail(error, QStringLiteral("a game needs at least two players"));
    QList<RaceId> raceOf;  // the players' races (the setup names them by key)
    for (const PlayerSetup& p : params.players) {
        const RaceId race = content.raceId(p.race);
        if (!content.playableRaces().contains(race)) return fail(error, QStringLiteral("race '%1' has no hero class").arg(p.race));
        raceOf << race;
    }

    MapGenParams mapParams;
    mapParams.width = params.mapWidth;
    mapParams.height = params.mapHeight;
    mapParams.seed = params.seed;
    if (params.islands) mapParams.islands = static_cast<int>(params.players.size());
    mapParams.rivers = params.rivers >= 0 ? params.rivers : 2 + params.mapWidth * params.mapHeight / 1500;
    mapParams.ridges = params.ridges;
    if (params.seaLevel >= 0) mapParams.seaLevel = params.seaLevel;
    if (params.hillFraction >= 0) mapParams.hillFraction = params.hillFraction;
    if (params.mountainFraction >= 0) mapParams.mountainFraction = params.mountainFraction;
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
                if (grid.inBounds(n) && content.terrain(gs.map().terrainAt(n)).sailable)
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
        // Continent: many tries of spreading the starts out; the fairest set wins (far
        // apart by walking, the land shared evenly), so no seat is worse off.
        double bestFairness = -1;
        for (int attempt = 0; attempt < 24; ++attempt) {
            QList<Pos> tryStarts{candidates[gs.rng().nextInt(0, static_cast<int>(candidates.size()) - 1)]};
            while (tryStarts.size() < count) {
                Pos best = candidates.first();
                int bestDistance = -1;
                for (int i = 0; i < 200; ++i) {
                    const Pos c = candidates[gs.rng().nextInt(0, static_cast<int>(candidates.size()) - 1)];
                    int nearest = 1000000;
                    for (const Pos& s : tryStarts) nearest = std::min(nearest, distance(c, s));
                    if (nearest > bestDistance) {
                        bestDistance = nearest;
                        best = c;
                    }
                }
                tryStarts.append(best);
            }
            const double fairness = startFairness(grid, map.width(), map.height(), tryStarts);
            if (fairness > bestFairness) {
                bestFairness = fairness;
                starts = tryStarts;
            }
        }
        shuffle(starts, gs.rng());  // whatever is left over falls on no seat in particular
    }

    // Each capital in its race's home terrain (the walkable one its heroes cross the
    // cheapest; grass for the others), fading out from the town: same walkability, so
    // nothing above changes.
    {
        const Reach reach = walkFrom(grid, map.width(), map.height(), starts);
        const TerrainId grass = content.terrainId(QStringLiteral("grass")), road = content.terrainId(QStringLiteral("road"));
        for (int i = 0; i < count; ++i) {
            const RaceDef& race = content.race(raceOf[i]);
            TerrainId home = grass;
            int cheapest = 100;
            for (int t = 0; t < race.terrainCostPercent.size(); ++t) {
                const TerrainId id(t);
                if (race.terrainCostPercent[t] < cheapest && content.terrain(id).walkable && id != road && !content.terrain(id).sailable) {
                    cheapest = race.terrainCostPercent[t];
                    home = id;
                }
            }
            if (!home.isValid()) continue;
            for (int dy = -7; dy <= 7; ++dy)
                for (int dx = -7; dx <= 7; ++dx) {
                    const Pos p{starts[i].x + dx, starts[i].y + dy};
                    const int d = std::max(std::abs(dx), std::abs(dy));
                    if (d < 2 || !grid.inBounds(p) || grid.cost(p) < 0 || reach.owner[p.y * map.width() + p.x] != i) continue;
                    if (gs.map().terrainAt(p) == road) continue;
                    if (gs.rng().nextInt(0, 99) < 100 - d * 11) gs.map().setTerrain(p, home);
                }
        }
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
                                         raceOf[i], ps.color);
        gs.player(id)->aiProfile = ps.aiProfile;
        Player* player = gs.player(id);
        player->resources = content.balance().economy.startingResources;
        if (!ps.isAi) {
            for (const auto [r, amount] : ResourceAmounts(player->resources)) player->resources[r] = amount * level.humanStartPercent / 100;
        } else {
            const DifficultyLevel slot = content.balance().aiSlot.value(ps.aiDifficulty);
            player->incomePercent = level.aiIncomePercent + slot.aiIncomePercent;
            player->growthPercent = level.aiGrowthPercent + slot.aiGrowthPercent;
        }
        ids.append(id);

        gs.addTown(QStringLiteral("%1's capital").arg(ps.name), raceOf[i], starts[i], id);

        const QList<HeroTemplateId> free = freeHeroTemplates(gs, content, raceOf[i]);
        const HeroTemplateDef* tmpl = free.isEmpty() ? nullptr
            : content.heroTemplate(free[gs.rng().nextInt(0, static_cast<int>(free.size()) - 1)]);
        HeroClassId classId;
        for (const HeroClassDef& c : content.heroClasses()) {
            if (c.race == raceOf[i] && !classId.isValid()) classId = c.id;
        }
        const HeroId hero = createHero(gs, content, id, tmpl ? tmpl->name : ps.name, tmpl ? tmpl->classId : classId, starts[i], error);
        if (!hero.isValid()) return false;
        Hero* h = gs.hero(hero);
        if (tmpl) giveHeroTemplate(*h, content, tmpl->id);
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
    const ResourceId currency = content.currency();
    for (const ResourceDef& r : content.resources()) {
        const int mines = std::max(1, area / (r.id == currency ? 500 : 900));
        const int perDay = (r.id == currency) ? 1000 : (r.value >= 300 ? 1 : 2);
        for (int i = 0; i < mines; ++i) {
            Pos where;
            if (place(&where)) gs.addMine(r.id, perDay, where);
        }
    }
    QList<UnitTypeId> growers;
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
    QList<UnitTypeId> monsterUnits;
    for (const UnitDef& u : content.units())
        if (!u.has(Ability::SiegeEngine)) monsterUnits.append(u.id);  // no wandering catapults
    const QSet<Pos> landSet(land.begin(), land.end());
    const auto addMonsters = [&](Pos where, int strength) {
        QList<UnitTypeId> fitting;
        for (const UnitTypeId id : monsterUnits) {
            const int tier = content.unit(id)->tier;
            if (tier >= strength && tier <= strength + 1) fitting.append(id);
        }
        if (fitting.isEmpty()) fitting = monsterUnits;
        if (fitting.isEmpty()) return;
        const UnitDef* unit = content.unit(fitting[gs.rng().nextInt(0, static_cast<int>(fitting.size()) - 1)]);
        const int count = std::max(1, gs.rng().nextInt(10, 20) * strength * level.monsterPercent / 100 * params.monsterPercent / (200 * unit->tier));
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
        if (m.resource == currency || (mineIndex++ % 2 == 0)) guard(m.pos, m.resource == currency ? 2 : 1);

    // The same start kit for every player, a fair walk from the capital on its own
    // side of the map: wood, stone and a (lightly guarded) gold mine, two treasures and
    // a dwelling of a weak unit. The rest of the map is scattered above and below.
    {
        const Reach reach = walkFrom(grid, map.width(), map.height(), starts);
        UnitTypeId kitUnit;  // the same for everyone
        {
            QList<UnitTypeId> weak;
            for (const UnitDef& u : content.units())
                if (u.weeklyGrowth > 0 && u.tier == 1 && !u.has(Ability::SiegeEngine)) weak.append(u.id);
            if (!weak.isEmpty()) kitUnit = weak[gs.rng().nextInt(0, static_cast<int>(weak.size()) - 1)];
        }
        const ResourceId wood = content.resourceId(QStringLiteral("wood")), stone = content.resourceId(QStringLiteral("stone"));
        ArtifactId kitArtifact;  // the start quest's reward, the same for everyone
        {
            QList<ArtifactId> minor;
            for (const ArtifactDef& a : content.artifacts())
                if (a.value <= 1) minor << a.id;
            if (!minor.isEmpty()) kitArtifact = minor[gs.rng().nextInt(0, static_cast<int>(minor.size()) - 1)];
        }
        for (int i = 0; i < count; ++i) {
            QList<Pos> ring;  // own side, 5..12 steps away, room around
            for (const Pos& p : land) {
                const int at = p.y * map.width() + p.x;
                if (reach.owner[at] == i && reach.steps[at] >= 5 && reach.steps[at] <= 12 && roomAround(grid, p)) ring.append(p);
            }
            shuffle(ring, gs.rng());
            const auto take = [&](Pos* where) {
                for (const Pos& p : ring) {
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
            Pos where;
            if (wood.isValid() && take(&where)) gs.addMine(wood, 2, where);
            if (stone.isValid() && take(&where)) gs.addMine(stone, 2, where);
            if (take(&where)) {
                gs.addMine(currency, 1000, where);
                guard(where, 1);
            }
            for (int t = 0; t < 2; ++t)
                if (take(&where)) {
                    MapObject o;
                    o.kind = MapObject::Kind::Treasure;
                    o.pos = where;
                    o.resources[t == 0 ? currency : (wood.isValid() ? wood : currency)] = t == 0 ? 1000 : 6;
                    gs.addMapObject(o);
                }
            if (kitUnit.isValid() && take(&where)) gs.addMapDwelling(kitUnit, where, content.unit(kitUnit)->weeklyGrowth);
            // A hermit with the same wish for everyone: come back at level 4.
            if (take(&where)) {
                MapObject o;
                o.kind = MapObject::Kind::Site;
                o.pos = where;
                o.site = QStringLiteral("quest_hut");
                o.quest.needLevel = 4;
                o.quest.rewardResources[currency] = 2500;
                o.quest.rewardArtifact = kitArtifact;
                gs.addMapObject(o);
            }
        }
    }

    // Guarded borders: where the shortest walk from one capital to another leaves its
    // owner's side of the map, a strong group of monsters stands (a safe start, and a
    // real fight to get through).
    {
        const Reach reach = walkFrom(grid, map.width(), map.height(), starts);
        QList<Pos> borders;
        for (int i = 0; i < count; ++i) {
            const Reach from = walkFrom(grid, map.width(), map.height(), {starts[i]});
            for (int j = 0; j < count; ++j) {
                if (j == i || from.steps[starts[j].y * map.width() + starts[j].x] < 0) continue;
                // Back from the other capital along falling steps; the last tile not ours is the border.
                Pos p = starts[j], border{-1, -1};
                for (int guardSteps = 0; guardSteps < 10000; ++guardSteps) {
                    const int here = from.steps[p.y * map.width() + p.x];
                    if (here <= 0) break;
                    if (reach.owner[p.y * map.width() + p.x] != i) border = p;
                    Pos next = p;
                    for (int dy = -1; dy <= 1 && next == p; ++dy)
                        for (int dx = -1; dx <= 1 && next == p; ++dx) {
                            const Pos n{p.x + dx, p.y + dy};
                            if (grid.inBounds(n) && from.steps[n.y * map.width() + n.x] == here - 1) next = n;
                        }
                    if (next == p) break;
                    p = next;
                }
                if (border.x < 0) continue;
                bool near = false;
                for (const Pos& b : borders) near = near || distance(b, border) < 6;
                for (const Pos& u : used) near = near || u == border;
                if (near) continue;
                borders.append(border);
                used.append(border);
                addMonsters(border, 3);
                // Three times a normal strong group.
                for (Army& a : gs.armies())
                    if (a.pos == border && !a.owner.isValid())
                        for (UnitStack& st : a.stacks) st.addUnits(2 * st.count());
            }
        }
    }

    // Neutral towns to conquer, each with a garrison and a few buildings.
    const QList<RaceId> raceList = content.playableRaces();
    const int neutralTowns = std::max(1, static_cast<int>(params.players.size()) / 2) + area / 2500;
    for (int i = 0; i < neutralTowns && !raceList.isEmpty(); ++i) {
        Pos where;
        if (!place(&where)) break;
        const RaceId race = raceList[gs.rng().nextInt(0, static_cast<int>(raceList.size()) - 1)];
        const TownId t = gs.addTown(QStringLiteral("Free town %1").arg(i + 1), race, where);
        Town* town = gs.town(t);
        // The cheapest dwelling of the race and the village hall, so it is worth taking.
        const BuildingDef* dwelling = nullptr;
        for (const BuildingDef& b : content.buildings()) {
            if (b.race != race || !b.requiredBuildings.isEmpty() || b.upgradeOf.isValid()) continue;
            for (const BuildingEffect& e : b.effects)
                if (e.type == BuildingEffect::Type::Dwelling && !dwelling) dwelling = &b;
        }
        if (const BuildingTypeId hall = content.buildingId(QStringLiteral("village_hall")); hall.isValid()) town->buildings << hall;
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
    QList<ResourceId> resources;
    for (const ResourceDef& r : content.resources()) resources.append(r.id);
    for (int i = 0, n = std::max(4, area / 250); i < n; ++i) {
        Pos where;
        if (!place(&where)) break;
        MapObject o;
        o.kind = MapObject::Kind::Treasure;
        o.pos = where;
        const ResourceId res = resources[gs.rng().nextInt(0, static_cast<int>(resources.size()) - 1)];
        o.resources[res] = res == currency ? 100 * gs.rng().nextInt(5, 15)
                                           : (content.resource(res).value >= 300 ? gs.rng().nextInt(2, 4) : gs.rng().nextInt(4, 10));
        gs.addMapObject(o);
    }
    QList<ArtifactId> artifactIds;
    for (const ArtifactDef& a : content.artifacts()) artifactIds << a.id;
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

    // Places to visit: a few of each (MapSites.h), unguarded.
    for (const QString& site : siteIds()) {
        if (site == QLatin1String("quest_hut")) continue;  // with their quests, below
        const int n = std::max(1, area / (site == QLatin1String("obelisk") ? 2500 : 1600));
        for (int i = 0; i < n; ++i) {
            Pos where;
            if (!place(&where)) break;
            MapObject o;
            o.kind = MapObject::Kind::Site;
            o.pos = where;
            o.site = site;
            gs.addMapObject(o);
        }
    }

    // Quest huts in neutral land: bring an artifact that lies somewhere on the map, some
    // rare resources, or a strong hero; well paid.
    {
        QList<ArtifactId> onMap;
        for (const MapObject& o : gs.mapObjects())
            if (o.kind == MapObject::Kind::Artifact) onMap << o.artifact;
        QList<ArtifactId> strong;
        for (const ArtifactDef& a : content.artifacts())
            if (a.value >= 2) strong << a.id;
        QList<ResourceId> rare;
        for (const ResourceDef& r : content.resources())
            if (r.value >= 300) rare << r.id;
        for (int i = 0, n = std::max(1, area / 2500); i < n; ++i) {
            Pos where;
            if (!place(&where)) break;
            MapObject o;
            o.kind = MapObject::Kind::Site;
            o.pos = where;
            o.site = QStringLiteral("quest_hut");
            Quest& q = o.quest;
            const int kind = gs.rng().nextInt(0, 2);
            if (kind == 0 && !onMap.isEmpty()) {
                q.needArtifact = onMap[gs.rng().nextInt(0, static_cast<int>(onMap.size()) - 1)];
                q.rewardResources[currency] = 5000;
                q.rewardExperience = 3000;
            } else if (kind == 1 && !rare.isEmpty()) {
                q.needResources[rare[gs.rng().nextInt(0, static_cast<int>(rare.size()) - 1)]] = 8;
                q.rewardResources[currency] = 6000;
            } else {
                q.needLevel = 8;
                if (!strong.isEmpty()) q.rewardArtifact = strong[gs.rng().nextInt(0, static_cast<int>(strong.size()) - 1)];
                q.rewardExperience = 2000;
            }
            gs.addMapObject(o);
        }
    }

    // Wandering monsters.
    for (int i = 0, n = std::max(3, area / 400); i < n && !monsterUnits.isEmpty(); ++i) {
        Pos where;
        if (!place(&where)) break;
        addMonsters(where, 1 + gs.rng().nextInt(0, 1));
    }

    // Roads join the towns: each to its nearest neighbours (a spanning tree),
    // around everything standing on the map; where a road meets a river it
    // becomes a bridge.
    if (params.roads) {
        QSet<Pos> blockedTiles;
        for (const Town& t : gs.towns())
            for (const Pos p : townTiles(t.pos, gs.map().width(), gs.map().height()))
                if (p != t.pos) blockedTiles.insert(p);
        for (const Mine& m : gs.mines()) blockedTiles.insert(m.pos);
        for (const MapDwelling& d : gs.mapDwellings()) blockedTiles.insert(d.pos);
        for (const MapObject& o : gs.mapObjects()) blockedTiles.insert(o.pos);
        for (const Army& a : gs.armies()) blockedTiles.insert(a.pos);
        GameMap roadMap = gs.map();  // rivers count as crossable (bridges) for the road builder
        for (int y = 0; y < roadMap.height(); ++y)
            for (int x = 0; x < roadMap.width(); ++x)
                if (roadMap.terrainAt({x, y}) == content.terrainId(QStringLiteral("river")))
                    roadMap.setTerrain({x, y}, content.terrainId(QStringLiteral("swamp")));
        const MoveCostGrid roadGrid(roadMap, content, MoveMode::Foot);
        QList<Pos> towns;
        for (const Town& t : gs.towns()) towns.append(t.pos);
        QList<Pos> joined;
        if (!towns.isEmpty()) joined.append(towns.takeFirst());
        while (!towns.isEmpty()) {
            // The closest pair between the joined towns and the rest.
            int bestI = 0, bestJ = 0, bestD = INT_MAX;
            for (int i = 0; i < towns.size(); ++i)
                for (int j = 0; j < joined.size(); ++j) {
                    const int d = distance(towns[i], joined[j]);
                    if (d < bestD) {
                        bestD = d;
                        bestI = i;
                        bestJ = j;
                    }
                }
            const Pos from = towns.takeAt(bestI);
            const PathResult road = findPath(roadGrid, from, joined[bestJ], [&](Pos p) { return blockedTiles.contains(p); });
            joined.append(from);
            if (!road.found || bestD > gs.map().width()) continue;  // across the sea, or too far
            for (const Pos p : road.steps) {
                if (gs.townCovering(p)) continue;
                gs.map().setTerrain(p, content.terrainId(QStringLiteral("road")));
            }
        }
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
