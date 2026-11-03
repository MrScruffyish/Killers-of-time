#include <QtTest>

#include "core/content/ContentRegistry.h"
#include "core/rules/HeroRules.h"
#include "core/rules/Movement.h"
#include "core/rules/TownRules.h"
#include "core/rules/TurnCycle.h"
#include "core/rules/Vision.h"

using namespace kot::core;

class TstTurnCycle : public QObject {
    Q_OBJECT
    ContentRegistry reg;
    GameState gs;
    PlayerId p1, p2;

private slots:
    void initTestCase() {
        QStringList errors;
        QVERIFY2(reg.loadFromDirectory(KOT_DATA_DIR, &errors), qPrintable(errors.join("\n")));
    }

    void init() {
        gs = GameState(1);
        gs.setMap(GameMap(20, 20, "grass"));
        p1 = gs.addPlayer("A", gs.newTeam(), Player::Controller::Human, "humans", "#ff0000");
        p2 = gs.addPlayer("B", gs.newTeam(), Player::Controller::Ai, "humans", "#0000ff");
    }

    void weekEventsApply() {
        QVERIFY(!reg.weekEvents().isEmpty());
        gs.player(p1)->resources = {{"gold", 100}};
        gs.addTown("T", "humans", {1, 1}, p1);
        // Draw until a gold event comes up (the draw uses the game's Rng).
        for (int i = 0; i < 200; ++i) {
            startWeekEvent(gs, reg);
            const WeekEventDef* e = reg.weekEvent(gs.weekEvent());
            QVERIFY(e);
            if (e->resources.value("gold") > 0) break;
            gs.player(p1)->resources = {{"gold", 100}};
        }
        const WeekEventDef* e = reg.weekEvent(gs.weekEvent());
        QVERIFY(e && e->resources.value("gold") > 0);
        QCOMPARE(gs.player(p1)->resources.value("gold"), 100 + e->resources.value("gold"));
        // A growth event changes this week's growth.
        for (const WeekEventDef& ev : reg.weekEvents()) {
            if (ev.growthPercent == 0) continue;
            gs.setWeekEvent(ev.id);
            const Town& t = gs.towns().first();
            QCOMPARE(townGrowth(gs, t, *reg.unit("militia"), reg),
                     std::max(1, (reg.unit("militia")->weeklyGrowth * (100 + ev.growthPercent) + 50) / 100));
            break;
        }
        GameState loaded;
        QStringList errors;
        gs.setTurnOrder({p1, p2});
        QVERIFY2(GameState::fromJson(gs.toJson(), reg, &loaded, &errors), qPrintable(errors.join("\n")));
        QCOMPARE(loaded.weekEvent(), gs.weekEvent());
    }

    void incomeFromTownsAndMines() {
        gs.addTown("T1", "humans", {1, 1}, p1);
        gs.addTown("T2", "humans", {3, 3}, p1);
        gs.addMine("gold", 1000, {5, 5}, p1);
        gs.addMine("wood", 2, {6, 6}, p1);
        gs.addMine("stone", 2, {7, 7});  // neutral
        gs.addTown("Other", "humans", {9, 9}, p2);
        const ResourceAmounts income = dailyIncome(gs, reg, p1);
        QCOMPARE(income.value("gold"), 2 * 500 + 1000);
        QCOMPARE(income.value("wood"), 2);
        QCOMPARE(income.value("stone"), 0);
        QCOMPARE(dailyIncome(gs, reg, p2).value("gold"), 500);
    }

    void beginGameGivesFirstPlayerMovementAndVision() {
        const HeroId h = createHero(gs, reg, p1, "H", "knight", {10, 10});
        gs.hero(h)->movementPoints = 0;
        beginGame(gs, reg);
        QCOMPARE(gs.currentPlayer(), p1);
        QCOMPARE(gs.hero(h)->movementPoints, 1100);
        QVERIFY(gs.exploredMap(gs.player(p1)->team)[10 * 20 + 10] == 1);
        QCOMPARE(gs.player(p1)->resources.value("gold"), 0);  // no income on day 1
    }

