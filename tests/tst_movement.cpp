#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

#include "core/content/ContentRegistry.h"
#include "core/rules/HeroRules.h"
#include "core/rules/Movement.h"

using namespace kot::core;

class TstMovement : public QObject {
    Q_OBJECT
    ContentRegistry reg;
    GameState gs;
    PlayerId p1, p2;

    HeroId makeHero(PlayerId owner, Pos pos) {
        return createHero(gs, reg, owner, "H", "knight", pos);
    }
    Army& armyOf(HeroId id) { return *gs.army(gs.hero(id)->army); }

private slots:
    void initTestCase() {
        QStringList errors;
        QVERIFY2(reg.loadFromDirectory(KOT_DATA_DIR, &errors), qPrintable(errors.join("\n")));
    }

    void init() {
        gs = GameState(1);
        gs.setMap(GameMap(10, 10, "grass"));
        p1 = gs.addPlayer("A", gs.newTeam(), Player::Controller::Human, "humans", "#ff0000");
        p2 = gs.addPlayer("B", gs.newTeam(), Player::Controller::Human, "humans", "#0000ff");
    }

    // ---- daily movement points
    void baseMovementFromTrait() {
        const HeroId h = makeHero(p1, {0, 0});  // knight: movement_points 1
        QCOMPARE(armyMovementPoints(armyOf(h), gs.hero(h), reg), 1100);
        QCOMPARE(armyMovementPoints(armyOf(h), nullptr, reg), 1000);
    }

    void slowUnitsLowerMovementALittle() {
        const HeroId h = makeHero(p1, {0, 0});
        Army& a = armyOf(h);
        a.addUnits("armoured_eagle", 2, 45);
        QCOMPARE(armyMovementPoints(a, gs.hero(h), reg), 1100);  // mapSpeed 10: no penalty
        a.addUnits("militia", 5, 6);                              // mapSpeed 4: -100
        QCOMPARE(armyMovementPoints(a, gs.hero(h), reg), 1000);
        a.addUnits("battle_snail", 1, 30);                        // mapSpeed 1: -300
        QCOMPARE(armyMovementPoints(a, gs.hero(h), reg), 800);
    }

    void eliminatedStacksDoNotSlowTheArmy() {
        const HeroId h = makeHero(p1, {0, 0});
        Army& a = armyOf(h);
        a.addUnits("battle_snail", 1, 30);
        a.stacks[0].applyDamage(1000);
        QCOMPARE(armyMovementPoints(a, gs.hero(h), reg), 1100);
    }

    void movementTraitPickGivesMorePoints() {
        const HeroId h = makeHero(p1, {0, 0});
        addExperience(*gs.hero(h), reg, 1000);
        QVERIFY(pickTrait(*gs.hero(h), reg, "movement_points"));
        QCOMPARE(armyMovementPoints(armyOf(h), gs.hero(h), reg), 1200);
    }

    void minimumMovementApplies() {
        // Copy the data and raise the minimum above what a snail army gets.
        QTemporaryDir tmp;
        QDirIterator it(KOT_DATA_DIR, QDir::Files, QDirIterator::Subdirectories);
        const QDir src(KOT_DATA_DIR);
        while (it.hasNext()) {
            it.next();
            const QString rel = src.relativeFilePath(it.filePath());
            QDir().mkpath(QFileInfo(tmp.filePath(rel)).absolutePath());
            QVERIFY(QFile::copy(it.filePath(), tmp.filePath(rel)));
        }
        QFile f(tmp.filePath("balance.json"));
        QVERIFY(f.open(QIODevice::ReadOnly));
        QString text = QString::fromUtf8(f.readAll());
        f.close();
        text.replace("\"minimum\": 300", "\"minimum\": 950");
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write(text.toUtf8());
        f.close();

        ContentRegistry strict;
        QStringList errors;
        QVERIFY2(strict.loadFromDirectory(tmp.path(), &errors), qPrintable(errors.join("\n")));
        const HeroId h = makeHero(p1, {0, 0});
        armyOf(h).addUnits("battle_snail", 1, 30);  // 1100 - 300 = 800 < 950
        QCOMPARE(armyMovementPoints(armyOf(h), gs.hero(h), strict), 950);
    }

    void refreshOnlyAffectsOwnersHeroes() {
        const HeroId a = makeHero(p1, {0, 0});
        const HeroId b = makeHero(p2, {5, 5});
        gs.hero(a)->movementPoints = 0;
        gs.hero(b)->movementPoints = 0;
        refreshHeroMovement(gs, reg, p1);
        QCOMPARE(gs.hero(a)->movementPoints, 1100);
        QCOMPARE(gs.hero(b)->movementPoints, 0);
    }

