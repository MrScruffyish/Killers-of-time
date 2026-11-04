#include <QSet>
#include <QtTest>

#include "core/content/ContentRegistry.h"
#include "core/rules/MapGenerator.h"
#include "core/rules/Pathfinding.h"

using namespace kot::core;

class TstMapGen : public QObject {
    Q_OBJECT
    ContentRegistry reg;

    static MapGenParams params(int size, std::uint64_t seed) {
        MapGenParams p;
        p.width = size;
        p.height = size;
        p.seed = seed;
        return p;
    }

    static int largestWalkableRegion(const GameMap& map, const ContentRegistry& reg) {
        const MoveCostGrid grid(map, reg, MoveMode::Foot);
        QSet<Pos> seen;
        int best = 0;
        for (int y = 0; y < map.height(); ++y) {
            for (int x = 0; x < map.width(); ++x) {
                const Pos p{x, y};
                if (grid.cost(p) < 0 || seen.contains(p)) continue;
                const QHash<Pos, int> region = reachableTiles(grid, p, 1000000000);
                for (auto it = region.begin(); it != region.end(); ++it) seen.insert(it.key());
                best = std::max(best, static_cast<int>(region.size()));
            }
        }
        return best;
    }

private slots:
    void initTestCase() {
        QStringList errors;
        QVERIFY2(reg.loadFromDirectory(KOT_DATA_DIR, &errors), qPrintable(errors.join("\n")));
    }

    void sameSeedSameMap() {
        GameMap a, b;
        QVERIFY(generateMap(reg, params(64, 7), &a));
        QVERIFY(generateMap(reg, params(64, 7), &b));
        QCOMPARE(a.tiles(), b.tiles());
    }

    void differentSeedsDiffer() {
        GameMap a, b;
        QVERIFY(generateMap(reg, params(64, 7), &a));
        QVERIFY(generateMap(reg, params(64, 8), &b));
        QVERIFY(a.tiles() != b.tiles());
    }

    void sizeAndKnownTerrain() {
        GameMap m;
        MapGenParams p = params(50, 3);
        p.height = 30;
        QVERIFY(generateMap(reg, p, &m));
        QCOMPARE(m.width(), 50);
        QCOMPARE(m.height(), 30);
        QCOMPARE(m.tiles().size(), 50 * 30);
        for (const QString& t : m.tiles()) QVERIFY2(reg.terrainType(t), qPrintable(t));
    }

    void hasVarietyAndReasonableSea() {
        for (std::uint64_t seed = 1; seed <= 4; ++seed) {
            GameMap m;
            QVERIFY(generateMap(reg, params(96, seed), &m));
            QSet<QString> kinds;
            int sea = 0;
            for (const QString& t : m.tiles()) {
                kinds.insert(t);
                if (t == "sea") ++sea;
            }
            QVERIFY2(kinds.size() >= 6, qPrintable(QString("seed %1: %2 kinds").arg(seed).arg(kinds.size())));
            const double seaFraction = double(sea) / m.tiles().size();
            QVERIFY2(seaFraction > 0.25 && seaFraction < 0.40, qPrintable(QString::number(seaFraction)));
        }
    }

    void hasHighlands() {
        for (std::uint64_t seed = 1; seed <= 4; ++seed) {
            GameMap m;
            QVERIFY(generateMap(reg, params(80, seed), &m));
            int mountains = 0, hills = 0;
            for (const QString& t : m.tiles()) {
                if (t == "mountains") ++mountains;
                if (t == "hills") ++hills;
            }
            QVERIFY2(mountains > 50, qPrintable(QString("seed %1: %2 mountains").arg(seed).arg(mountains)));
            QVERIFY2(hills > 50, qPrintable(QString("seed %1: %2 hills").arg(seed).arg(hills)));
        }
    }

    void edgesAreSea() {
        GameMap m;
        QVERIFY(generateMap(reg, params(64, 2), &m));
        QCOMPARE(m.terrainAt({0, 0}), QString("sea"));
        QCOMPARE(m.terrainAt({63, 63}), QString("sea"));
    }

    void riversCanBeDisabled() {
        MapGenParams p = params(64, 4);
        p.rivers = 0;
        GameMap m;
        QVERIFY(generateMap(reg, p, &m));
        QVERIFY(!m.tiles().contains("river"));
    }

    void someLandIsConnected() {
        for (std::uint64_t seed = 1; seed <= 3; ++seed) {
            GameMap m;
            QVERIFY(generateMap(reg, params(64, seed), &m));
            const int largest = largestWalkableRegion(m, reg);
            QVERIFY2(largest > 64 * 64 / 4, qPrintable(QString("seed %1: %2").arg(seed).arg(largest)));
        }
    }

    void rejectsBadSize() {
        GameMap m;
        QString err;
        MapGenParams p = params(4, 1);
        QVERIFY(!generateMap(reg, p, &m, &err));
        QVERIFY(err.contains("map size"));
    }
};

QTEST_APPLESS_MAIN(TstMapGen)
#include "tst_mapgen.moc"
