#include <QtTest>

#include "TestContent.h"

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
        useContent(reg);
    }

    void init() {
        gs = GameState(1);
        p1 = gs.addPlayer("A", gs.newTeam(), Player::Controller::Human, RC("humans"), "#ff0000");
        gs.player(p1)->race = RC("orcs");  // a race without a trade bonus (humans get +10%): plain trade numbers
        town = gs.addTown("Home", RC("humans"), {3, 3}, p1);
    }

    void marketsImproveTheRate() {
        QCOMPARE(tradeEfficiency(gs, reg, p1), 80);
        gs.town(town)->buildings << B("village_hall") << B("marketplace");
        QCOMPARE(tradeEfficiency(gs, reg, p1), 85);
        const TownId second = gs.addTown("Second", RC("humans"), {9, 9}, p1);
        gs.town(second)->buildings << B("marketplace");
        QCOMPARE(tradeEfficiency(gs, reg, p1), 90);
        QCOMPARE(tradeYield(reg, R("wood"), 10, R("gold"), tradeEfficiency(gs, reg, p1)), 900);
        gs.player(p1)->resources = amounts({{"wood", 10}});
        QVERIFY(trade(gs, reg, p1, R("wood"), 10, R("gold")).ok);
        QCOMPARE(gs.player(p1)->resources.value(R("gold")), 900);
    }

    // values: gold 1, wood 100, gems 300, efficiency 80%
    void ratesFollowValuesAndFee() {
        QCOMPARE(tradeYield(reg, R("wood"), 10, R("gold")), 800);
        QCOMPARE(tradeYield(reg, R("gold"), 1000, R("wood")), 8);
        QCOMPARE(tradeYield(reg, R("gold"), 100, R("wood")), 0);
        QCOMPARE(tradeCostToGet(reg, R("gold"), R("wood"), 8), 1000);
        QCOMPARE(tradeCostToGet(reg, R("gold"), R("wood"), 1), 125);
        QCOMPARE(tradeCostToGet(reg, R("gems"), R("wood"), 5), 3);
        QCOMPARE(tradeYield(reg, R("nonsense"), 10, R("gold")), 0);
    }

    void plainTrade() {
        me().resources = amounts({{"gold", 1500}});
        QVERIFY(trade(gs, reg, p1, R("gold"), 1000, R("wood")).ok);
        QCOMPARE(me().resources.value(R("gold")), 500);
        QCOMPARE(me().resources.value(R("wood")), 8);
    }

    void plainTradeRejections() {
        me().resources = amounts({{"gold", 1500}});
        QVERIFY(trade(gs, reg, p1, R("gold"), 10, R("gold")).error.contains("itself"));
        QVERIFY(trade(gs, reg, p1, R("gold"), 5000, R("wood")).error.contains("not enough gold"));
        QVERIFY(trade(gs, reg, p1, R("gold"), 10, R("wood")).error.contains("too little"));
        QVERIFY(trade(gs, reg, p1, R("gold"), 10, R("nonsense")).error.contains("unknown resource"));
        QVERIFY(trade(gs, reg, PlayerId(99), R("gold"), 10, R("wood")).error.contains("unknown player"));
        QCOMPARE(me().resources.value(R("gold")), 1500);
    }

    void nothingToDoWhenAlreadyAffordable() {
        const auto plan = planTradeFor(reg, amounts({{"gold", 500}, {"wood", 5}}), amounts({{"gold", 300}, {"wood", 5}}));
        QVERIFY(plan.has_value());
        QVERIFY(plan->isEmpty());
    }

    void goldBuysMissingWood() {
        const auto plan = planTradeFor(reg, amounts({{"gold", 1000}}), amounts({{"wood", 5}}));
        QVERIFY(plan.has_value());
        QCOMPARE(plan->size(), 1);
        QCOMPARE((*plan)[0].give, R("gold"));
        QCOMPARE((*plan)[0].giveAmount, 625);
        QCOMPARE((*plan)[0].get, R("wood"));
        QCOMPARE((*plan)[0].getAmount, 5);
    }

    void otherResourcesAreSoldBeforeGold() {
        const auto plan = planTradeFor(reg, amounts({{"gold", 1000}, {"gems", 4}}), amounts({{"wood", 5}}));
        QVERIFY(plan.has_value());
        QCOMPARE(plan->size(), 1);
        QCOMPARE((*plan)[0].give, R("gems"));
        QCOMPARE((*plan)[0].giveAmount, 3);
    }

    void neverSellsWhatTheCostNeeds() {
        // Gold 1000 is needed for the cost (900); wood is spare and is sold instead.
        ResourceAmounts have = amounts({{"gold", 1000}, {"wood", 5}});
        const auto plan = planTradeFor(reg, have, amounts({{"gold", 900}, {"stone", 1}}));
        QVERIFY(plan.has_value());
        QCOMPARE(plan->size(), 1);
        QCOMPARE((*plan)[0].give, R("wood"));
        QCOMPARE((*plan)[0].giveAmount, 2);
        applyTradeSteps(have, *plan);
        QCOMPARE(have.value(R("gold")), 1000);
        QCOMPARE(have.value(R("wood")), 3);
        QCOMPARE(have.value(R("stone")), 1);
    }

    void severalMissingResources() {
        ResourceAmounts have = amounts({{"gold", 1000}});
        const auto plan = planTradeFor(reg, have, amounts({{"wood", 2}, {"stone", 2}}));
        QVERIFY(plan.has_value());
        QCOMPARE(plan->size(), 2);
        applyTradeSteps(have, *plan);
        QCOMPARE(have.value(R("wood")), 2);
        QCOMPARE(have.value(R("stone")), 2);
        QCOMPARE(have.value(R("gold")), 500);
    }

    void impossibleWhenTooPoor() {
        QVERIFY(!planTradeFor(reg, amounts({{"gold", 10}}), amounts({{"gems", 5}})).has_value());
        QVERIFY(!planTradeFor(reg, ResourceAmounts(), amounts({{"gems", 1}})).has_value());
    }

    void buildWithTradeBuysTheMissingWood() {
        me().resources = amounts({{"gold", 5000}});
        QVERIFY(whyCannotBuild(gs, reg, town, B("barracks"), p1).contains("not enough wood"));
        const BuildResult r = buildBuildingWithTrade(gs, reg, town, B("barracks"), p1);
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(r.trades.size(), 1);
        QCOMPARE(me().resources.value(R("wood")), 0);
        QCOMPARE(me().resources.value(R("gold")), 5000 - 625 - 300);
        QVERIFY(gs.town(town)->buildings.contains(B("barracks")));
    }

    void buildWithTradeChangesNothingWhenItCannotWork() {
        me().resources = amounts({{"gold", 100}});
        const BuildResult r = buildBuildingWithTrade(gs, reg, town, B("barracks"), p1);
        QVERIFY(!r.ok);
        QVERIFY(r.error.contains("even by trading"));
        QCOMPARE(me().resources.value(R("gold")), 100);
        QVERIFY(gs.town(town)->buildings.isEmpty());
    }

    void buildWithTradeStillObeysOtherRules() {
        me().resources = amounts({{"gold", 50000}});
        QVERIFY(buildBuildingWithTrade(gs, reg, town, B("archery_range"), p1).error.contains("requires Barracks"));
        QCOMPARE(me().resources.value(R("gold")), 50000);  // no trade without a valid build
    }

    void buildWithoutNeedingTradeDoesNotTrade() {
        me().resources = amounts({{"gold", 5000}, {"wood", 10}});
        const BuildResult r = buildBuildingWithTrade(gs, reg, town, B("barracks"), p1);
        QVERIFY(r.ok);
        QVERIFY(r.trades.isEmpty());
        QCOMPARE(me().resources.value(R("gold")), 4700);
    }
};

QTEST_APPLESS_MAIN(TstTrade)
#include "tst_trade.moc"
