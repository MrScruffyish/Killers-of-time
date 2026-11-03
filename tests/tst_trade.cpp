#include <QtTest>

#include "core/content/ContentRegistry.h"
#include "core/rules/TownRules.h"
#include "core/rules/Trade.h"

using namespace kot::core;

class TstTrade : public QObject {
    Q_OBJECT
    ContentRegistry reg;
    GameState gs;
    PlayerId p1;
    TownId town;

    Player& me() { return *gs.player(p1); }

private slots:
    void initTestCase() {
        QStringList errors;
        QVERIFY2(reg.loadFromDirectory(KOT_DATA_DIR, &errors), qPrintable(errors.join("\n")));
    }

    void init() {
        gs = GameState(1);
        p1 = gs.addPlayer("A", gs.newTeam(), Player::Controller::Human, "humans", "#ff0000");
        gs.player(p1)->race = "orcs";  // a race without a trade bonus (humans get +10%): plain trade numbers
        town = gs.addTown("Home", "humans", {3, 3}, p1);
    }

    void marketsImproveTheRate() {
        QCOMPARE(tradeEfficiency(gs, reg, p1), 80);
        gs.town(town)->buildings << "village_hall" << "marketplace";
        QCOMPARE(tradeEfficiency(gs, reg, p1), 85);
        const TownId second = gs.addTown("Second", "humans", {9, 9}, p1);
        gs.town(second)->buildings << "marketplace";
        QCOMPARE(tradeEfficiency(gs, reg, p1), 90);
        QCOMPARE(tradeYield(reg, "wood", 10, "gold", tradeEfficiency(gs, reg, p1)), 900);
        gs.player(p1)->resources = {{"wood", 10}};
        QVERIFY(trade(gs, reg, p1, "wood", 10, "gold").ok);
        QCOMPARE(gs.player(p1)->resources.value("gold"), 900);
    }

    // values: gold 1, wood 100, gems 300, efficiency 80%
    void ratesFollowValuesAndFee() {
        QCOMPARE(tradeYield(reg, "wood", 10, "gold"), 800);
        QCOMPARE(tradeYield(reg, "gold", 1000, "wood"), 8);
        QCOMPARE(tradeYield(reg, "gold", 100, "wood"), 0);
        QCOMPARE(tradeCostToGet(reg, "gold", "wood", 8), 1000);
        QCOMPARE(tradeCostToGet(reg, "gold", "wood", 1), 125);
        QCOMPARE(tradeCostToGet(reg, "gems", "wood", 5), 3);
        QCOMPARE(tradeYield(reg, "nonsense", 10, "gold"), 0);
    }

    void plainTrade() {
        me().resources = {{"gold", 1500}};
        QVERIFY(trade(gs, reg, p1, "gold", 1000, "wood").ok);
        QCOMPARE(me().resources.value("gold"), 500);
        QCOMPARE(me().resources.value("wood"), 8);
    }

    void plainTradeRejections() {
        me().resources = {{"gold", 1500}};
        QVERIFY(trade(gs, reg, p1, "gold", 10, "gold").error.contains("itself"));
        QVERIFY(trade(gs, reg, p1, "gold", 5000, "wood").error.contains("not enough gold"));
        QVERIFY(trade(gs, reg, p1, "gold", 10, "wood").error.contains("too little"));
        QVERIFY(trade(gs, reg, p1, "gold", 10, "nonsense").error.contains("unknown resource"));
        QVERIFY(trade(gs, reg, PlayerId(99), "gold", 10, "wood").error.contains("unknown player"));
        QCOMPARE(me().resources.value("gold"), 1500);
    }

    void nothingToDoWhenAlreadyAffordable() {
        const auto plan = planTradeFor(reg, {{"gold", 500}, {"wood", 5}}, {{"gold", 300}, {"wood", 5}});
        QVERIFY(plan.has_value());
        QVERIFY(plan->isEmpty());
    }

