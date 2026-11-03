#include <QtTest>

#include "core/battle/AutoBattle.h"
#include "core/battle/Battle.h"
#include "core/battle/BattleAi.h"
#include "core/content/ContentRegistry.h"
#include "core/rules/HeroRules.h"

using namespace kot::core;

// Sieges: walls, gate, towers and the attacker's catapult.
class TstSiege : public QObject {
    Q_OBJECT
    ContentRegistry reg;
    GameState gs;
    PlayerId p1, p2;
    HeroId attackerHero;
    TownId town;

    ArmyId attacker() { return gs.hero(attackerHero)->army; }
    ArmyId garrison() { return gs.town(town)->garrison; }
    void walls(int level) {
        QStringList& b = gs.town(town)->buildings;
        b = {"village_hall"};
        if (level >= 1) b << "fort";
        if (level >= 2) b << "citadel";
    }

private slots:
    void initTestCase() {
        QStringList errors;
        QVERIFY2(reg.loadFromDirectory(KOT_DATA_DIR, &errors), qPrintable(errors.join("\n")));
    }
    void init() {
        gs = GameState(5);
        gs.setMap(GameMap(10, 10, "grass"));
        p1 = gs.addPlayer("A", gs.newTeam(), Player::Controller::Human, "humans", "#ff0000");
        p2 = gs.addPlayer("B", gs.newTeam(), Player::Controller::Human, "humans", "#0000ff");
        town = gs.addTown("Keep", "humans", {5, 5}, p2);
        attackerHero = createHero(gs, reg, p1, "H", "knight", {4, 5});
        gs.army(attacker())->addUnits("pikeman", 30, reg.unit("pikeman")->maxHp);
        gs.army(garrison())->addUnits("archer", 10, reg.unit("archer")->maxHp);
    }

    void noWallsNoSiege() {
        walls(0);
        Battle b(gs, reg, attacker(), garrison(), 1);
        QCOMPARE(b.wallLevel(), 0);
        QVERIFY(b.walls().isEmpty());
        QVERIFY(!b.whyNoSiegeEngine().isEmpty());
    }

    void fortAndCitadelBuildWallsAndTowers() {
        walls(1);
        {
            Battle b(gs, reg, attacker(), garrison(), 1);
            QCOMPARE(b.wallLevel(), 1);
            QCOMPARE(b.walls().size(), hex::kHeight);  // a whole column
            QCOMPARE(b.towers().size(), 2);
            QCOMPARE(b.walls().value(b.gate()), reg.balance().siege.wallHpPerLevel);
            for (const Pos p : b.obstacles()) QVERIFY(p.x < kWallColumn);  // no rocks in the wall
        }
        walls(2);
        Battle b(gs, reg, attacker(), garrison(), 1);
        QCOMPARE(b.wallLevel(), 2);
        QCOMPARE(b.towers().size(), 3);
        QCOMPARE(b.wallMaxHp(), 2 * reg.balance().siege.wallHpPerLevel);
    }

    void wallsStopWalkersButNotTheDefendersAtTheGate() {
        walls(1);
        Battle b(gs, reg, attacker(), garrison(), 1);
        for (const BattleUnit& u : b.units()) {
            const QHash<Pos, int> reach = b.reachable(u);
            for (auto it = reach.cbegin(); it != reach.cend(); ++it) {
                if (u.side == 0) QVERIFY2(it.key().x < kWallColumn, "an attacker got through the wall");
            }
            if (u.side == 1) QVERIFY(!b.blockedFor(b.gate(), u));
            else QVERIFY(b.blockedFor(b.gate(), u));
        }
    }

    void towersShootTheAttackers() {
        walls(1);
        Battle b(gs, reg, attacker(), garrison(), 1);
        // The first round has begun: the towers have shot already.
        const BattleUnit* pikes = nullptr;
        for (const BattleUnit& u : b.units()) if (u.side == 0) pikes = &u;
        QVERIFY(pikes);
        QVERIFY(pikes->stack.totalHp() < 30 * reg.unit("pikeman")->maxHp);
    }

    void theCatapultBreaksTheWall() {
        walls(1);
        gs.player(p1)->resources = {{"gold", 5000}, {"wood", 50}};
        Battle b(gs, reg, attacker(), garrison(), 1);
        QList<BattleEvent> ev;
        QVERIFY2(buySiegeEngine(gs, reg, b, &ev).isEmpty(), "could not buy a catapult");
        QCOMPARE(gs.player(p1)->resources.value("gold"), 5000 - reg.balance().siege.engineCost.value("gold"));
        QVERIFY(b.engineBuilt());
        QVERIFY(!buySiegeEngine(gs, reg, b).isEmpty());  // only one per battle
        // Let the battle AI play: the catapult aims at the gate until it falls.
        int hits = 0;
        bool gateFell = false;
        for (int i = 0; i < 400 && !b.isOver() && !gateFell; ++i) {
            QList<BattleEvent> e;
            b.act(chooseBattleAction(b), &e);
            for (const BattleEvent& x : e) {
                if (x.type == BattleEvent::Type::WallHit) ++hits;
                if (x.type == BattleEvent::Type::WallHit && x.killed) gateFell = !b.walls().contains(b.gate());
            }
        }
        QVERIFY(hits > 0);
        QVERIFY(gateFell);
    }

    void theCatapultIsGoneAfterTheBattle() {
        walls(1);
        gs.player(p1)->resources = {{"gold", 5000}, {"wood", 50}};
        gs.army(attacker())->addUnits("armoured_eagle", 10, reg.unit("armoured_eagle")->maxHp);  // flyers win it
        Battle b(gs, reg, attacker(), garrison(), 2);
        QVERIFY(buySiegeEngine(gs, reg, b).isEmpty());
        while (!b.isOver()) b.act(chooseBattleAction(b));
        const BattleReport r = applyBattleResult(gs, reg, b);
        QVERIFY(r.attackerWon());
        for (const UnitStack& s : gs.army(attacker())->stacks) QVERIFY(s.unitTypeId() != "catapult");
        QVERIFY(!r.attacker.losses.contains("catapult"));
    }

    void anAiAttackerBuysACatapultWhenAutoResolving() {
        walls(1);
        gs.player(p1)->resources = {{"gold", 5000}, {"wood", 50}};
        resolveBattle(gs, reg, attacker(), garrison());
        QCOMPARE(gs.player(p1)->resources.value("gold"), 5000 - reg.balance().siege.engineCost.value("gold"));
    }
};

QTEST_APPLESS_MAIN(TstSiege)
#include "tst_siege.moc"