    // ---- moving heroes
    void moveWithinBudget() {
        const HeroId h = makeHero(p1, {0, 0});
        const MoveResult r = moveHero(gs, reg, h, {3, 0});
        QVERIFY2(r.ok, qPrintable(r.error));
        QVERIFY(r.arrived);
        QCOMPARE(r.stepsTaken, 3);
        QCOMPARE(r.movementSpent, 300);
        QCOMPARE(armyOf(h).pos, (Pos{3, 0}));
        QCOMPARE(gs.hero(h)->movementPoints, 800);
    }

    void moveStopsWhenOutOfMovementAndContinuesNextDay() {
        gs.setMap(GameMap(20, 10, "grass"));
        const HeroId h = makeHero(p1, {0, 0});  // 1100 movement points
        MoveResult r = moveHero(gs, reg, h, {15, 0});  // needs 1500
        QVERIFY(r.ok);
        QVERIFY(!r.arrived);
        QCOMPARE(r.stepsTaken, 11);
        QCOMPARE(r.movementSpent, 1100);
        QCOMPARE(gs.hero(h)->movementPoints, 0);
        QCOMPARE(armyOf(h).pos, (Pos{11, 0}));

        r = moveHero(gs, reg, h, {15, 0});
        QVERIFY(!r.ok);
        QVERIFY(r.error.contains("not enough movement points"));

        refreshHeroMovement(gs, reg, p1);
        r = moveHero(gs, reg, h, {15, 0});
        QVERIFY(r.ok);
        QVERIFY(r.arrived);
        QCOMPARE(r.movementSpent, 400);
        QCOMPARE(armyOf(h).pos, (Pos{15, 0}));
    }

    void cannotEnterOrCrossObjects() {
        const HeroId h = makeHero(p1, {0, 0});
        gs.addMine("gold", 1000, {5, 0});
        MoveResult r = moveHero(gs, reg, h, {5, 0});
        QVERIFY(!r.ok);
        QVERIFY(r.error.contains("occupied"));

        // Walking past the mine: goes around it (4 straight + 2 diagonal = 682).
        r = moveHero(gs, reg, h, {6, 0});
        QVERIFY2(r.ok, qPrintable(r.error));
        QVERIFY(r.arrived);
        QCOMPARE(r.movementSpent, 682);
    }

    void otherArmiesBlock() {
        const HeroId h = makeHero(p1, {0, 0});
        const HeroId enemy = makeHero(p2, {4, 0});
        MoveResult r = moveHero(gs, reg, h, {4, 0});
        QVERIFY(!r.ok);
        QVERIFY(r.error.contains("occupied"));
        Q_UNUSED(enemy);
    }

    void noPathAcrossWaterOnFoot() {
        for (int y = 0; y < 10; ++y) gs.map().setTerrain({5, y}, "sea");
        const HeroId h = makeHero(p1, {0, 0});
        const MoveResult r = moveHero(gs, reg, h, {8, 0});
        QVERIFY(!r.ok);
        QVERIFY(r.error.contains("no path"));
    }

    void boatModeSailsOnWater() {
        for (int y = 0; y < 10; ++y)
            for (int x = 2; x < 8; ++x) gs.map().setTerrain({x, y}, "sea");
        const HeroId h = makeHero(p1, {3, 3});
        MoveResult r = moveHero(gs, reg, h, {7, 3}, MoveMode::Boat);
        QVERIFY2(r.ok, qPrintable(r.error));
        QVERIFY(r.arrived);
        r = moveHero(gs, reg, h, {9, 3}, MoveMode::Boat);  // land: boat cannot go there
        QVERIFY(!r.ok);
    }

    void invalidRequests() {
        const HeroId h = makeHero(p1, {2, 2});
        QVERIFY(moveHero(gs, reg, HeroId(999), {1, 1}).error.contains("unknown hero"));
        QVERIFY(moveHero(gs, reg, h, {50, 50}).error.contains("outside the map"));
        QVERIFY(moveHero(gs, reg, h, {2, 2}).error.contains("already there"));
        QVERIFY(!moveHero(gs, reg, h, {2, 2}).ok);
    }
};

QTEST_APPLESS_MAIN(TstMovement)
#include "tst_movement.moc"
