#include <QtTest>

#include "TestContent.h"

#include "core/model/Army.h"

using namespace kot::core;

class TstArmy : public QObject {
    Q_OBJECT
private slots:
    void addUnitsMergesSameType() {
        Army a;
        QVERIFY(a.addUnits(UnitTypeId(103), 5, 6));
        QVERIFY(a.addUnits(UnitTypeId(103), 3, 6));
        QCOMPARE(a.stacks.size(), 1);
        QCOMPARE(a.totalUnits(), 8);
    }

    void differentMaxHpMakesNewStack() {
        Army a;
        a.addUnits(UnitTypeId(103), 5, 6);
        a.addUnits(UnitTypeId(103), 5, 8);
        QCOMPARE(a.stacks.size(), 2);
    }

    void atMostSevenStacks() {
        Army a;
        for (int i = 0; i < kMaxArmyStacks; ++i)
            QVERIFY(a.addUnits(UnitTypeId(i), 1, 5));
        QVERIFY(!a.addUnits(UnitTypeId(104), 1, 5));
        QVERIFY(a.addUnits(UnitTypeId(0), 2, 5));  // merging still works when full
        QCOMPARE(a.stacks.size(), kMaxArmyStacks);
    }

    void removeEmptyStacks() {
        Army a;
        a.addUnits(UnitTypeId(100), 2, 5);
        a.addUnits(UnitTypeId(101), 2, 5);
        a.stacks[0].applyDamage(1000);
        a.removeEmptyStacks();
        QCOMPARE(a.stacks.size(), 1);
        QCOMPARE(a.stacks[0].unitTypeId(), UnitTypeId(101));
    }

    void heroPresence() {
        Army a;
        QVERIFY(!a.hasHero());
        a.hero = HeroId(3);
        QVERIFY(a.hasHero());
    }

    void stackFromTotalHp() {
        const UnitStack s = UnitStack::fromTotalHp(UnitTypeId(102), 75, 10);
        QCOMPARE(s.count(), 8);
        QCOMPARE(s.frontUnitHp(), 5);
        QCOMPARE(s.maxHp(), 10);
    }
};

QTEST_APPLESS_MAIN(TstArmy)
#include "tst_army.moc"
