#include <QtTest>

#include "TestContent.h"

#include "core/content/ContentRegistry.h"
#include "core/rules/Boats.h"
#include "core/rules/GameSetup.h"
#include "core/rules/Pathfinding.h"
#include "core/rules/HeroRules.h"
#include "core/rules/Movement.h"
#include "core/rules/TownRules.h"

using namespace kot::core;

// Land on the left (x < 5), sea on the right; the town stands at the shore.
class TstBoats : public QObject {
    Q_OBJECT
    ContentRegistry reg;
    GameState gs;
    PlayerId p1;
    TownId town;
    HeroId hero;

private slots:
    void initTestCase() {
        QStringList errors;
        QVERIFY2(reg.loadFromDirectory(KOT_DATA_DIR, &errors), qPrintable(errors.join("\n")));
        useContent(reg);
    }
    void init() {
        gs = GameState(2);
        gs.setMap(GameMap(16, 8, T("grass")));
        for (int x = 5; x < 16; ++x)
            for (int y = 0; y < 8; ++y) gs.map().setTerrain({x, y}, T("sea"));
        p1 = gs.addPlayer("A", gs.newTeam(), Player::Controller::Human, RC("humans"), "#ff0000");
        gs.setTurnOrder({p1});
        gs.player(p1)->resources = amounts({{"gold", 20000}, {"wood", 50}, {"stone", 50}});
        town = gs.addTown("Port", RC("humans"), {4, 4}, p1);
        hero = createHero(gs, reg, p1, "H", C("knight"), {4, 4});
        gs.army(gs.hero(hero)->army)->addUnits(U("pikeman"), 5, 10);
        gs.hero(hero)->movementPoints = 2000;
    }

    void shipyardsNeedTheCoast() {
        gs.town(town)->buildings << B("village_hall");
        QVERIFY(whyCannotBuild(gs, reg, town, B("shipyard"), p1).isEmpty());
        const TownId inland = gs.addTown("Inland", RC("humans"), {1, 1}, p1);
        gs.town(inland)->buildings << B("village_hall");
        QVERIFY(whyCannotBuild(gs, reg, inland, B("shipyard"), p1).contains("coast"));
        // Boats on all the water near it: still at the coast (only buying a boat waits).
        const TownId cove = gs.addTown("Cove", RC("humans"), {3, 7}, p1);
        gs.town(cove)->buildings << B("village_hall");
        gs.town(cove)->lastBuildDay = -1;
        for (int y = 5; y < 8; ++y) gs.addArmy(PlayerId{}, {5, y});
        QVERIFY(!freeWaterNext(gs, reg, {3, 7}).has_value());
        QVERIFY(coastalAt(gs, reg, {3, 7}));
        QVERIFY(whyCannotBuild(gs, reg, cove, B("shipyard"), p1).isEmpty());
    }

    void buySailAndLand() {
        QVERIFY(!buyBoat(gs, reg, hero).ok);  // no shipyard yet
        gs.town(town)->buildings << B("village_hall") << B("shipyard");
        const int gold = gs.player(p1)->resources.value(R("gold"));
        const ActionResult r = buyBoat(gs, reg, hero);
        QVERIFY2(r.ok, qPrintable(r.error));
        QVERIFY(gs.hero(hero)->inBoat);
        QCOMPARE(gs.player(p1)->resources.value(R("gold")), gold - reg.balance().economy.boatCost.value(R("gold")));
        const Pos boat = gs.army(gs.hero(hero)->army)->pos;
        QVERIFY(reg.terrain(gs.map().terrainAt(boat)).sailable);

        // Sail along the water.
        MoveResult m = moveHero(gs, reg, hero, {10, 4});
        QVERIFY2(m.ok, qPrintable(m.error));
        QCOMPARE(gs.army(gs.hero(hero)->army)->pos.x > boat.x, true);
        // Land only next to the shore.
        QVERIFY(!moveHero(gs, reg, hero, {2, 2}).ok);
        gs.hero(hero)->movementPoints = 2000;
        m = moveHero(gs, reg, hero, {5, 2});  // sea next to the land
        QVERIFY2(m.ok, qPrintable(m.error));
        if (gs.army(gs.hero(hero)->army)->pos != Pos{5, 2}) {
            gs.hero(hero)->movementPoints = 2000;
            QVERIFY(moveHero(gs, reg, hero, {5, 2}).ok);
        }
        gs.hero(hero)->movementPoints = 2000;
        m = moveHero(gs, reg, hero, {4, 2});
        QVERIFY2(m.ok && m.landed, qPrintable(m.error));
        QVERIFY(!gs.hero(hero)->inBoat);
        QCOMPARE(gs.hero(hero)->movementPoints, 0);
        QCOMPARE(gs.army(gs.hero(hero)->army)->pos, (Pos{4, 2}));
    }

    void islandMapsSeparateThePlayers() {
        GameSetupParams params;
        params.seed = 5;
        params.islands = true;
        params.players = {{"A", false, {}, "humans", 1, "#ff0000"}, {"B", false, {}, "undead", 2, "#00ff00"}};
        GameState g;
        QString error;
        QVERIFY2(createGame(reg, params, &g, nullptr, &error), qPrintable(error));
        QList<Pos> capitals;
        for (const Town& t : g.towns())
            if (t.owner.isValid()) capitals << t.pos;
        QCOMPARE(capitals.size(), 2);
        const MoveCostGrid grid(g.map(), reg, MoveMode::Foot);
        QVERIFY(!findPath(grid, capitals[0], capitals[1]).found);  // only by sea
        for (const Pos c : capitals) QVERIFY(freeWaterNext(g, reg, c).has_value());  // a shipyard is possible
    }

    void boatIsSaved() {
        gs.town(town)->buildings << B("village_hall") << B("shipyard");
        QVERIFY(buyBoat(gs, reg, hero).ok);
        GameState loaded;
        QStringList errors;
        QVERIFY2(GameState::fromJson(gs.toJson(reg), reg, &loaded, &errors), qPrintable(errors.join("\n")));
        QVERIFY(loaded.hero(hero)->inBoat);
    }
};

QTEST_GUILESS_MAIN(TstBoats)
#include "tst_boats.moc"
