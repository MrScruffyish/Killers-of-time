#include <QtTest>

#include "core/content/ContentRegistry.h"
#include "core/rules/Dwellings.h"
#include "core/rules/HeroRules.h"
#include "core/rules/Movement.h"
#include "core/rules/TownRules.h"

using namespace kot::core;

class TstDwellings : public QObject {
    Q_OBJECT
    ContentRegistry reg;
    GameState gs;
    PlayerId p1, p2;

    Player& me() { return *gs.player(p1); }

private slots:
    void initTestCase() {
        QStringList errors;
        QVERIFY2(reg.loadFromDirectory(KOT_DATA_DIR, &errors), qPrintable(errors.join("\n")));
    }

    void init() {
        gs = GameState(1);
        gs.setMap(GameMap(30, 30, "grass"));
        p1 = gs.addPlayer("A", gs.newTeam(), Player::Controller::Human, "humans", "#ff0000");
        p2 = gs.addPlayer("B", gs.newTeam(), Player::Controller::Human, "humans", "#0000ff");
        me().resources = {{"gold", 10000}, {"wood", 20}};
    }

    // ---- visiting and recruiting on the map
    void heroCapturesNeutralAndEnemyDwellings() {
        const HeroId h = createHero(gs, reg, p1, "H", "knight", {5, 5});
        const MapDwellingId neutral = gs.addMapDwelling("pikeman", {7, 5}, 10);
        const MapDwellingId enemy = gs.addMapDwelling("archer", {5, 7}, 6, p2);
        QCOMPARE(moveHero(gs, reg, h, {7, 5}).capturedDwelling, neutral);
        QCOMPARE(gs.mapDwelling(neutral)->owner, p1);
        QCOMPARE(moveHero(gs, reg, h, {5, 7}).capturedDwelling, enemy);
        QCOMPARE(gs.mapDwelling(enemy)->owner, p1);
        // Own dwelling: just a visit.
        QVERIFY(!moveHero(gs, reg, h, {7, 5}).capturedDwelling.isValid());
    }

    void recruitAtVisitedDwelling() {
        const HeroId h = createHero(gs, reg, p1, "H", "knight", {5, 5});
        const MapDwellingId d = gs.addMapDwelling("pikeman", {7, 5}, 10);
        moveHero(gs, reg, h, {7, 5});
        const ArmyId army = gs.hero(h)->army;
        const ActionResult r = recruitAtDwelling(gs, reg, d, p1, 3, army);
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(gs.army(army)->totalUnits(), 3);
        QCOMPARE(gs.mapDwelling(d)->available, 7);
        QCOMPARE(me().resources.value("gold"), 10000 - 3 * reg.unit("pikeman")->cost.value("gold"));
    }

    void recruitAtDwellingRejections() {
        const HeroId h = createHero(gs, reg, p1, "H", "knight", {5, 5});
        const MapDwellingId d = gs.addMapDwelling("pikeman", {7, 5}, 10, p1);
        const ArmyId army = gs.hero(h)->army;
        QVERIFY(recruitAtDwelling(gs, reg, d, p1, 1, army).error.contains("not at this dwelling"));
        gs.army(army)->pos = {7, 5};
        QVERIFY(recruitAtDwelling(gs, reg, d, p1, 11, army).error.contains("only 10"));
        QVERIFY(recruitAtDwelling(gs, reg, d, p1, 0, army).error.contains("invalid count"));
        QVERIFY(recruitAtDwelling(gs, reg, d, p2, 1, army).error.contains("not yours"));
        me().resources["gold"] = 10;
        QVERIFY(recruitAtDwelling(gs, reg, d, p1, 1, army).error.contains("not enough gold"));
        QCOMPARE(gs.mapDwelling(d)->available, 10);
    }

    void mapDwellingsGrowWeekly() {
        const MapDwellingId d = gs.addMapDwelling("pikeman", {7, 5}, 10);
        applyWeeklyGrowth(gs, reg);
        QCOMPARE(gs.mapDwelling(d)->available, 22);  // pikeman grows 12 a week
    }

