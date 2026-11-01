#include <QtTest>

#include "core/content/ContentRegistry.h"
#include "core/rules/HeroRules.h"
#include "core/rules/TownRules.h"
#include "core/rules/TurnCycle.h"

using namespace kot::core;

class TstTowns : public QObject {
    Q_OBJECT
    ContentRegistry reg;
    GameState gs;
    PlayerId p1, p2;
    TownId town;

    Player& me() { return *gs.player(p1); }
    Town& t() { return *gs.town(town); }
    ActionResult build(const char* id) { return buildBuilding(gs, reg, town, id, p1); }
    // Builds on consecutive days (one building per town per day).
    void buildOnNewDay(const char* id) {
        gs.setDay(gs.day() + 1);
        const ActionResult r = build(id);
        QVERIFY2(r.ok, qPrintable(QString(id) + ": " + r.error));
    }

private slots:
    void initTestCase() {
        QStringList errors;
        QVERIFY2(reg.loadFromDirectory(KOT_DATA_DIR, &errors), qPrintable(errors.join("\n")));
    }

    void init() {
        gs = GameState(1);
        gs.setMap(GameMap(10, 10, "grass"));
        p1 = gs.addPlayer("A", gs.newTeam(), Player::Controller::Human, "humans", "#ff0000");
        p2 = gs.addPlayer("B", gs.newTeam(), Player::Controller::Human, "humans", "#0000ff");
        me().resources = {{"gold", 10000}, {"wood", 50}, {"gems", 10}, {"stone", 10}};
        town = gs.addTown("Home", "humans", {3, 3}, p1);
    }

    // ---- building
    void buildingPaysAndStocksTheDwelling() {
        QVERIFY2(build("barracks").ok, "barracks");
        QCOMPARE(me().resources.value("gold"), 9700);
        QCOMPARE(me().resources.value("wood"), 45);
        QVERIFY(t().buildings.contains("barracks"));
        QCOMPARE(t().recruitPool.value("barracks"), 12);  // militia weekly growth
        QCOMPARE(t().lastBuildDay, 1);
    }

    void oneBuildingPerDay() {
        QVERIFY(build("barracks").ok);
        const ActionResult r = build("village_hall");
        QVERIFY(!r.ok);
        QVERIFY(r.error.contains("already built here today"));
        gs.setDay(2);
        QVERIFY(build("village_hall").ok);
    }

    void cannotBuildTwice() {
        QVERIFY(build("barracks").ok);
        gs.setDay(2);
        QVERIFY(build("barracks").error.contains("already built"));
    }

    void prerequisitesAreEnforced() {
        const ActionResult r = build("archery_range");
        QVERIFY(!r.ok);
        QVERIFY(r.error.contains("requires Barracks"));
        QVERIFY(t().buildings.isEmpty());
        QCOMPARE(me().resources.value("gold"), 10000);
    }

    void notEnoughResources() {
        me().resources["gold"] = 100;
        const ActionResult r = build("barracks");
        QVERIFY(!r.ok);
        QVERIFY(r.error.contains("not enough gold"));
        QCOMPARE(me().resources.value("gold"), 100);
        QVERIFY(t().buildings.isEmpty());
    }

    void onlyTheOwnerBuilds() {
        QVERIFY(buildBuilding(gs, reg, town, "barracks", p2).error.contains("not your town"));
        const TownId neutral = gs.addTown("Free", "humans", {7, 7});
        QVERIFY(buildBuilding(gs, reg, neutral, "barracks", p1).error.contains("not your town"));
        QVERIFY(buildBuilding(gs, reg, TownId(999), "barracks", p1).error.contains("unknown town"));
    }

    void wrongRaceAndUnknownBuilding() {
        t().race = "undead";
        QVERIFY(build("barracks").error.contains("cannot be built in"));
        t().race = "humans";
        QVERIFY(build("castle").error.contains("unknown building"));
    }

    void whyCannotBuildExplains() {
        QVERIFY(whyCannotBuild(gs, reg, town, "barracks", p1).isEmpty());
        QVERIFY(whyCannotBuild(gs, reg, town, "archery_range", p1).contains("Barracks"));
    }

    // ---- exclusive choices
    void exclusiveChoiceBlocksTheOtherOne() {
        QVERIFY(build("barracks").ok);
        buildOnNewDay("archery_range");
        buildOnNewDay("eagle_roost");
        gs.setDay(gs.day() + 1);
        const ActionResult r = build("snail_pen");
        QVERIFY(!r.ok);
        QVERIFY(r.error.contains("excluded by Eagle Roost"));
    }

    void exclusiveChoiceWorksBothWays() {
        QVERIFY(build("barracks").ok);
        buildOnNewDay("snail_pen");
        buildOnNewDay("archery_range");
        gs.setDay(gs.day() + 1);
        QVERIFY(build("eagle_roost").error.contains("excluded by Snail Pen"));
    }

    // ---- upgrades
    void upgradeNeedsTheBaseBuilding() {
        QVERIFY(build("pike_barracks").error.contains("requires Barracks"));
    }

