#include <QtTest>

#include "core/content/ContentRegistry.h"
#include "core/rules/HeroRules.h"
#include "core/rules/TurnCycle.h"
#include "core/rules/Victory.h"

using namespace kot::core;

class TstVictory : public QObject {
    Q_OBJECT
    ContentRegistry reg;
    GameState gs;
    PlayerId a, b, c;
    TeamId teamA, teamB;

    void removeHeroes(PlayerId p) {
        for (const Hero& h : QList<Hero>(gs.heroes().values()))
            if (h.owner == p) {
                gs.armies().remove(h.army);
                gs.heroes().remove(h.id);
            }
    }

private slots:
    void initTestCase() {
        QStringList errors;
        QVERIFY2(reg.loadFromDirectory(KOT_DATA_DIR, &errors), qPrintable(errors.join("\n")));
    }

    void init() {
        gs = GameState(1);
        gs.setMap(GameMap(10, 10, "grass"));
        teamA = gs.newTeam();
        teamB = gs.newTeam();
        a = gs.addPlayer("A", teamA, Player::Controller::Human, "humans", "#ff0000");
        b = gs.addPlayer("B", teamB, Player::Controller::Human, "humans", "#0000ff");
        c = gs.addPlayer("C", teamB, Player::Controller::Ai, "humans", "#00ff00");
        gs.setTurnOrder({a, b, c});
        for (PlayerId p : {a, b, c}) createHero(gs, reg, p, "H", "knight", {int(p.value()), 0});
        gs.addTown("TownB", "humans", {5, 5}, b);
        beginGame(gs, reg);
    }

    void playersWithAHeroOrTownAreIn() {
        for (PlayerId p : {a, b, c}) QVERIFY(!isEliminated(gs, p));
        QVERIFY(!gameOutcome(gs).over);
        removeHeroes(b);
        QVERIFY(!isEliminated(gs, b));  // still has a town
        removeHeroes(a);
        QVERIFY(isEliminated(gs, a));
    }

    void lastTeamStandingWins() {
        removeHeroes(c);
        QVERIFY(isEliminated(gs, c));
        QVERIFY(!gameOutcome(gs).over);  // team B still has player B, team A has A
        removeHeroes(a);
        const GameOutcome o = gameOutcome(gs);
        QVERIFY(o.over);
        QCOMPARE(o.winner, teamB);
    }

    void eliminatedPlayersAreSkipped() {
        removeHeroes(b);
        gs.towns().clear();
        QVERIFY(isEliminated(gs, b));
        QCOMPARE(gs.currentPlayer(), a);
        endTurn(gs, reg);
        QCOMPARE(gs.currentPlayer(), c);  // b skipped
        const TurnInfo t = endTurn(gs, reg);
        QCOMPARE(t.player, a);
        QVERIFY(t.newDay);
        QCOMPARE(gs.day(), 2);
    }

    void skippingTheLastPlayersStillStartsANewDay() {
        removeHeroes(c);
        endTurn(gs, reg);  // a -> b
        const TurnInfo t = endTurn(gs, reg);  // b -> (c skipped) -> a
        QCOMPARE(t.player, a);
        QVERIFY(t.newDay);
    }
};

QTEST_GUILESS_MAIN(TstVictory)
#include "tst_victory.moc"
