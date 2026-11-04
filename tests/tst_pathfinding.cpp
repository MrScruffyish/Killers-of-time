#include <QtTest>

#include "core/content/ContentRegistry.h"
#include "core/rules/MapGenerator.h"
#include "core/rules/Pathfinding.h"
#include "core/util/Rng.h"

using namespace kot::core;

// Builds a map from text rows: . grass, r road, # mountains, ~ sea, = river, f forest
static GameMap mapFrom(const QStringList& rows) {
    const int h = static_cast<int>(rows.size());
    const int w = static_cast<int>(rows[0].size());
    GameMap map(w, h, "grass");
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const QChar c = rows[y][x];
            QString t = "grass";
            if (c == 'r') t = "road";
            else if (c == '#') t = "mountains";
            else if (c == '~') t = "sea";
            else if (c == '=') t = "river";
            else if (c == 'f') t = "forest";
            map.setTerrain({x, y}, t);
        }
    }
    return map;
}

class TstPathfinding : public QObject {
    Q_OBJECT
    ContentRegistry reg;

    PathResult foot(const QStringList& rows, Pos a, Pos b, const BlockedFn& blocked = {}) {
        const GameMap map = mapFrom(rows);
        return findPath(MoveCostGrid(map, reg, MoveMode::Foot), a, b, blocked);
    }

private slots:
    void initTestCase() {
        QStringList errors;
        QVERIFY2(reg.loadFromDirectory(KOT_DATA_DIR, &errors), qPrintable(errors.join("\n")));
    }

    void stepCostRounding() {
        QCOMPARE(stepCost(100, false), 100);
        QCOMPARE(stepCost(100, true), 141);
        QCOMPARE(stepCost(70, true), 99);
        QCOMPARE(stepCost(150, true), 212);
    }

    void straightLine() {
        const PathResult p = foot({"....."}, {0, 0}, {4, 0});
        QVERIFY(p.found);
        QCOMPARE(p.steps.size(), 4);
        QCOMPARE(p.totalCost, 400);
        QCOMPARE(p.steps.last(), (Pos{4, 0}));
        QCOMPARE(p.cumulativeCost, QList<int>({100, 200, 300, 400}));
    }

    void diagonalCostsMore() {
        const PathResult p = foot({"...", "...", "..."}, {0, 0}, {1, 1});
        QCOMPARE(p.totalCost, 141);
        QCOMPARE(p.steps.size(), 1);
    }

    void startEqualsGoal() {
        const PathResult p = foot({"..."}, {1, 0}, {1, 0});
        QVERIFY(p.found);
        QVERIFY(p.steps.isEmpty());
        QCOMPARE(p.totalCost, 0);
    }

    void outOfBoundsNotFound() {
        QVERIFY(!foot({"..."}, {0, 0}, {5, 0}).found);
        QVERIFY(!foot({"..."}, {-1, 0}, {1, 0}).found);
    }

    void prefersRoad() {
        const PathResult p = foot({".......", "rrrrrrr", "......."}, {0, 1}, {6, 1});
        QVERIFY(p.found);
        QCOMPARE(p.totalCost, 420);
        for (const Pos& s : p.steps) QCOMPARE(s.y, 1);
    }

    void goesAroundMountains() {
        const PathResult p = foot({"..#..", "..#..", "....."}, {0, 0}, {4, 0});
        QVERIFY(p.found);
        QCOMPARE(p.totalCost, 564);  // four diagonal steps through the gap
        for (const Pos& s : p.steps) QVERIFY(!(s.x == 2 && s.y < 2));
    }

    void completeWallBlocks() {
        QVERIFY(!foot({"..#..", "..#..", "..#.."}, {0, 0}, {4, 0}).found);
    }

    void mountainGoalNotEnterable() {
        QVERIFY(!foot({"..#"}, {0, 0}, {2, 0}).found);
    }

