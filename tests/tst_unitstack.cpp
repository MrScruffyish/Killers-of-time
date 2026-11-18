#include <QtTest>

#include "core/model/UnitStack.h"

using namespace kot::core;

class TstUnitStack : public QObject {
    Q_OBJECT
private slots:
    void fullStack() {
        UnitStack s(UnitTypeId(0), 10, 10);
        QCOMPARE(s.count(), 10);
        QCOMPARE(s.totalHp(), 100);
        QCOMPARE(s.frontUnitHp(), 10);
        QVERIFY(!s.isEliminated());
    }

    void damageBelowOneUnitKillsNobody() {
        UnitStack s(UnitTypeId(0), 10, 10);
        const auto r = s.applyDamage(5);
        QCOMPARE(r.damageDealt, 5);
        QCOMPARE(r.unitsKilled, 0);
        QCOMPARE(s.count(), 10);
        QCOMPARE(s.frontUnitHp(), 5);
    }

    void exactlyOneUnit() {
        UnitStack s(UnitTypeId(0), 10, 10);
        const auto r = s.applyDamage(10);
        QCOMPARE(r.unitsKilled, 1);
        QCOMPARE(s.count(), 9);
        QCOMPARE(s.frontUnitHp(), 10);
    }

    void damageSpillsOverToNextUnit() {
        // 25 damage: two units die, third is left with 5 HP.
        UnitStack s(UnitTypeId(0), 10, 10);
        const auto r = s.applyDamage(25);
        QCOMPARE(r.unitsKilled, 2);
        QCOMPARE(s.count(), 8);
        QCOMPARE(s.totalHp(), 75);
        QCOMPARE(s.frontUnitHp(), 5);
    }

    void overkillEliminatesAndCapsDamage() {
        UnitStack s(UnitTypeId(0), 10, 10);
        const auto r = s.applyDamage(1000);
        QCOMPARE(r.damageDealt, 100);
        QCOMPARE(r.unitsKilled, 10);
        QVERIFY(r.eliminated);
        QVERIFY(s.isEliminated());
        QCOMPARE(s.count(), 0);
        QCOMPARE(s.frontUnitHp(), 0);
    }

    void zeroAndNegativeDamageDoNothing() {
        UnitStack s(UnitTypeId(0), 3, 7);
        QCOMPARE(s.applyDamage(0).damageDealt, 0);
        QCOMPARE(s.applyDamage(-5).damageDealt, 0);
        QCOMPARE(s.totalHp(), 21);
    }

    void damageOnEliminatedStack() {
        UnitStack s(UnitTypeId(0), 1, 5);
        s.applyDamage(5);
        const auto r = s.applyDamage(3);
        QCOMPARE(r.damageDealt, 0);
        QVERIFY(r.eliminated);
    }

    void splitDamageEqualsSingleHit() {
        UnitStack a(UnitTypeId(1), 7, 13);
        UnitStack b(UnitTypeId(1), 7, 13);
        a.applyDamage(3);
        a.applyDamage(14);
        a.applyDamage(20);
        b.applyDamage(37);
        QCOMPARE(a.totalHp(), b.totalHp());
        QCOMPARE(a.count(), b.count());
        QCOMPARE(a.frontUnitHp(), b.frontUnitHp());
    }

    void healIsCappedAndDoesNotRevive() {
        UnitStack s(UnitTypeId(0), 10, 10);
        s.applyDamage(25);  // 8 units alive, front 5/10
        QCOMPARE(s.heal(3), 3);
        QCOMPARE(s.frontUnitHp(), 8);
        QCOMPARE(s.heal(100), 2);  // only 2 HP missing among living units
        QCOMPARE(s.count(), 8);
        QCOMPARE(s.totalHp(), 80);
    }

    void healEliminatedStackDoesNothing() {
        UnitStack s(UnitTypeId(0), 1, 5);
        s.applyDamage(5);
        QCOMPARE(s.heal(10), 0);
        QVERIFY(s.isEliminated());
    }

    void addUnitsAndRestore() {
        UnitStack s(UnitTypeId(0), 4, 10);
        s.applyDamage(15);  // 25 HP left: 3 units, front 5
        s.addUnits(2);      // +20 HP
        QCOMPARE(s.count(), 5);
        QCOMPARE(s.totalHp(), 45);
        s.restoreFullHealth();
        QCOMPARE(s.totalHp(), 50);
        QCOMPARE(s.frontUnitHp(), 10);
    }

    void effectiveMaxHpFormula() {
        QCOMPARE(effectiveMaxHp(10, {}), 10);
        QCOMPARE(effectiveMaxHp(10, {7, 0.0}), 17);
        QCOMPARE(effectiveMaxHp(10, {0, 0.2}), 12);
        QCOMPARE(effectiveMaxHp(10, {7, 0.1}), 19);   // 17 * 1.1 = 18.7 -> 19
        QCOMPARE(effectiveMaxHp(1, {-5, 0.0}), 1);    // never below 1
    }

    void splitTakesFreshUnitsAndKeepsWounds() {
        UnitStack s(UnitTypeId(0), 10, 10);
        s.applyDamage(25);  // 8 units, front wounded: 75 HP
        const UnitStack part = s.split(3);
        QCOMPARE(part.count(), 3);
        QCOMPARE(part.totalHp(), 30);
        QCOMPARE(s.totalHp(), 45);
        QCOMPARE(s.count(), 5);
        QCOMPARE(s.frontUnitHp(), 5);
    }

    void splitRejectsBadCounts() {
        UnitStack s(UnitTypeId(0), 4, 10);
        QCOMPARE(s.split(0).count(), 0);
        QCOMPARE(s.split(4).count(), 0);  // whole stack: use move instead
        QCOMPARE(s.split(9).count(), 0);
        QCOMPARE(s.count(), 4);
    }

    void mergePoolsHp() {
        UnitStack a(UnitTypeId(0), 3, 10);
        UnitStack b(UnitTypeId(0), 2, 10);
        b.applyDamage(5);
        QVERIFY(a.canMerge(b));
        a.merge(b);
        QCOMPARE(a.totalHp(), 45);
        QCOMPARE(a.count(), 5);
        UnitStack other(UnitTypeId(1), 2, 10);
        QVERIFY(!a.canMerge(other));
        UnitStack differentHp(UnitTypeId(0), 2, 12);
        QVERIFY(!a.canMerge(differentHp));
        a.merge(other);  // ignored
        QCOMPARE(a.totalHp(), 45);
    }

    void heroBonusStackExample() {
        // Hero gives +7 flat and +10%: base 10 HP -> 19 HP per unit.
        UnitStack s(UnitTypeId(0), 5, effectiveMaxHp(10, {7, 0.1}));
        QCOMPARE(s.maxHp(), 19);
        QCOMPARE(s.totalHp(), 95);
    }

    void invalidMaxHpIsClamped() {
        UnitStack s(UnitTypeId(2), 3, 0);
        QCOMPARE(s.maxHp(), 1);
        QCOMPARE(s.count(), 3);
    }
};

QTEST_APPLESS_MAIN(TstUnitStack)
#include "tst_unitstack.moc"
