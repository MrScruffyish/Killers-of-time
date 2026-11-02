#include <QtTest>

#include "core/battle/AutoBattle.h"
#include "core/battle/Battle.h"
#include "core/battle/BattleAi.h"
#include "core/content/ContentRegistry.h"
#include "core/rules/HeroRules.h"

using namespace kot::core;

class TstHexBattle : public QObject {
    Q_OBJECT
    ContentRegistry reg;
    GameState gs;
    PlayerId p1;
    HeroId hero;

    ArmyId mine() { return gs.hero(hero)->army; }
    ArmyId monsters(const QString& unit, int count) {
        const ArmyId a = gs.addArmy(PlayerId(), {1, 0});
        gs.army(a)->addUnits(unit, count, reg.unit(unit)->maxHp);
        return a;
    }
    void add(const QString& unit, int count) { gs.army(mine())->addUnits(unit, count, reg.unit(unit)->maxHp); }
    static BattleAction move(Pos h) { BattleAction a; a.type = BattleAction::Type::Move; a.hex = h; a.hasHex = true; return a; }
    static BattleAction attack(int target) { BattleAction a; a.type = BattleAction::Type::Attack; a.target = target; return a; }
    static BattleAction shoot(int target) { BattleAction a; a.type = BattleAction::Type::Shoot; a.target = target; return a; }
    static BattleAction defend() { return BattleAction{}; }

private slots:
    void initTestCase() {
        QStringList errors;
        QVERIFY2(reg.loadFromDirectory(KOT_DATA_DIR, &errors), qPrintable(errors.join("\n")));
    }
    void init() {
        gs = GameState(3);
        gs.setMap(GameMap(8, 8, "grass"));
        p1 = gs.addPlayer("A", gs.newTeam(), Player::Controller::Human, "humans", "#ff0000");
        hero = createHero(gs, reg, p1, "H", "knight", {0, 0});
    }

    void hexGeometry() {
        QCOMPARE(hex::distance({0, 0}, {0, 0}), 0);
        QCOMPARE(hex::distance({0, 0}, {14, 0}), 14);
        QCOMPARE(hex::neighbours({5, 5}).size(), 6);
        QCOMPARE(hex::neighbours({0, 0}).size(), 2);
        for (const Pos n : hex::neighbours({4, 3})) QCOMPARE(hex::distance({4, 3}, n), 1);
        for (const Pos n : hex::neighbours({4, 4})) QCOMPARE(hex::distance({4, 4}, n), 1);
        QVERIFY(hex::adjacent({4, 4}, {3, 5}));   // even row: down-left
        QVERIFY(hex::adjacent({4, 3}, {5, 4}));   // odd row: down-right
        QVERIFY(!hex::adjacent({4, 4}, {5, 5}));
    }

    void startPositionsAndOrder() {
        add("militia", 10);
        add("armoured_eagle", 2);
        const ArmyId m = monsters("pikeman", 5);
        Battle b(gs, reg, mine(), m, 1);
        QCOMPARE(b.units().size(), 3);
        for (const BattleUnit& u : b.units()) QCOMPARE(u.pos.x, u.side == 0 ? 0 : hex::kWidth - 1);
        QCOMPARE(b.activeUnit()->def->id, QString("armoured_eagle"));  // initiative 12
        QCOMPARE(b.round(), 1);
    }

    void movingWithinBattleSpeed() {
        add("militia", 10);  // battle speed 4
        Battle b(gs, reg, mine(), monsters("battle_snail", 1), 1);
        const BattleUnit* me = b.activeUnit();
        QCOMPARE(me->side, 0);
        QVERIFY(!b.act(move({6, me->pos.y})).isEmpty());   // too far
        QVERIFY(b.act(move({4, me->pos.y})).isEmpty());
        QCOMPARE(b.units()[0].pos, (Pos{4, 5}));
        QCOMPARE(b.activeUnit()->side, 1);                  // the snail's turn
    }