    void goldBuysMissingWood() {
        const auto plan = planTradeFor(reg, {{"gold", 1000}}, {{"wood", 5}});
        QVERIFY(plan.has_value());
        QCOMPARE(plan->size(), 1);
        QCOMPARE((*plan)[0].give, QString("gold"));
        QCOMPARE((*plan)[0].giveAmount, 625);
        QCOMPARE((*plan)[0].get, QString("wood"));
        QCOMPARE((*plan)[0].getAmount, 5);
    }

    void otherResourcesAreSoldBeforeGold() {
        const auto plan = planTradeFor(reg, {{"gold", 1000}, {"gems", 4}}, {{"wood", 5}});
        QVERIFY(plan.has_value());
        QCOMPARE(plan->size(), 1);
        QCOMPARE((*plan)[0].give, QString("gems"));
        QCOMPARE((*plan)[0].giveAmount, 3);
    }

    void neverSellsWhatTheCostNeeds() {
        // Gold 1000 is needed for the cost (900); wood is spare and is sold instead.
        ResourceAmounts have = {{"gold", 1000}, {"wood", 5}};
        const auto plan = planTradeFor(reg, have, {{"gold", 900}, {"stone", 1}});
        QVERIFY(plan.has_value());
        QCOMPARE(plan->size(), 1);
        QCOMPARE((*plan)[0].give, QString("wood"));
        QCOMPARE((*plan)[0].giveAmount, 2);
        applyTradeSteps(have, *plan);
        QCOMPARE(have.value("gold"), 1000);
        QCOMPARE(have.value("wood"), 3);
        QCOMPARE(have.value("stone"), 1);
    }

    void severalMissingResources() {
        ResourceAmounts have = {{"gold", 1000}};
        const auto plan = planTradeFor(reg, have, {{"wood", 2}, {"stone", 2}});
        QVERIFY(plan.has_value());
        QCOMPARE(plan->size(), 2);
        applyTradeSteps(have, *plan);
        QCOMPARE(have.value("wood"), 2);
        QCOMPARE(have.value("stone"), 2);
        QCOMPARE(have.value("gold"), 500);
    }

    void impossibleWhenTooPoor() {
        QVERIFY(!planTradeFor(reg, {{"gold", 10}}, {{"gems", 5}}).has_value());
        QVERIFY(!planTradeFor(reg, {}, {{"gems", 1}}).has_value());
    }

    void buildWithTradeBuysTheMissingWood() {
        me().resources = {{"gold", 5000}};
        QVERIFY(whyCannotBuild(gs, reg, town, "barracks", p1).contains("not enough wood"));
        const BuildResult r = buildBuildingWithTrade(gs, reg, town, "barracks", p1);
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(r.trades.size(), 1);
        QCOMPARE(me().resources.value("wood"), 0);
        QCOMPARE(me().resources.value("gold"), 5000 - 625 - 300);
        QVERIFY(gs.town(town)->buildings.contains("barracks"));
    }

    void buildWithTradeChangesNothingWhenItCannotWork() {
        me().resources = {{"gold", 100}};
        const BuildResult r = buildBuildingWithTrade(gs, reg, town, "barracks", p1);
        QVERIFY(!r.ok);
        QVERIFY(r.error.contains("even by trading"));
        QCOMPARE(me().resources.value("gold"), 100);
        QVERIFY(gs.town(town)->buildings.isEmpty());
    }

    void buildWithTradeStillObeysOtherRules() {
        me().resources = {{"gold", 50000}};
        QVERIFY(buildBuildingWithTrade(gs, reg, town, "archery_range", p1).error.contains("requires Barracks"));
        QCOMPARE(me().resources.value("gold"), 50000);  // no trade without a valid build
    }

    void buildWithoutNeedingTradeDoesNotTrade() {
        me().resources = {{"gold", 5000}, {"wood", 10}};
        const BuildResult r = buildBuildingWithTrade(gs, reg, town, "barracks", p1);
        QVERIFY(r.ok);
        QVERIFY(r.trades.isEmpty());
        QCOMPARE(me().resources.value("gold"), 4700);
    }
};

QTEST_APPLESS_MAIN(TstTrade)
#include "tst_trade.moc"