    // ---- send all units to a town
    void sendAllToOneTown() {
        const TownId town = gs.addTown("Home", "humans", {1, 1}, p1);
        gs.addMapDwelling("militia", {10, 10}, 10, p1);
        gs.addMapDwelling("pikeman", {20, 20}, 5, p1);
        const MapDwellingId theirs = gs.addMapDwelling("archer", {3, 3}, 9, p2);
        const MapDwellingId free = gs.addMapDwelling("archer", {4, 4}, 9);
        const SendReport r = sendDwellingUnitsToTown(gs, p1, town);
        QCOMPARE(r.dwellingsEmptied, 2);
        QCOMPARE(r.unitsSent, 15);
        QCOMPARE(gs.town(town)->importedUnits.value("militia"), 10);
        QCOMPARE(gs.town(town)->importedUnits.value("pikeman"), 5);
        QCOMPARE(gs.mapDwelling(theirs)->available, 9);  // not ours: untouched
        QCOMPARE(gs.mapDwelling(free)->available, 9);
    }

    void cannotSendToSomeoneElsesTown() {
        const TownId foreign = gs.addTown("Theirs", "humans", {1, 1}, p2);
        const MapDwellingId d = gs.addMapDwelling("militia", {10, 10}, 10, p1);
        QCOMPARE(sendDwellingUnitsToTown(gs, p1, foreign).unitsSent, 0);
        QCOMPARE(gs.mapDwelling(d)->available, 10);
    }

    void sendToClosestTown() {
        const TownId west = gs.addTown("West", "humans", {2, 15}, p1);
        const TownId east = gs.addTown("East", "humans", {28, 15}, p1);
        gs.addTown("Enemy", "humans", {10, 10}, p2);  // closer but not ours
        const MapDwellingId nearWest = gs.addMapDwelling("militia", {8, 12}, 10, p1);
        const MapDwellingId nearEast = gs.addMapDwelling("pikeman", {22, 18}, 5, p1);
        const SendReport r = sendDwellingUnitsToClosestTown(gs, p1);
        QCOMPARE(r.dwellingsEmptied, 2);
        QCOMPARE(gs.town(west)->importedUnits.value("militia"), 10);
        QCOMPARE(gs.town(east)->importedUnits.value("pikeman"), 5);
        QCOMPARE(gs.mapDwelling(nearWest)->available, 0);
        QCOMPARE(gs.mapDwelling(nearEast)->available, 0);
    }

    void withoutATownNothingMoves() {
        const MapDwellingId d = gs.addMapDwelling("militia", {8, 12}, 10, p1);
        QCOMPARE(sendDwellingUnitsToClosestTown(gs, p1).unitsSent, 0);
        QCOMPARE(gs.mapDwelling(d)->available, 10);
    }

    // ---- recruiting sent units
    void sentUnitsCanBeRecruitedInTheTown() {
        const TownId town = gs.addTown("Home", "humans", {1, 1}, p1);
        gs.addMapDwelling("militia", {10, 10}, 10, p1);
        sendDwellingUnitsToTown(gs, p1, town);
        QVERIFY2(recruit(gs, reg, town, p1, "militia", 4).ok, "recruit sent units");
        QCOMPARE(gs.town(town)->importedUnits.value("militia"), 6);
        QCOMPARE(gs.army(gs.town(town)->garrison)->totalUnits(), 4);
        QCOMPARE(me().resources.value("gold"), 10000 - 80);
        QVERIFY(recruit(gs, reg, town, p1, "militia", 7).error.contains("only 6"));
        QVERIFY(recruit(gs, reg, town, p1, "archer", 1).error.contains("cannot be recruited"));
    }

    void sentUnitsAreUsedBeforeTheDwellingStock() {
        const TownId town = gs.addTown("Home", "humans", {1, 1}, p1);
        gs.town(town)->buildings << "barracks";
        gs.town(town)->recruitPool["barracks"] = 12;
        gs.town(town)->importedUnits["militia"] = 10;
        QCOMPARE(recruitOptions(*gs.town(town), reg).size(), 1);
        QCOMPARE(recruitOptions(*gs.town(town), reg)[0].available, 22);
        QVERIFY(recruit(gs, reg, town, p1, "militia", 15).ok);
        QVERIFY(!gs.town(town)->importedUnits.contains("militia"));
        QCOMPARE(gs.town(town)->recruitPool.value("barracks"), 7);
    }
};

QTEST_APPLESS_MAIN(TstDwellings)
#include "tst_dwellings.moc"
