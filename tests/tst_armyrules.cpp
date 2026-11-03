#include <QtTest>

#include "core/rules/ArmyRules.h"

using namespace kot::core;

class TstArmyRules : public QObject {
    Q_OBJECT
    GameState gs;
    PlayerId p1, p2;
    ArmyId a, b;

private slots:
    void init() {
        gs = GameState(1);
        p1 = gs.addPlayer("A", gs.newTeam(), Player::Controller::Human, "humans", "#ff0000");
        p2 = gs.addPlayer("B", gs.newTeam(), Player::Controller::Human, "humans", "#0000ff");
        a = gs.addArmy(p1, {2, 2});
        b = gs.addArmy(p1, {2, 2});
        gs.army(a)->addUnits("militia", 10, 6);
    }

    void movesPartOfAStack() {
        const ActionResult r = moveUnits(gs, a, b, 0, 4);
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(gs.army(a)->totalUnits(), 6);
        QCOMPARE(gs.army(b)->totalUnits(), 4);
    }

    void movingTheWholeStackRemovesIt() {
        QVERIFY(moveUnits(gs, a, b, 0, 10).ok);
        QVERIFY(gs.army(a)->stacks.isEmpty());
        QCOMPARE(gs.army(b)->stacks.size(), 1);
    }

    void wholeWoundedStackKeepsWounds() {
        gs.army(a)->stacks[0].applyDamage(4);  // 56 HP
        QVERIFY(moveUnits(gs, a, b, 0, 10).ok);
        QCOMPARE(gs.army(b)->stacks[0].totalHp(), 56);
    }

    void partialMoveLeavesWoundsBehind() {
        gs.army(a)->stacks[0].applyDamage(4);
        QVERIFY(moveUnits(gs, a, b, 0, 3).ok);
        QCOMPARE(gs.army(a)->stacks[0].totalHp(), 38);
        QCOMPARE(gs.army(b)->stacks[0].totalHp(), 18);
    }

    void mergesIntoExistingStack() {
        gs.army(b)->addUnits("militia", 2, 6);
        QVERIFY(moveUnits(gs, a, b, 0, 3).ok);
        QCOMPARE(gs.army(b)->stacks.size(), 1);
        QCOMPARE(gs.army(b)->totalUnits(), 5);
    }

    void fullTargetAcceptsOnlyMerges() {
        for (int i = 0; i < kMaxArmyStacks; ++i) gs.army(b)->addUnits(QString("u%1").arg(i), 1, 5);
        ActionResult r = moveUnits(gs, a, b, 0, 2);
        QVERIFY(!r.ok);
        QVERIFY(r.error.contains("no free slot"));
        QCOMPARE(gs.army(a)->totalUnits(), 10);  // nothing was lost
        gs.army(a)->addUnits("u0", 1, 5);
        QVERIFY(moveUnits(gs, a, b, 1, 1).ok);  // merges into u0
    }

    void movingEverythingTakesWhatFits() {
        gs.army(a)->addUnits("archer", 5, 10);
        gs.army(a)->addUnits("pikeman", 3, 10);
        int moved = 0;
        QVERIFY(moveAllUnits(gs, a, b, &moved).ok);
        QCOMPARE(moved, 18);
        QVERIFY(gs.army(a)->stacks.isEmpty());
        QCOMPARE(gs.army(b)->stacks.size(), 3);
        // A full army takes only what joins its stacks.
        for (int i = 3; i < kMaxArmyStacks; ++i) gs.army(b)->addUnits(QString("u%1").arg(i), 1, 5);
        gs.army(a)->addUnits("zombie", 4, 10);
        gs.army(a)->addUnits("militia", 2, 6);
        QVERIFY(moveAllUnits(gs, a, b, &moved).ok);
        QCOMPARE(moved, 2);                       // the militia joined
        QCOMPARE(gs.army(a)->totalUnits(), 4);    // the zombies stay
        QVERIFY(!moveAllUnits(gs, a, b).ok);      // nothing more fits
    }

    void rejections() {
        QVERIFY(moveUnits(gs, a, ArmyId(999), 0, 1).error.contains("unknown army"));
        QVERIFY(moveUnits(gs, a, a, 0, 1).error.contains("same army"));
        QVERIFY(moveUnits(gs, a, b, 5, 1).error.contains("no such stack"));
        QVERIFY(moveUnits(gs, a, b, 0, 0).error.contains("invalid unit count"));
        QVERIFY(moveUnits(gs, a, b, 0, 11).error.contains("invalid unit count"));

        const ArmyId enemy = gs.addArmy(p2, {2, 2});
        QVERIFY(moveUnits(gs, a, enemy, 0, 1).error.contains("different players"));
        const ArmyId far = gs.addArmy(p1, {7, 7});
        QVERIFY(moveUnits(gs, a, far, 0, 1).error.contains("not on the same tile"));
        const ArmyId neutral = gs.addArmy(PlayerId(), {2, 2});
        QVERIFY(!moveUnits(gs, neutral, a, 0, 1).ok);
    }
};

QTEST_APPLESS_MAIN(TstArmyRules)
#include "tst_armyrules.moc"
