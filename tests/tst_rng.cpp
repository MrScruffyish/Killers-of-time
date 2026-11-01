#include <QtTest>

#include "core/util/Rng.h"

using kot::core::Rng;

class TstRng : public QObject {
    Q_OBJECT
private slots:
    void sameSeedSameSequence() {
        Rng a(12345);
        Rng b(12345);
        for (int i = 0; i < 1000; ++i) {
            QCOMPARE(a.nextU32(), b.nextU32());
        }
    }

    void differentSeedsDiffer() {
        Rng a(1);
        Rng b(2);
        bool different = false;
        for (int i = 0; i < 10; ++i) {
            different = different || (a.nextU32() != b.nextU32());
        }
        QVERIFY(different);
    }

    void reseedRestartsSequence() {
        Rng a(99);
        const auto first = a.nextU32();
        a.nextU32();
        a.reseed(99);
        QCOMPARE(a.nextU32(), first);
        QCOMPARE(a.seed(), std::uint64_t(99));
    }

    void nextIntStaysInRange() {
        Rng r(7);
        for (int i = 0; i < 10000; ++i) {
            const int v = r.nextInt(-3, 5);
            QVERIFY(v >= -3 && v <= 5);
        }
    }

    void nextIntSingleValue() {
        Rng r(7);
        QCOMPARE(r.nextInt(5, 5), 5);
    }

    void nextIntCoversWholeRange() {
        Rng r(3);
        bool seen[6] = {};
        for (int i = 0; i < 1000; ++i) {
            seen[r.nextInt(0, 5)] = true;
        }
        for (bool s : seen) QVERIFY(s);
    }

    void nextDoubleInUnitInterval() {
        Rng r(5);
        for (int i = 0; i < 10000; ++i) {
            const double d = r.nextDouble();
            QVERIFY(d >= 0.0 && d < 1.0);
        }
    }

    void chanceExtremes() {
        Rng r(11);
        for (int i = 0; i < 100; ++i) {
            QVERIFY(!r.chance(0.0));
            QVERIFY(r.chance(1.0));
        }
    }
};

QTEST_APPLESS_MAIN(TstRng)
#include "tst_rng.moc"