    void seaAndRiverNeedBoat() {
        const GameMap sea = mapFrom({"..~~.."});
        const MoveCostGrid footGrid(sea, reg, MoveMode::Foot);
        const MoveCostGrid boatGrid(sea, reg, MoveMode::Boat);
        QVERIFY(!findPath(footGrid, {0, 0}, {5, 0}).found);
        QVERIFY(!findPath(footGrid, {1, 0}, {2, 0}).found);   // cannot walk into the sea
        QVERIFY(findPath(boatGrid, {2, 0}, {3, 0}).found);    // boat sails
        QVERIFY(!findPath(boatGrid, {2, 0}, {1, 0}).found);   // boat cannot enter grass

        const GameMap river = mapFrom({"..=.."});
        QVERIFY(!findPath(MoveCostGrid(river, reg, MoveMode::Foot), {0, 0}, {4, 0}).found);
        QVERIFY(findPath(MoveCostGrid(river, reg, MoveMode::Boat), {2, 0}, {2, 0}).found);
    }

    void blockedTilesAreAvoided() {
        // One-row corridor: a blocker in the middle cuts the path ...
        const auto blockMiddle = [](Pos p) { return p == Pos{1, 0}; };
        QVERIFY(!foot({"..."}, {0, 0}, {2, 0}, blockMiddle).found);
        // ... but with room the path goes around.
        const PathResult p = foot({"...", "..."}, {0, 0}, {2, 0}, blockMiddle);
        QVERIFY(p.found);
        for (const Pos& s : p.steps) QVERIFY(!(s == Pos{1, 0}));
        // The goal itself may be a blocked tile (to visit an object).
        QVERIFY(foot({"..."}, {0, 0}, {2, 0}, [](Pos p) { return p == Pos{2, 0}; }).found);
    }

    void reachableWithinBudget() {
        const GameMap map = mapFrom({".....", ".....", ".....", ".....", "....."});
        const MoveCostGrid grid(map, reg, MoveMode::Foot);
        const QHash<Pos, int> r = reachableTiles(grid, {2, 2}, 200);
        QCOMPARE(r.size(), 13);
        QCOMPARE(r.value({2, 2}), 0);
        QCOMPARE(r.value({1, 1}), 141);
        QCOMPARE(r.value({2, 0}), 200);
        QVERIFY(!r.contains({0, 0}));
        QVERIFY(!r.contains({2, 1}) == false);  // straight neighbour is reachable
    }

    void reachableRespectsBlockedAndTerrain() {
        const GameMap map = mapFrom({".#.", "...", "..."});
        const MoveCostGrid grid(map, reg, MoveMode::Foot);
        const QHash<Pos, int> r =
            reachableTiles(grid, {0, 0}, 100000, [](Pos p) { return p == Pos{2, 1}; });
        QVERIFY(!r.contains({1, 0}));  // mountains
        QVERIFY(!r.contains({2, 1}));  // blocked
        QVERIFY(r.contains({2, 0}));   // reached around the obstacles
    }

    void affordableStepsCountsWholeSteps() {
        const PathResult p = foot({"....."}, {0, 0}, {4, 0});
        QCOMPARE(affordableSteps(p, 350), 3);
        QCOMPARE(affordableSteps(p, 99), 0);
        QCOMPARE(affordableSteps(p, 400), 4);
        QCOMPARE(affordableSteps(p, 100000), 4);
    }

    void aStarAgreesWithDijkstraOnGeneratedMap() {
        MapGenParams params;
        params.width = 48;
        params.height = 48;
        params.seed = 5;
        GameMap map;
        QVERIFY(generateMap(reg, params, &map));
        const MoveCostGrid grid(map, reg, MoveMode::Foot);
        Rng rng(99);
        int found = 0;
        for (int i = 0; i < 40; ++i) {
            const Pos a{rng.nextInt(0, 47), rng.nextInt(0, 47)};
            const Pos b{rng.nextInt(0, 47), rng.nextInt(0, 47)};
            if (grid.cost(a) < 0 || grid.cost(b) < 0) continue;
            const PathResult p = findPath(grid, a, b);
            const QHash<Pos, int> all = reachableTiles(grid, a, 100000000);
            QCOMPARE(p.found, all.contains(b));
            if (p.found) {
                QCOMPARE(p.totalCost, all.value(b));
                ++found;
            }
        }
        QVERIFY(found > 5);
    }
};

QTEST_APPLESS_MAIN(TstPathfinding)
#include "tst_pathfinding.moc"
