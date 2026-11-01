#include <QtTest>

#include "core/content/ContentRegistry.h"
#include "core/rules/Automation.h"
#include "core/rules/Dwellings.h"
#include "core/rules/TownRules.h"

using namespace kot::core;

class TstAutomation : public QObject {
    Q_OBJECT
    ContentRegistry reg;
    GameState gs;
    PlayerId p1;
    TownId t1, t2;

    Player& me() { return *gs.player(p1); }
    int garrisonUnits(TownId t, const QString& unit) {
        int n = 0;
        for (const UnitStack& s : gs.army(gs.town(t)->garrison)->stacks)
            if (s.unitTypeId() == unit) n += s.count();
        return n;
    }
    static bool builtIn(const AutoBuildReport& r, TownId t, const QString& b) {
        for (const auto& x : r.built) if (x.town == t && x.building == b) return true;
        return false;
    }

private slots:
    void initTestCase() {
        QStringList errors;
        QVERIFY2(reg.loadFromDirectory(KOT_DATA_DIR, &errors), qPrintable(errors.join("\n")));
    }

    void init() {
        gs = GameState(1);
        gs.setMap(GameMap(20, 20, "grass"));
        p1 = gs.addPlayer("A", gs.newTeam(), Player::Controller::Human, "humans", "#ff0000");
        me().resources = {{"gold", 20000}, {"wood", 100}, {"stone", 20}, {"gems", 10}};
        t1 = gs.addTown("One", "humans", {3, 3}, p1);
        t2 = gs.addTown("Two", "humans", {6, 6}, p1);
    }

    // ---- auto-build
    void buildsTheBestPlainBuildingInEveryTown() {
        const AutoBuildReport r = autoBuild(gs, reg, p1);
        QCOMPARE(r.built.size(), 2);
        QVERIFY(builtIn(r, t1, "barracks"));  // dwelling beats Village Hall
        QVERIFY(builtIn(r, t2, "barracks"));
        QVERIFY(r.needsChoice.isEmpty());
    }

    void oncePerTownPerDay() {
        autoBuild(gs, reg, p1);
        QVERIFY(autoBuild(gs, reg, p1).built.isEmpty());
    }

    void choicesAreFlaggedNotBuilt() {
        autoBuild(gs, reg, p1);        // day 1: barracks
        gs.setDay(2);
        AutoBuildReport r = autoBuild(gs, reg, p1);
        QVERIFY(builtIn(r, t1, "archery_range"));  // highest tier unit first
        QCOMPARE(r.needsChoice.size(), 2);          // snail_pen would shut out eagle_roost
        QCOMPARE(r.needsChoice[0].buildings, QStringList({"snail_pen"}));

        gs.setDay(3);
        r = autoBuild(gs, reg, p1);
        QVERIFY(builtIn(r, t1, "pike_barracks"));
        QCOMPARE(r.needsChoice[0].buildings, QStringList({"eagle_roost", "snail_pen"}));

        gs.setDay(4);
        QVERIFY(builtIn(autoBuild(gs, reg, p1), t1, "village_hall"));

        gs.setDay(5);  // only choices left
        r = autoBuild(gs, reg, p1);
        QVERIFY(r.built.isEmpty());
        QCOMPARE(r.needsChoice.size(), 2);
        QVERIFY(!gs.town(t1)->buildings.contains("snail_pen"));
        QVERIFY(!gs.town(t1)->buildings.contains("eagle_roost"));
    }

    void restrictedToOneTown() {
        const AutoBuildReport r = autoBuild(gs, reg, p1, t2);
        QCOMPARE(r.built.size(), 1);
        QCOMPARE(r.built[0].town, t2);
        QVERIFY(gs.town(t1)->buildings.isEmpty());
    }

    void reportsWhenItCannotAfford() {
        me().resources = {{"gold", 0}};
        const AutoBuildReport r = autoBuild(gs, reg, p1);
        QVERIFY(r.built.isEmpty());
        QCOMPARE(r.cannotAfford.size(), 2);
    }

    void fallsBackToAnAffordableBuilding() {
        me().resources = {{"gold", 5000}};  // no wood: barracks unaffordable, hall is fine
        const AutoBuildReport r = autoBuild(gs, reg, p1, t1);
        QVERIFY(builtIn(r, t1, "village_hall"));
    }