    void turnsAlternateAndDaysAdvance() {
        // Players without towns and heroes are out of the game and skipped; keep both in.
        createHero(gs, reg, p1, "H1", "knight", {0, 0});
        createHero(gs, reg, p2, "H2", "knight", {19, 19});
        beginGame(gs, reg);
        QCOMPARE(gs.day(), 1);
        TurnInfo t = endTurn(gs, reg);
        QCOMPARE(t.player, p2);
        QVERIFY(!t.newDay);
        QCOMPARE(t.day, 1);
        t = endTurn(gs, reg);
        QCOMPARE(t.player, p1);
        QVERIFY(t.newDay);
        QCOMPARE(t.day, 2);
        QCOMPARE(gs.day(), 2);
    }

    void incomeIsPaidAtStartOfTurnFromDayTwo() {
        gs.addTown("T", "humans", {1, 1}, p1);
        gs.addMine("gold", 1000, {5, 5}, p1);
        // Players without towns and heroes are out of the game and skipped; keep B in.
        createHero(gs, reg, p2, "H2", "knight", {19, 19});
        beginGame(gs, reg);
        endTurn(gs, reg);  // p2, day 1
        QCOMPARE(gs.player(p1)->resources.value("gold"), 0);
        endTurn(gs, reg);  // p1, day 2
        QCOMPARE(gs.player(p1)->resources.value("gold"), 1500);
        endTurn(gs, reg);
        endTurn(gs, reg);  // p1, day 3
        QCOMPARE(gs.player(p1)->resources.value("gold"), 3000);
        QCOMPARE(gs.player(p2)->resources.value("gold"), 0);
    }

    void heroesRecoverAtTheirOwnersTurnStart() {
        const HeroId a = createHero(gs, reg, p1, "A", "knight", {1, 1});
        const HeroId b = createHero(gs, reg, p2, "B", "knight", {8, 8});
        beginGame(gs, reg);
        gs.hero(a)->movementPoints = 0;
        gs.hero(a)->spellPoints = 2;
        gs.hero(b)->movementPoints = 0;
        gs.hero(b)->spellPoints = 2;
        endTurn(gs, reg);  // p2's turn starts: only B recovers
        QCOMPARE(gs.hero(b)->movementPoints, 1100);
        QCOMPARE(gs.hero(b)->spellPoints, 3);  // knight recovers 1 per day
        QCOMPARE(gs.hero(a)->movementPoints, 0);
        QCOMPARE(gs.hero(a)->spellPoints, 2);
    }

    void capturedMineStartsPayingNextDay() {
        const HeroId h = createHero(gs, reg, p1, "H", "knight", {1, 1});
        const MineId mine = gs.addMine("gems", 3, {3, 1});
        // Players without towns and heroes are out of the game and skipped; keep B in.
        createHero(gs, reg, p2, "H2", "knight", {19, 19});
        beginGame(gs, reg);
        QCOMPARE(moveHero(gs, reg, h, {3, 1}).capturedMine, mine);
        endTurn(gs, reg);
        endTurn(gs, reg);  // day 2, p1
        QCOMPARE(gs.player(p1)->resources.value("gems"), 3);
    }

    void emptyGameDoesNothing() {
        GameState empty;
        const TurnInfo t = endTurn(empty, reg);
        QVERIFY(!t.player.isValid());
    }

    void exploredMapSurvivesSaveAndLoad() {
        createHero(gs, reg, p1, "H", "knight", {10, 10});
        beginGame(gs, reg);
        GameState loaded;
        QStringList errors;
        QVERIFY2(GameState::fromJson(gs.toJson(), reg, &loaded, &errors), qPrintable(errors.join("\n")));
        const TeamId team = gs.player(p1)->team;
        QCOMPARE(loaded.exploredMap(team), gs.exploredMap(team));
        QVERIFY(loaded.exploredMap(team)[10 * 20 + 10] == 1);
    }
};

QTEST_APPLESS_MAIN(TstTurnCycle)
#include "tst_turncycle.moc"
