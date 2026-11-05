#include <QtTest>

#include "TestContent.h"

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
    ActionResult build(const char* id) { return buildBuilding(gs, reg, town, B(id), p1); }
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
        useContent(reg);
    }

    void init() {
        gs = GameState(1);
        gs.setMap(GameMap(10, 10, T("grass")));
        p1 = gs.addPlayer("A", gs.newTeam(), Player::Controller::Human, RC("humans"), "#ff0000");
        p2 = gs.addPlayer("B", gs.newTeam(), Player::Controller::Human, RC("humans"), "#0000ff");
        me().resources = amounts({{"gold", 10000}, {"wood", 50}, {"gems", 10}, {"stone", 10}});
        town = gs.addTown("Home", RC("humans"), {3, 3}, p1);
    }

    // ---- building
    void buildingPaysAndStocksTheDwelling() {
        QVERIFY2(build("barracks").ok, "barracks");
        QCOMPARE(me().resources.value(R("gold")), 9700);
        QCOMPARE(me().resources.value(R("wood")), 45);
        QVERIFY(t().buildings.contains(B("barracks")));
        QCOMPARE(t().recruitPool.value(B("barracks")), 12);  // militia weekly growth
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
        QCOMPARE(me().resources.value(R("gold")), 10000);
    }

    void notEnoughResources() {
        me().resources[R("gold")] = 100;
        const ActionResult r = build("barracks");
        QVERIFY(!r.ok);
        QVERIFY(r.error.contains("not enough gold"));
        QCOMPARE(me().resources.value(R("gold")), 100);
        QVERIFY(t().buildings.isEmpty());
    }

    void onlyTheOwnerBuilds() {
        QVERIFY(buildBuilding(gs, reg, town, B("barracks"), p2).error.contains("not your town"));
        const TownId neutral = gs.addTown("Free", RC("humans"), {7, 7});
        QVERIFY(buildBuilding(gs, reg, neutral, B("barracks"), p1).error.contains("not your town"));
        QVERIFY(buildBuilding(gs, reg, TownId(999), B("barracks"), p1).error.contains("unknown town"));
    }

    void wrongRaceAndUnknownBuilding() {
        t().race = RC("undead");
        QVERIFY(build("barracks").error.contains("cannot be built in"));
        t().race = RC("humans");
        QVERIFY(build("moon_palace").error.contains("unknown building"));
    }

    void whyCannotBuildExplains() {
        QVERIFY(whyCannotBuild(gs, reg, town, B("barracks"), p1).isEmpty());
        QVERIFY(whyCannotBuild(gs, reg, town, B("archery_range"), p1).contains("Barracks"));
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
        QCOMPARE(dwellings(t(), reg)[0].unitId, U("militia"));
        buildOnNewDay("pike_barracks");
        const QList<Dwelling> d = dwellings(t(), reg);
        QCOMPARE(d.size(), 1);
        QCOMPARE(d[0].unitId, U("pikeman"));
        QCOMPARE(d[0].poolKey, B("barracks"));
        QCOMPARE(t().recruitPool.value(B("barracks")), 12);  // stock kept, not reset
    }

    // ---- recruiting
    void recruitIntoGarrison() {
        QVERIFY(build("barracks").ok);
        const ActionResult r = recruit(gs, reg, town, p1, U("militia"), 5);
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(gs.army(t().garrison)->totalUnits(), 5);
        QCOMPARE(gs.army(t().garrison)->stacks[0].maxHp(), 6);
        QCOMPARE(me().resources.value(R("gold")), 9700 - 100);
        QCOMPARE(t().recruitPool.value(B("barracks")), 7);
    }

    void recruitLimits() {
        QVERIFY(build("barracks").ok);
        QVERIFY(recruit(gs, reg, town, p1, U("militia"), 13).error.contains("only 12 Militia available"));
        QVERIFY(recruit(gs, reg, town, p1, U("militia"), 0).error.contains("invalid count"));
        QVERIFY(recruit(gs, reg, town, p1, U("archer"), 1).error.contains("cannot be recruited"));
        QVERIFY(recruit(gs, reg, town, p1, U("nonsense"), 1).error.contains("unknown unit"));
        me().resources[R("gold")] = 30;
        QVERIFY(recruit(gs, reg, town, p1, U("militia"), 2).error.contains("not enough gold"));
        QCOMPARE(t().recruitPool.value(B("barracks")), 12);
        QVERIFY(recruit(gs, reg, town, p2, U("militia"), 1).error.contains("not your town"));
    }

    void recruitIntoVisitingHeroArmy() {
        QVERIFY(build("barracks").ok);
        const HeroId h = createHero(gs, reg, p1, "Hero", C("knight"), {3, 3});
        const ArmyId heroArmy = gs.hero(h)->army;
        QVERIFY(recruit(gs, reg, town, p1, U("militia"), 4, heroArmy).ok);
        QCOMPARE(gs.army(heroArmy)->totalUnits(), 4);
        QCOMPARE(gs.army(t().garrison)->totalUnits(), 0);

        const HeroId away = createHero(gs, reg, p1, "Far", C("knight"), {8, 8});
        const ActionResult r = recruit(gs, reg, town, p1, U("militia"), 1, gs.hero(away)->army);
        QVERIFY(!r.ok);
        QVERIFY(r.error.contains("not in this town"));
    }

    void fullArmyBlocksRecruitingWithoutCharging() {
        QVERIFY(build("barracks").ok);
        Army& garrison = *gs.army(t().garrison);
        for (int i = 0; i < kMaxArmyStacks; ++i) garrison.addUnits(UnitTypeId(1000 + i), 1, 5);
        const int gold = me().resources.value(R("gold"));
        const ActionResult r = recruit(gs, reg, town, p1, U("militia"), 1);
        QVERIFY(!r.ok);
        QVERIFY(r.error.contains("no free slot"));
        QCOMPARE(me().resources.value(R("gold")), gold);
        QCOMPARE(t().recruitPool.value(B("barracks")), 12);
    }

    void afterUpgradeOnlyTheUpgradedUnitIsRecruitable() {
        QVERIFY(build("barracks").ok);
        buildOnNewDay("pike_barracks");
        QVERIFY(recruit(gs, reg, town, p1, U("militia"), 1).error.contains("cannot be recruited"));
        QVERIFY(recruit(gs, reg, town, p1, U("pikeman"), 3).ok);
        QCOMPARE(gs.army(t().garrison)->stacks[0].unitTypeId(), U("pikeman"));
        QCOMPARE(t().recruitPool.value(B("barracks")), 9);
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
        // Players without towns and heroes are out of the game and skipped; keep B in.
        createHero(gs, reg, p2, "H2", C("knight"), {9, 9});
        beginGame(gs, reg);
        for (int i = 0; i < 6; ++i) endTurn(gs, reg);  // two players: three days later
        QCOMPARE(gs.day(), 4);
        QCOMPARE(t().recruitPool.value(B("barracks")), 12);
        for (int i = 0; i < 8; ++i) endTurn(gs, reg);  // day 8
        QCOMPARE(gs.day(), 8);
        QCOMPARE(t().recruitPool.value(B("barracks")), 24);
    }

    void neutralTownsGrowTheirGarrisonSlowly() {
        const TownId free = gs.addTown("Free", RC("humans"), {7, 7}, PlayerId());
        gs.town(free)->buildings << B("barracks");  // militia, 12 a week
        const int week = townGrowth(gs, *gs.town(free), *reg.unit(U("militia")), reg);
        const auto garrison = [&]() { return gs.army(gs.town(free)->garrison)->totalUnits(); };
        QCOMPARE(garrison(), 0);
        applyWeeklyGrowth(gs, reg);
        QCOMPARE(garrison(), std::max(1, week * reg.balance().neutralTowns.growthPercent / 100));
        QCOMPARE(gs.town(free)->recruitPool.value(B("barracks")), 0);  // nothing piles up for the next owner
        for (int i = 0; i < 40; ++i) applyWeeklyGrowth(gs, reg);
        QCOMPARE(garrison(), reg.balance().neutralTowns.capWeeks * week);  // up to the cap
    }

    void upgradedDwellingGrowsWithTheNewUnit() {
        QVERIFY(build("barracks").ok);
        buildOnNewDay("pike_barracks");
        gs.setDay(7);
        beginGame(gs, reg);
        endTurn(gs, reg);
        endTurn(gs, reg);  // day 8
        QCOMPARE(t().recruitPool.value(B("barracks")), 12 + 12);  // pikeman growth is 12
    }

    void granaryMakesDwellingsGrowMore() {
        const UnitDef& militia = *reg.unit(U("militia"));
        QCOMPARE(townGrowth(gs, t(), militia, reg), 12);
        me().resources[R("grain")] = 10;
        buildOnNewDay("village_hall");
        buildOnNewDay("granary");
        QCOMPARE(townGrowth(gs, t(), militia, reg), 14);  // +20%, rounded
    }

    void buildingsAddIncome() {
        QVERIFY(build("village_hall").ok);
        const ResourceAmounts income = dailyIncome(gs, reg, p1);
        QCOMPARE(income.value(R("gold")), 500 + 250);
    }
};

QTEST_APPLESS_MAIN(TstTowns)
#include "tst_towns.moc"
