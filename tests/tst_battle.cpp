#include <QtTest>

#include "TestContent.h"

#include "core/battle/AutoBattle.h"
#include "core/content/ContentRegistry.h"
#include "core/rules/HeroRules.h"
#include "core/rules/Movement.h"

using namespace kot::core;

class TstBattle : public QObject {
    Q_OBJECT
    ContentRegistry reg;
    GameState gs;
    PlayerId p1, p2;

    HeroId makeHero(PlayerId owner, Pos pos, const QString& unit = QString(), int count = 0) {
        const HeroId h = createHero(gs, reg, owner, "H", C("knight"), pos);
        if (count > 0) armyOf(h).addUnits(U(qPrintable(unit)), count, reg.unit(U(qPrintable(unit)))->maxHp);
        return h;
    }
    Army& armyOf(HeroId id) { return *gs.army(gs.hero(id)->army); }
    ArmyId monsters(Pos pos, const QString& unit, int count) {
        const ArmyId a = gs.addArmy(PlayerId(), pos);
        gs.army(a)->addUnits(U(qPrintable(unit)), count, reg.unit(U(qPrintable(unit)))->maxHp);
        return a;
    }

private slots:
    void initTestCase() {
        QStringList errors;
        QVERIFY2(reg.loadFromDirectory(KOT_DATA_DIR, &errors), qPrintable(errors.join("\n")));
        useContent(reg);
        QVERIFY(reg.balance().battle.maxRounds > 0);
    }

    void init() {
        gs = GameState(7);
        gs.setMap(GameMap(12, 12, T("grass")));
        p1 = gs.addPlayer("A", gs.newTeam(), Player::Controller::Human, RC("humans"), "#ff0000");
        p2 = gs.addPlayer("B", gs.newTeam(), Player::Controller::Human, RC("humans"), "#0000ff");
    }

    void strongArmyBeatsMonsters() {
        const HeroId h = makeHero(p1, {0, 0}, "pikeman", 30);
        const ArmyId m = monsters({1, 0}, "militia", 5);
        const BattleReport b = resolveBattle(gs, reg, gs.hero(h)->army, m);
        QVERIFY(b.attackerWon());
        QVERIFY(!gs.army(m));                       // monsters removed
        QCOMPARE(b.defender.losses.value(U("militia")), 5);
        QCOMPARE(b.experience, 5 * 6 * reg.balance().battle.experiencePerHp);
        QCOMPARE(gs.hero(h)->experience, b.experience);
        for (const UnitStack& s : armyOf(h).stacks) QCOMPARE(s.totalHp(), s.count() * s.maxHp());  // healed
    }

    void weakHeroLosesAndDies() {
        const HeroId h = makeHero(p1, {0, 0}, "militia", 2);
        const ArmyId army = gs.hero(h)->army;
        const ArmyId m = monsters({1, 0}, "armoured_eagle", 10);
        const BattleReport b = resolveBattle(gs, reg, army, m);
        QCOMPARE(b.outcome, BattleReport::Outcome::DefenderWon);
        QVERIFY(b.loserHeroKilled);
        QVERIFY(!gs.hero(h));
        QVERIFY(!gs.army(army));
        QVERIFY(gs.army(m));
    }

    void heroTraitsMatter() {
        // Same armies, many seeds: a high-attack hero wins more often.
        int plainWins = 0, strongWins = 0;
        for (int seed = 1; seed <= 20; ++seed) {
            for (int strong = 0; strong < 2; ++strong) {
                init();
                gs = GameState(seed);
                gs.setMap(GameMap(12, 12, T("grass")));
                p1 = gs.addPlayer("A", gs.newTeam(), Player::Controller::Human, RC("humans"), "#ff0000");
                const HeroId h = makeHero(p1, {0, 0}, "pikeman", 12);
                if (strong) gs.hero(h)->traits[TR("attack")] = 20;
                const ArmyId m = monsters({1, 0}, "pikeman", 14);
                if (resolveBattle(gs, reg, gs.hero(h)->army, m).attackerWon()) ++(strong ? strongWins : plainWins);
            }
        }
        QVERIFY2(strongWins > plainWins, qPrintable(QString("%1 vs %2").arg(strongWins).arg(plainWins)));
    }