    void upgradeReplacesTheDwellingAndKeepsTheStock() {
        QVERIFY(build("barracks").ok);
        QCOMPARE(dwellings(t(), reg).size(), 1);
        QCOMPARE(dwellings(t(), reg)[0].unitId, QString("militia"));
        buildOnNewDay("pike_barracks");
        const QList<Dwelling> d = dwellings(t(), reg);
        QCOMPARE(d.size(), 1);
        QCOMPARE(d[0].unitId, QString("pikeman"));
        QCOMPARE(d[0].poolKey, QString("barracks"));
        QCOMPARE(t().recruitPool.value("barracks"), 12);  // stock kept, not reset
    }

    // ---- recruiting
    void recruitIntoGarrison() {
        QVERIFY(build("barracks").ok);
        const ActionResult r = recruit(gs, reg, town, p1, "militia", 5);
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(gs.army(t().garrison)->totalUnits(), 5);
        QCOMPARE(gs.army(t().garrison)->stacks[0].maxHp(), 6);
        QCOMPARE(me().resources.value("gold"), 9700 - 100);
        QCOMPARE(t().recruitPool.value("barracks"), 7);
    }

    void recruitLimits() {
        QVERIFY(build("barracks").ok);
        QVERIFY(recruit(gs, reg, town, p1, "militia", 13).error.contains("only 12 Militia available"));
        QVERIFY(recruit(gs, reg, town, p1, "militia", 0).error.contains("invalid count"));
        QVERIFY(recruit(gs, reg, town, p1, "archer", 1).error.contains("cannot be recruited"));
        QVERIFY(recruit(gs, reg, town, p1, "nonsense", 1).error.contains("unknown unit"));
        me().resources["gold"] = 30;
        QVERIFY(recruit(gs, reg, town, p1, "militia", 2).error.contains("not enough gold"));
        QCOMPARE(t().recruitPool.value("barracks"), 12);
        QVERIFY(recruit(gs, reg, town, p2, "militia", 1).error.contains("not your town"));
    }

    void recruitIntoVisitingHeroArmy() {
        QVERIFY(build("barracks").ok);
        const HeroId h = createHero(gs, reg, p1, "Hero", "knight", {3, 3});
        const ArmyId heroArmy = gs.hero(h)->army;
        QVERIFY(recruit(gs, reg, town, p1, "militia", 4, heroArmy).ok);
        QCOMPARE(gs.army(heroArmy)->totalUnits(), 4);
        QCOMPARE(gs.army(t().garrison)->totalUnits(), 0);

        const HeroId away = createHero(gs, reg, p1, "Far", "knight", {8, 8});
        const ActionResult r = recruit(gs, reg, town, p1, "militia", 1, gs.hero(away)->army);
        QVERIFY(!r.ok);
        QVERIFY(r.error.contains("not in this town"));
    }

    void fullArmyBlocksRecruitingWithoutCharging() {
        QVERIFY(build("barracks").ok);
        Army& garrison = *gs.army(t().garrison);
        for (int i = 0; i < kMaxArmyStacks; ++i) garrison.addUnits(QString("other%1").arg(i), 1, 5);
        const int gold = me().resources.value("gold");
        const ActionResult r = recruit(gs, reg, town, p1, "militia", 1);
        QVERIFY(!r.ok);
        QVERIFY(r.error.contains("no free slot"));
        QCOMPARE(me().resources.value("gold"), gold);
        QCOMPARE(t().recruitPool.value("barracks"), 12);
    }

    void afterUpgradeOnlyTheUpgradedUnitIsRecruitable() {
        QVERIFY(build("barracks").ok);
        buildOnNewDay("pike_barracks");
        QVERIFY(recruit(gs, reg, town, p1, "militia", 1).error.contains("cannot be recruited"));
        QVERIFY(recruit(gs, reg, town, p1, "pikeman", 3).ok);
        QCOMPARE(gs.army(t().garrison)->stacks[0].unitTypeId(), QString("pikeman"));
        QCOMPARE(t().recruitPool.value("barracks"), 9);
    }

    // ---- weekly growth and income
    void weekDetection() {
        QVERIFY(!isNewWeek(1));
        QVERIFY(!isNewWeek(7));
        QVERIFY(isNewWeek(8));
        QVERIFY(isNewWeek(15));
        QVERIFY(!isNewWeek(9));
    }

    void dwellingsGrowEveryWeek() {
        QVERIFY(build("barracks").ok);
        beginGame(gs, reg);
        for (int i = 0; i < 6; ++i) endTurn(gs, reg);  // two players: three days later
        QCOMPARE(gs.day(), 4);
        QCOMPARE(t().recruitPool.value("barracks"), 12);
        for (int i = 0; i < 8; ++i) endTurn(gs, reg);  // day 8
        QCOMPARE(gs.day(), 8);
        QCOMPARE(t().recruitPool.value("barracks"), 24);
    }

    void upgradedDwellingGrowsWithTheNewUnit() {
        QVERIFY(build("barracks").ok);
        buildOnNewDay("pike_barracks");
        gs.setDay(7);
        beginGame(gs, reg);
        endTurn(gs, reg);
        endTurn(gs, reg);  // day 8
        QCOMPARE(t().recruitPool.value("barracks"), 12 + 12);  // pikeman growth is 12
    }

    void buildingsAddIncome() {
        QVERIFY(build("village_hall").ok);
        const ResourceAmounts income = dailyIncome(gs, reg, p1);
        QCOMPARE(income.value("gold"), 500 + 250);
    }
};

QTEST_APPLESS_MAIN(TstTowns)
#include "tst_towns.moc"