    void tradeLetsItBuyTheBetterBuilding() {
        me().resources = {{"gold", 5000}};
        const AutoBuildReport r = autoBuild(gs, reg, p1, t1, true);
        QVERIFY(builtIn(r, t1, "barracks"));
        QCOMPARE(me().resources.value("gold"), 5000 - 625 - 300);
    }

    void ignoresTownsOfOtherPlayers() {
        const PlayerId p2 = gs.addPlayer("B", gs.newTeam(), Player::Controller::Ai, "humans", "#00f");
        const TownId foreign = gs.addTown("Theirs", "humans", {9, 9}, p2);
        autoBuild(gs, reg, p1);
        QVERIFY(gs.town(foreign)->buildings.isEmpty());
    }

    // ---- buy all units
    void setUpDwellings(TownId t) {
        gs.town(t)->buildings = QStringList({"barracks", "archery_range"});
        gs.town(t)->recruitPool = {{"barracks", 12}, {"archery_range", 8}};
    }

    void bestFirstBuysTheStrongestUnitsFirst() {
        setUpDwellings(t1);
        me().resources = {{"gold", 1000}, {"wood", 10}};
        const BuyReport r = buyAllUnits(gs, reg, p1, t1, BuyStrategy::BestFirst);
        QCOMPARE(garrisonUnits(t1, "archer"), 8);
        QCOMPARE(garrisonUnits(t1, "militia"), 10);
        QCOMPARE(r.unitsBought, 18);
        QCOMPARE(r.townsServed, 1);
        QCOMPARE(r.spent.value("gold"), 1000);
        QCOMPARE(me().resources.value("gold"), 0);
    }

    void mostUnitsBuysTheCheapestFirst() {
        setUpDwellings(t1);
        me().resources = {{"gold", 1000}, {"wood", 10}};
        const BuyReport r = buyAllUnits(gs, reg, p1, t1, BuyStrategy::MostUnits);
        QCOMPARE(garrisonUnits(t1, "militia"), 12);
        QCOMPARE(garrisonUnits(t1, "archer"), 7);
        QCOMPARE(r.unitsBought, 19);
    }

    void buyAllInAllTownsInOrder() {
        setUpDwellings(t1);
        setUpDwellings(t2);
        me().resources = {{"gold", 1000}, {"wood", 10}};
        const BuyReport r = buyAllUnits(gs, reg, p1);
        QCOMPARE(r.townsServed, 1);  // the money was gone after the first town
        QCOMPARE(garrisonUnits(t1, "archer"), 8);
        QCOMPARE(garrisonUnits(t2, "archer"), 0);
    }

    void buysUnitsSentFromMapDwellings() {
        gs.addMapDwelling("pikeman", {15, 15}, 6, p1);
        sendDwellingUnitsToClosestTown(gs, p1);
        me().resources = {{"gold", 1000}};
        const BuyReport r = buyAllUnits(gs, reg, p1);
        QCOMPARE(r.unitsBought, 6);
    }

    void stopsCleanlyWhenTheGarrisonIsFull() {
        setUpDwellings(t1);
        Army& g = *gs.army(gs.town(t1)->garrison);
        for (int i = 0; i < kMaxArmyStacks; ++i) g.addUnits(QString("other%1").arg(i), 1, 5);
        const int gold = me().resources.value("gold");
        const BuyReport r = buyAllUnits(gs, reg, p1, t1);
        QCOMPARE(r.unitsBought, 0);
        QCOMPARE(me().resources.value("gold"), gold);
    }

    void nothingToBuyWithoutMoney() {
        setUpDwellings(t1);
        me().resources = {};
        QCOMPARE(buyAllUnits(gs, reg, p1).unitsBought, 0);
    }

    // ---- build, then buy
    void buildThenBuyUsesTheNewDwelling() {
        const BuildAndBuyReport r = buildThenBuyUnits(gs, reg, p1, t1);
        QVERIFY(builtIn(r.build, t1, "barracks"));
        QCOMPARE(r.buy.unitsBought, 12);  // the new barracks starts with a week of militia
        QCOMPARE(garrisonUnits(t1, "militia"), 12);
    }
};

QTEST_APPLESS_MAIN(TstAutomation)
#include "tst_automation.moc"