    void deterministicForSameSeed() {
        QJsonObject first;
        for (int run = 0; run < 2; ++run) {
            init();
            const HeroId h = makeHero(p1, {0, 0}, "pikeman", 15);
            armyOf(h).addUnits(U("archer"), 4, 10);
            const ArmyId m = monsters({1, 0}, "pikeman", 18);
            const QJsonObject j = battleReportToJson(resolveBattle(gs, reg, gs.hero(h)->army, m));
            if (run == 0) first = j;
            else QCOMPARE(j, first);
        }
    }

    void moveAttacksMonstersFromTheTileBefore() {
        const HeroId h = makeHero(p1, {0, 0}, "pikeman", 30);
        const ArmyId m = monsters({3, 0}, "militia", 4);
        const MoveResult r = moveHero(gs, reg, h, {3, 0});
        QVERIFY2(r.ok, qPrintable(r.error));
        QVERIFY(r.attacked);
        QCOMPARE(r.battles.size(), 1);
        QVERIFY(r.battles[0].attackerWon());
        QVERIFY(!gs.army(m));
        QCOMPARE(armyOf(h).pos, (Pos{2, 0}));
        QVERIFY(!r.arrived);
    }

    void tooFarAwayJustWalks() {
        gs.setMap(GameMap(40, 3, T("grass")));
        const HeroId h = makeHero(p1, {0, 0}, "pikeman", 30);
        const ArmyId m = monsters({30, 0}, "militia", 4);
        const MoveResult r = moveHero(gs, reg, h, {30, 0});
        QVERIFY(r.ok);
        QVERIFY(!r.attacked);
        QVERIFY(gs.army(m));
    }

    void attackingEnemyTownFightsGarrisonAndCaptures() {
        const HeroId h = makeHero(p1, {0, 0}, "pikeman", 40);
        const TownId town = gs.addTown("Fort", RC("humans"), {3, 0}, p2);
        gs.army(gs.town(town)->garrison)->addUnits(U("militia"), 5, 6);
        const MoveResult r = moveHero(gs, reg, h, {3, 0});
        QVERIFY2(r.ok, qPrintable(r.error));
        QVERIFY(r.battles.value(0).attackerWon());
        QVERIFY(r.arrived);
        QCOMPARE(r.capturedTown, town);
        QCOMPARE(gs.town(town)->owner, p1);
        const Army* garrison = gs.army(gs.town(town)->garrison);
        QVERIFY(garrison);                     // the town keeps its (now empty) garrison army
        QCOMPARE(garrison->totalUnits(), 0);
        QCOMPARE(garrison->owner, p1);
    }

    void visitingEnemyHeroDefendsTownFirst() {
        const HeroId h = makeHero(p1, {0, 0}, "pikeman", 60);
        const TownId town = gs.addTown("Fort", RC("humans"), {3, 0}, p2);
        gs.army(gs.town(town)->garrison)->addUnits(U("militia"), 3, 6);
        const HeroId defender = makeHero(p2, {3, 0}, "militia", 3);
        const MoveResult r = moveHero(gs, reg, h, {3, 0});
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(r.battles.size(), 2);
        QCOMPARE(r.battles[0].defender.hero, defender);
        QVERIFY(!gs.hero(defender));
        QCOMPARE(gs.town(town)->owner, p1);
    }

    void losingAttackerIsRemoved() {
        const HeroId h = makeHero(p1, {0, 0}, "militia", 1);
        monsters({2, 0}, "armoured_eagle", 10);
        const MoveResult r = moveHero(gs, reg, h, {2, 0});
        QVERIFY(r.ok);
        QVERIFY(r.heroKilled);
        QVERIFY(!gs.hero(h));
    }
};

QTEST_GUILESS_MAIN(TstBattle)
#include "tst_battle.moc"
