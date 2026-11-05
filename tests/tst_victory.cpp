#include <QtTest>

#include "TestContent.h"

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
        useContent(reg);
    }

    void init() {
        gs = GameState(1);
        gs.setMap(GameMap(10, 10, T("grass")));
        teamA = gs.newTeam();
        teamB = gs.newTeam();
        a = gs.addPlayer("A", teamA, Player::Controller::Human, RC("humans"), "#ff0000");
        b = gs.addPlayer("B", teamB, Player::Controller::Human, RC("humans"), "#0000ff");
        c = gs.addPlayer("C", teamB, Player::Controller::Ai, RC("humans"), "#00ff00");
        gs.setTurnOrder({a, b, c});
        for (PlayerId p : {a, b, c}) createHero(gs, reg, p, "H", C("knight"), {int(p.value()), 0});
        gs.addTown("TownB", RC("humans"), {5, 5}, b);
        beginGame(gs, reg);
    }

    void playersWithAHeroOrTownAreIn() {
        for (PlayerId p : {a, b, c}) QVERIFY(!isEliminated(gs, p));
        QVERIFY(!gameOutcome(gs, reg).over);
        removeHeroes(b);
        QVERIFY(!isEliminated(gs, b));  // still has a town
        removeHeroes(a);
        QVERIFY(isEliminated(gs, a));
    }

    void sevenDaysWithoutATownAndYouAreOut() {
        // B loses his only town (to A) during his turn; his hero is still out there.
        endTurn(gs, reg);  // A -> B
        const int lostOn = gs.day();
        gs.town(gs.towns().first().id)->owner = a;
        endTurn(gs, reg);  // B -> C: noted
        QCOMPARE(gs.player(b)->townlessSince, lostOn);
        QVERIFY(!isEliminated(gs, b));
        QCOMPARE(townlessDaysLeft(gs, b), kTownlessGraceDays);
        // Saved and loaded.
        GameState loaded;
        QStringList errors;
        QVERIFY2(GameState::fromJson(gs.toJson(reg), reg, &loaded, &errors), qPrintable(errors.join("\n")));
        QCOMPARE(loaded.player(b)->townlessSince, lostOn);
        // Taking a town back stops the clock.
        gs.town(gs.towns().first().id)->owner = b;
        endTurn(gs, reg);
        QCOMPARE(gs.player(b)->townlessSince, 0);
        QCOMPARE(townlessDaysLeft(gs, b), -1);
        // Lost again: after 7 days he is out, and his hero leaves the map.
        gs.town(gs.towns().first().id)->owner = a;
        endTurn(gs, reg);
        const int since = gs.player(b)->townlessSince;
        QVERIFY(since > 0);
        while (gs.day() < since + kTownlessGraceDays - 1) endTurn(gs, reg);
        QVERIFY(!isEliminated(gs, b));
        QCOMPARE(townlessDaysLeft(gs, b), 1);
        while (gs.day() < since + kTownlessGraceDays) endTurn(gs, reg);
        QVERIFY(isEliminated(gs, b));
        for (const Hero& h : gs.heroes()) QVERIFY(h.owner != b);
        QVERIFY(gs.currentPlayer() != b);  // his turns are skipped
    }

    void defeatRules() {
        // "town": out as soon as the last town is gone, heroes and all.
        gs.setDefeatRule("town");
        const TownId townB = gs.towns().first().id;
        gs.town(townB)->owner = a;
        QVERIFY(isEliminated(gs, b));
        QCOMPARE(townlessDaysLeft(gs, b), -1);
        endTurn(gs, reg);
        for (const Hero& h : gs.heroes()) QVERIFY(h.owner != b);
        // "all": a hero without a town keeps the player in, for good.
        init();
        gs.setDefeatRule("all");
        gs.town(gs.towns().first().id)->owner = a;
        for (int i = 0; i < 3 * 10; ++i) endTurn(gs, reg);
        QVERIFY(!isEliminated(gs, b));
        QCOMPARE(townlessDaysLeft(gs, b), -1);
        // Saved and loaded.
        GameState loaded;
        QStringList errors;
        QVERIFY2(GameState::fromJson(gs.toJson(reg), reg, &loaded, &errors), qPrintable(errors.join("\n")));
        QCOMPARE(loaded.defeatRule(), QString("all"));
        QVERIFY(describeDefeat(loaded).contains("no heroes"));
    }

    void lastTeamStandingWins() {
        removeHeroes(c);
        QVERIFY(isEliminated(gs, c));
        QVERIFY(!gameOutcome(gs, reg).over);  // team B still has player B, team A has A
        removeHeroes(a);
        const GameOutcome o = gameOutcome(gs, reg);
        QVERIFY(o.over);
        QCOMPARE(o.winner, teamB);
    }

    void otherVictoryConditions() {
        // Take a town.
        VictoryCondition v;
        v.type = VictoryCondition::Type::HoldTown;
        const TownId target = gs.addTown("Prize", RC("humans"), {8, 8});
        v.town = target;
        gs.setVictory(v);
        QVERIFY(!gameOutcome(gs, reg).over);
        gs.town(target)->owner = a;
        QCOMPARE(gameOutcome(gs, reg).winner, teamA);
        QVERIFY(describeVictory(gs).contains("Prize"));
        // Saved and loaded.
        GameState loaded;
        QStringList errors;
        QVERIFY2(GameState::fromJson(gs.toJson(reg), reg, &loaded, &errors), qPrintable(errors.join("\n")));
        QCOMPARE(loaded.victory().town, target);
        // Gold.
        gs.town(target)->owner = PlayerId();
        v = VictoryCondition();
        v.type = VictoryCondition::Type::Gold;
        v.amount = 50000;
        gs.setVictory(v);
        QVERIFY(!gameOutcome(gs, reg).over);
        gs.player(c)->resources[R("gold")] = 60000;
        QCOMPARE(gameOutcome(gs, reg).winner, teamB);
        // Days: most towns when time is up.
        gs.player(c)->resources[R("gold")] = 0;
        v = VictoryCondition();
        v.type = VictoryCondition::Type::Days;
        v.amount = 3;
        gs.setVictory(v);
        gs.setDay(3);
        QVERIFY(!gameOutcome(gs, reg).over);
        gs.setDay(4);
        QVERIFY(gameOutcome(gs, reg).over);
        QCOMPARE(gameOutcome(gs, reg).winner, teamB);  // B has a town, A none
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
