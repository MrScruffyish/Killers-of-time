#include <QtTest>

#include "core/model/Army.h"

using namespace kot::core;

class TstArmy : public QObject {
    Q_OBJECT
private slots:
    void addUnitsMergesSameType() {
        Army a;
        QVERIFY(a.addUnits("militia", 5, 6));
        QVERIFY(a.addUnits("militia", 3, 6));
        QCOMPARE(a.stacks.size(), 1);
        QCOMPARE(a.totalUnits(), 8);
    }

    void differentMaxHpMakesNewStack() {
        Army a;
        a.addUnits("militia", 5, 6);
        a.addUnits("militia", 5, 8);
        QCOMPARE(a.stacks.size(), 2);
    }

    void atMostSevenStacks() {
        Army a;
        for (int i = 0; i < kMaxArmyStacks; ++i)
            QVERIFY(a.addUnits(QString("unit%1").arg(i), 1, 5));
        QVERIFY(!a.addUnits("one_too_many", 1, 5));
        QVERIFY(a.addUnits("unit0", 2, 5));  // merging still works when full
        QCOMPARE(a.stacks.size(), kMaxArmyStacks);
    }

    void removeEmptyStacks() {
        Army a;
        a.addUnits("a", 2, 5);
        a.addUnits("b", 2, 5);
        a.stacks[0].applyDamage(1000);
        a.removeEmptyStacks();
        QCOMPARE(a.stacks.size(), 1);
        QCOMPARE(a.stacks[0].unitTypeId(), QString("b"));
    }

    void heroPresence() {
        Army a;
        QVERIFY(!a.hasHero());
        a.hero = HeroId(3);
        QVERIFY(a.hasHero());
    }

    void stackFromTotalHp() {
        const UnitStack s = UnitStack::fromTotalHp("eagle", 75, 10);
        QCOMPARE(s.count(), 8);
        QCOMPARE(s.frontUnitHp(), 5);
        QCOMPARE(s.maxHp(), 10);
    }
};

QTEST_APPLESS_MAIN(TstArmy)
#include "tst_army.moc"
