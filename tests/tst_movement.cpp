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

    void unfinishedMoveRemembersTheDestination() {
        gs.setMap(GameMap(20, 10, "grass"));
        const HeroId h = makeHero(p1, {0, 0});
        QVERIFY(moveHero(gs, reg, h, {15, 0}).ok);
        QCOMPARE(gs.hero(h)->destination, std::optional<Pos>(Pos{15, 0}));
        GameState loaded;
        QStringList errors;
        QVERIFY2(GameState::fromJson(gs.toJson(), reg, &loaded, &errors), qPrintable(errors.join("\n")));
        QCOMPARE(loaded.hero(h)->destination, std::optional<Pos>(Pos{15, 0}));
        refreshHeroMovement(gs, reg, p1);
        QVERIFY(moveHero(gs, reg, h, {15, 0}).arrived);
        QVERIFY(!gs.hero(h)->destination);
    }

    // ---- zone of control and scouting
    void monstersAttackAHeroWhoComesTooClose() {
        gs.setMap(GameMap(20, 10, "grass"));
        const HeroId h = makeHero(p1, {0, 0});
        armyOf(h).addUnits("pikeman", 40, 10);
        const ArmyId m = gs.addArmy(PlayerId(), {8, 2});
        gs.army(m)->addUnits("militia", 2, 6);
        QCOMPARE(zoneOfControlRadius(*gs.hero(h), reg), 2);
        const MoveResult r = moveHero(gs, reg, h, {12, 0});
        QVERIFY2(r.ok, qPrintable(r.error));
        QVERIFY(r.ambushed);
        QVERIFY(r.attacked);
        QCOMPARE(r.battles.size(), 1);
        QCOMPARE(armyOf(h).pos, (Pos{6, 0}));  // stopped two tiles from the monsters
        QVERIFY(!gs.army(m));                   // and beat them
        QCOMPARE(gs.hero(h)->destination, std::optional<Pos>(Pos{12, 0}));
    }

    void walkingAwayFromAnArmyAlreadyInReachIsFree() {
        const HeroId h = makeHero(p1, {1, 1});
        const ArmyId m = gs.addArmy(PlayerId(), {2, 2});
        gs.army(m)->addUnits("militia", 2, 6);
        const MoveResult r = moveHero(gs, reg, h, {6, 1});
        QVERIFY(r.ok);
        QVERIFY(!r.ambushed);
        QVERIFY(r.arrived);
    }

    void scoutingShrinksTheZoneAndAddsVisionAndMovement() {
        gs.setMap(GameMap(20, 10, "grass"));
        const HeroId h = makeHero(p1, {0, 0});
        const int before = armyMovementPoints(armyOf(h), gs.hero(h), reg);
        gs.hero(h)->traits["scouting"] = 2;
        QCOMPARE(zoneOfControlRadius(*gs.hero(h), reg), 1);
        QCOMPARE(armyMovementPoints(armyOf(h), gs.hero(h), reg), before + before * 10 / 100);
        gs.hero(h)->traits["scouting"] = 20;
        QCOMPARE(zoneOfControlRadius(*gs.hero(h), reg), 1);  // never below the minimum
        gs.hero(h)->traits["scouting"] = 2;
        // Two tiles from the monsters is fine now, one is not.
        const ArmyId m = gs.addArmy(PlayerId(), {8, 2});
        gs.army(m)->addUnits("militia", 2, 6);
        MoveResult r = moveHero(gs, reg, h, {6, 0});
        QVERIFY(r.arrived && !r.ambushed);
        r = moveHero(gs, reg, h, {7, 0});
        QVERIFY(!r.ambushed);  // still two away (8,2)-(7,0)
    }

    void armiesInTownsAndAlliesHaveNoZone() {
        const HeroId h = makeHero(p1, {0, 0});
        const HeroId friendHero = makeHero(p1, {3, 2});
        Q_UNUSED(friendHero);
        const MoveResult r = moveHero(gs, reg, h, {4, 0});
        QVERIFY(r.arrived && !r.ambushed);
    }

    void cannotWalkThroughObjects() {
        const HeroId h = makeHero(p1, {0, 0});
        gs.addMine("gold", 1000, {5, 0});
        // Walking past the mine goes around it (4 straight + 2 diagonal = 682).
        const MoveResult r = moveHero(gs, reg, h, {6, 0});
        QVERIFY2(r.ok, qPrintable(r.error));
        QVERIFY(r.arrived);
        QCOMPARE(r.movementSpent, 682);
        QVERIFY(!r.capturedMine.isValid());
    }

    void visitingNeutralMineCapturesIt() {
        const HeroId h = makeHero(p1, {0, 0});
        const MineId mine = gs.addMine("gold", 1000, {3, 0});
        const MoveResult r = moveHero(gs, reg, h, {3, 0});
        QVERIFY2(r.ok, qPrintable(r.error));
        QVERIFY(r.arrived);
        QCOMPARE(r.capturedMine, mine);
        QCOMPARE(gs.mine(mine)->owner, p1);
        QCOMPARE(armyOf(h).pos, (Pos{3, 0}));
    }

    void enemyMineIsTakenOver() {
        const HeroId h = makeHero(p1, {0, 0});
        const MineId mine = gs.addMine("wood", 2, {2, 0}, p2);
        QCOMPARE(moveHero(gs, reg, h, {2, 0}).capturedMine, mine);
        QCOMPARE(gs.mine(mine)->owner, p1);
    }

    void ownAndTeammatesMinesAreJustVisited() {
        const PlayerId ally = gs.addPlayer("C", gs.player(p1)->team, Player::Controller::Ai, "humans", "#00ff00");
        const HeroId h = makeHero(p1, {0, 0});
        const MineId own = gs.addMine("gold", 1000, {2, 0}, p1);
        const MineId friendly = gs.addMine("gold", 1000, {2, 2}, ally);
        QVERIFY(!moveHero(gs, reg, h, {2, 0}).capturedMine.isValid());
        QVERIFY(!moveHero(gs, reg, h, {2, 2}).capturedMine.isValid());
        QCOMPARE(gs.mine(own)->owner, p1);
        QCOMPARE(gs.mine(friendly)->owner, ally);
    }

    void partialMoveDoesNotCapture() {
        gs.setMap(GameMap(30, 3, "grass"));
        const HeroId h = makeHero(p1, {0, 0});
        const MineId mine = gs.addMine("gold", 1000, {20, 0});  // 2000 away, 1100 points
        const MoveResult r = moveHero(gs, reg, h, {20, 0});
        QVERIFY(r.ok);
        QVERIFY(!r.arrived);
        QVERIFY(!gs.mine(mine)->owner.isValid());
    }

    void emptyNeutralTownIsCaptured() {
        const HeroId h = makeHero(p1, {0, 0});
        const TownId town = gs.addTown("Hamlet", "humans", {2, 0});
        const MoveResult r = moveHero(gs, reg, h, {2, 0});
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(r.capturedTown, town);
        QCOMPARE(gs.town(town)->owner, p1);
        QCOMPARE(gs.army(gs.town(town)->garrison)->owner, p1);
    }

    void defendedEnemyTownNeedsUnitsToAttack() {
        const HeroId h = makeHero(p1, {0, 0});  // no units
        const TownId town = gs.addTown("Fort", "humans", {2, 0}, p2);
        gs.army(gs.town(town)->garrison)->addUnits("pikeman", 5, 10);
        const MoveResult r = moveHero(gs, reg, h, {2, 0});
        QVERIFY(!r.ok);
        QVERIFY(r.error.contains("no units"));
        QCOMPARE(gs.town(town)->owner, p2);
        QCOMPARE(armyOf(h).pos, (Pos{0, 0}));  // did not move
    }

    void ownTownWithGarrisonCanBeEntered() {
        const HeroId h = makeHero(p1, {0, 0});
        const TownId town = gs.addTown("Home", "humans", {2, 0}, p1);
        gs.army(gs.town(town)->garrison)->addUnits("pikeman", 5, 10);
        const MoveResult r = moveHero(gs, reg, h, {2, 0});
        QVERIFY2(r.ok, qPrintable(r.error));
        QVERIFY(r.arrived);
        QVERIFY(!r.capturedTown.isValid());
    }

    void onlyOneHeroPerTown() {
        const TownId town = gs.addTown("Home", "humans", {2, 1}, p1);
        Q_UNUSED(town);
        makeHero(p1, {2, 1});                 // already in the town
        const HeroId second = makeHero(p1, {0, 1});
        const MoveResult r = moveHero(gs, reg, second, {2, 1});
        QVERIFY(!r.ok);
        QVERIFY(r.error.contains("occupied"));
        QCOMPARE(armyOf(second).pos, (Pos{0, 1}));
    }

    void movingRevealsMap() {
        gs.setMap(GameMap(30, 10, "grass"));
        const HeroId h = makeHero(p1, {2, 5});
        const TeamId team = gs.player(p1)->team;
        QVERIFY(gs.exploredMap(team).isEmpty() || gs.exploredMap(team)[0] == 0);
        moveHero(gs, reg, h, {9, 5});
        QVERIFY(gs.exploredMap(team)[5 * 30 + 14] == 1);   // 5 tiles ahead of the new position
        QVERIFY(gs.exploredMap(team)[5 * 30 + 25] == 0);   // far away still unexplored
    }

    void friendlyArmiesBlock() {
        const HeroId h = makeHero(p1, {0, 0});
        const HeroId friendHero = makeHero(p1, {4, 0});
        MoveResult r = moveHero(gs, reg, h, {4, 0});
        QVERIFY(!r.ok);
        QVERIFY(r.error.contains("occupied"));
        Q_UNUSED(friendHero);
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
