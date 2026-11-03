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

    void shootingNeedsTheTargetInRange() {
        add("archer", 10);  // range 5; the armies start 14 hexes apart
        {
            Battle b(gs, reg, mine(), monsters("pikeman", 50), 1);
            const BattleUnit* archer = b.activeUnit();
            QCOMPARE(b.shootRange(*archer), 5);
            QVERIFY(b.canShoot(*archer));
            QVERIFY(!b.canShootAt(*archer, *b.unit(2)));
            QVERIFY(b.act(shoot(2)).contains("out of range"));
            // The battle AI walks toward range instead of shooting at nothing.
            const BattleAction a = chooseBattleAction(b);
            QCOMPARE(a.type, BattleAction::Type::Move);
            QVERIFY(hex::distance(a.hex, b.unit(2)->pos) < hex::distance(archer->pos, b.unit(2)->pos));
        }
        // The hero's Attack Range trait adds to it.
        gs.hero(hero)->traits["attack_range"] = 9;
        Battle b(gs, reg, mine(), monsters("pikeman", 50), 1);
        QCOMPARE(b.shootRange(*b.activeUnit()), 14);
        QVERIFY(b.canShootAt(*b.activeUnit(), *b.unit(2)));
        gs.hero(hero)->traits.remove("attack_range");
    }

    void archersShootWithoutRetaliationUntilEnemyIsNext() {
        add("archer", 10);
        gs.hero(hero)->traits["attack_range"] = 10;  // reach across the field
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

    // ---- depth: abilities, hero traits, wait, retreat, rocks
    void rocksBlockWalkers() {
        add("militia", 5);
        Battle b(gs, reg, mine(), monsters("militia", 5), 1);
        QCOMPARE(b.obstacles().size(), reg.balance().battle.obstacles);
        for (const Pos p : b.obstacles()) {
            QVERIFY(p.x >= 3 && p.x <= hex::kWidth - 4);
            QVERIFY(b.blocked(p));
            for (const BattleUnit& u : b.units()) QVERIFY(!b.reachable(u).contains(p));
        }
    }

    void flyersPassOverStacks() {
        add("armoured_eagle", 2);  // flying, speed 12
        gs.army(mine())->stacks.append(UnitStack("militia", 1, 6));
        const ArmyId m = monsters("militia", 3);
        Battle b(gs, reg, mine(), m, 1);
        const BattleUnit* eagle = b.activeUnit();
        QVERIFY(eagle->def->has("flying"));
        // Every free hex within its speed is reachable, stacks or rocks in between or not.
        int free = 0;
        for (int y = 0; y < hex::kHeight; ++y)
            for (int x = 0; x < hex::kWidth; ++x)
                free += (!b.blocked({x, y}) && hex::distance(eagle->pos, {x, y}) <= 12 && Pos{x, y} != eagle->pos);
        QCOMPARE(b.reachable(*eagle).size() - 1, free);
    }

    void noRetaliationAgainstWraiths() {
        gs.army(mine())->addUnits("wraith", 5, 18);
        Battle b(gs, reg, mine(), monsters("pikeman", 5), 1);
        // Over the battle the wraith attacks, and nothing ever strikes back at it.
        int attacks = 0;
        while (!b.isOver()) {
            QList<BattleEvent> ev;
            b.act(chooseBattleAction(b), &ev);
            for (const BattleEvent& e : ev) {
                attacks += (e.type == BattleEvent::Type::Attack && e.unit == 1);
                QVERIFY(!(e.type == BattleEvent::Type::Retaliate && e.target == 1));
            }
        }
        QVERIFY(attacks > 0);
    }

    void marksmenShootTwice() {
        add("elf_marksman", 4);
        gs.hero(hero)->traits["attack_range"] = 10;
        Battle b(gs, reg, mine(), monsters("treant", 3), 1);
        const int shots = b.activeUnit()->shotsLeft;
        QList<BattleEvent> ev;
        QVERIFY(b.act(shoot(2), &ev).isEmpty());
        int shotsFired = 0;
        for (const BattleEvent& e : ev) shotsFired += e.type == BattleEvent::Type::Shoot;
        QCOMPARE(shotsFired, 2);
        QCOMPARE(b.unit(1)->shotsLeft, shots - 2);
    }

    void goodMoraleGivesAnExtraAction() {
        gs.hero(hero)->traits["morale"] = 20;  // 100%
        add("militia", 5);
        Battle b(gs, reg, mine(), monsters("battle_snail", 1), 1);
        QList<BattleEvent> ev;
        QVERIFY(b.act(move({2, 5}), &ev).isEmpty());
        QCOMPARE(ev.last().type, BattleEvent::Type::Morale);
        QCOMPARE(b.activeUnit()->id, 1);  // again
        QVERIFY(b.act(move({3, 5}), &ev).isEmpty());
        QCOMPARE(b.activeUnit()->side, 1);  // only once per round
    }

    void badMoraleMakesStacksFalterButNotUndead() {
        gs.hero(hero)->traits["morale"] = -20;
        add("militia", 5);
        gs.army(mine())->stacks.append(UnitStack("skeleton", 5, 6));
        Battle b(gs, reg, mine(), monsters("battle_snail", 1), 1);
        // The militia always falters; the skeletons (undead) act.
        QCOMPARE(b.activeUnit()->def->id, QString("skeleton"));
    }

    void luckDoublesDamage() {
        gs.hero(hero)->traits["luck"] = 20;
        add("militia", 10);
        Battle b(gs, reg, mine(), monsters("battle_snail", 50), 1);
        while (!b.isOver() && b.round() < 4) {
            QList<BattleEvent> ev;
            b.act(chooseBattleAction(b), &ev);
            for (const BattleEvent& e : ev)
                if (e.type == BattleEvent::Type::Attack && b.unit(e.unit)->side == 0) QVERIFY(e.lucky);
        }
    }

    void hitPointsTraitRaisesMaxHpInBattleOnly() {
        gs.hero(hero)->traits["hit_points"] = 4;  // +20%
        add("pikeman", 10);
        const ArmyId m = monsters("militia", 2);
        Battle b(gs, reg, mine(), m, 1);
        QCOMPARE(b.unit(1)->stack.maxHp(), 12);
        QCOMPARE(b.unit(1)->stack.count(), 10);
        while (!b.isOver()) b.act(chooseBattleAction(b));
        applyBattleResult(gs, reg, b);
        QCOMPARE(gs.army(mine())->stacks[0].maxHp(), 10);
    }

    void waitingActsLater() {
        add("militia", 5);
        Battle b(gs, reg, mine(), monsters("pikeman", 5), 1);
        BattleAction wait;
        wait.type = BattleAction::Type::Wait;
        QCOMPARE(b.activeUnit()->id, 1);
        QVERIFY(b.act(wait).isEmpty());
        QCOMPARE(b.activeUnit()->id, 2);
        QVERIFY(b.act(defend()).isEmpty());
        QCOMPARE(b.activeUnit()->id, 1);  // its turn comes back
        QVERIFY(!b.act(wait).isEmpty());  // only once per round
        QCOMPARE(b.round(), 1);
    }

    void attackerCanRetreat() {
        add("militia", 5);
        const ArmyId m = monsters("bone_dragon", 3);
        Battle b(gs, reg, mine(), m, 1);
        BattleAction retreat;
        retreat.type = BattleAction::Type::Retreat;
        while (b.activeUnit()->side != 0) b.act(defend());
        QVERIFY(b.act(retreat).isEmpty());
        QCOMPARE(b.outcome(), Battle::Outcome::DefenderWon);
        const BattleReport r = applyBattleResult(gs, reg, b);
        QVERIFY(r.retreated);
        QVERIFY(!r.loserHeroKilled);
        QVERIFY(gs.hero(hero));
        QVERIFY(gs.army(mine())->stacks.isEmpty());
    }

    void townWallsHelpTheDefender() {
        add("militia", 5);
        const TownId t = gs.addTown("Fort", "humans", {2, 2}, PlayerId());
        Army& garrison = *gs.army(gs.town(t)->garrison);
        garrison.addUnits("pikeman", 5, 10);
        Battle open(gs, reg, mine(), gs.town(t)->garrison, 1);
        QCOMPARE(open.townDefense(), 0);
        gs.town(t)->buildings << "village_hall" << "fort" << "citadel";
        Battle walled(gs, reg, mine(), gs.town(t)->garrison, 1);
        QCOMPARE(walled.townDefense(), 6);
        QCOMPARE(battleToJson(walled)["townDefense"].toInt(), 6);
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