    void meleeGetsOneRetaliationPerRound() {
        add("pikeman", 10);
        gs.army(mine())->stacks.append(UnitStack("pikeman", 10, 10));  // a second, separate stack
        Battle b(gs, reg, mine(), monsters("battle_snail", 20), 1);
        // Walk both pikemen next to the snail over a few rounds.
        while (!b.isOver() && b.round() < 6) {
            const BattleUnit* me = b.activeUnit();
            if (me->side == 1) { b.act(defend()); continue; }
            QList<BattleEvent> ev;
            const BattleUnit* snail = nullptr;
            for (const BattleUnit& u : b.units()) if (u.side == 1) snail = &u;
            Pos from;
            if (b.attackHex(*me, *snail, &from)) {
                const int retaliatedBefore = snail->retaliated;
                QVERIFY(b.act(attack(snail->id), &ev).isEmpty());
                int retaliations = 0;
                for (const BattleEvent& e : ev) retaliations += e.type == BattleEvent::Type::Retaliate;
                QCOMPARE(retaliations, retaliatedBefore ? 0 : 1);
            } else {
                b.act(chooseBattleAction(b));
            }
        }
    }

    void archersShootWithoutRetaliationUntilEnemyIsNext() {
        add("archer", 10);
        Battle b(gs, reg, mine(), monsters("pikeman", 50), 1);
        const BattleUnit* archer = b.activeUnit();
        const int shots = archer->shotsLeft;
        QVERIFY(b.canShoot(*archer));
        QList<BattleEvent> ev;
        QVERIFY(b.act(shoot(2), &ev).isEmpty());
        QCOMPARE(b.unit(1)->shotsLeft, shots - 1);
        for (const BattleEvent& e : ev) QVERIFY(e.type != BattleEvent::Type::Retaliate);
        QVERIFY(b.unit(2)->stack.count() < 50);
        // Now the pikemen act: they have no ranged attack.
        QCOMPARE(b.activeUnit()->id, 2);
        QVERIFY(!b.act(shoot(1)).isEmpty());
    }

    void defendingForeverIsADraw() {
        add("militia", 5);
        Battle b(gs, reg, mine(), monsters("militia", 5), 1);
        while (!b.isOver()) QVERIFY(b.act(defend()).isEmpty());
        QCOMPARE(b.outcome(), Battle::Outcome::Draw);
        QCOMPARE(b.round(), reg.balance().battle.maxRounds);
        QVERIFY(!b.act(defend()).isEmpty());
    }

    void aiPlaysToTheEndAndIsDeterministic() {
        add("pikeman", 20);
        add("archer", 6);
        QJsonObject first;
        for (int run = 0; run < 2; ++run) {
            const ArmyId m = monsters("militia", 25);
            Battle b(gs, reg, mine(), m, 42);
            QList<BattleEvent> all;
            while (!b.isOver()) QVERIFY(b.act(chooseBattleAction(b), &all).isEmpty());
            QVERIFY(b.outcome() != Battle::Outcome::Running);
            QJsonObject j = battleToJson(b);
            j.insert("events", int(all.size()));
            if (run == 0) first = j;
            else QCOMPARE(j, first);
            gs.armies().remove(m);
        }
    }

    void applyingTheResultChangesTheArmies() {
        add("pikeman", 30);
        const ArmyId m = monsters("militia", 4);
        Battle b(gs, reg, mine(), m, 5);
        while (!b.isOver()) b.act(chooseBattleAction(b));
        QCOMPARE(b.outcome(), Battle::Outcome::AttackerWon);
        const BattleReport r = applyBattleResult(gs, reg, b);
        QVERIFY(r.attackerWon());
        QVERIFY(!gs.army(m));
        QCOMPARE(r.defender.losses.value("militia"), 4);
        QCOMPARE(gs.hero(hero)->experience, r.experience);
    }

    void jsonForTheClient() {
        add("archer", 3);
        Battle b(gs, reg, mine(), monsters("militia", 3), 1);
        const QJsonObject j = battleToJson(b);
        QCOMPARE(j["width"].toInt(), hex::kWidth);
        QCOMPARE(j["units"].toArray().size(), 2);
        QCOMPARE(j["active"].toInt(), 1);
        QVERIFY(j["canShoot"].toBool());
        QVERIFY(!j["reachable"].toArray().isEmpty());
    }
};

QTEST_GUILESS_MAIN(TstHexBattle)
#include "tst_hexbattle.moc"
