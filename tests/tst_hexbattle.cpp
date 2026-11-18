#include <QtTest>

#include "TestContent.h"

#include "core/battle/AutoBattle.h"
#include "core/battle/Battle.h"
#include "core/battle/BattleAi.h"
#include "core/content/ContentRegistry.h"
#include "core/rules/HeroRules.h"
#include "core/rules/Trade.h"
#include "core/rules/Vision.h"

using namespace kot::core;

class TstHexBattle : public QObject {
    Q_OBJECT
    ContentRegistry reg;
    GameState gs;
    PlayerId p1;
    HeroId hero;

    ArmyId mine() { return gs.hero(hero)->army; }
    ArmyId monsters(const QString& unit, int count) {
        const ArmyId a = gs.addArmy(PlayerId(), {1, 0});
        gs.army(a)->addUnits(U(qPrintable(unit)), count, reg.unit(U(qPrintable(unit)))->maxHp);
        return a;
    }
    void add(const QString& unit, int count) { gs.army(mine())->addUnits(U(qPrintable(unit)), count, reg.unit(U(qPrintable(unit)))->maxHp); }
    static BattleAction move(Pos h) { BattleAction a; a.type = BattleAction::Type::Move; a.hex = h; a.hasHex = true; return a; }
    static BattleAction attack(int target) { BattleAction a; a.type = BattleAction::Type::Attack; a.target = target; return a; }
    static BattleAction shoot(int target) { BattleAction a; a.type = BattleAction::Type::Shoot; a.target = target; return a; }
    static BattleAction defend() { return BattleAction{}; }

private slots:
    void initTestCase() {
        QStringList errors;
        QVERIFY2(reg.loadFromDirectory(KOT_DATA_DIR, &errors), qPrintable(errors.join("\n")));
        useContent(reg);
    }
    void init() {
        gs = GameState(3);
        gs.setMap(GameMap(8, 8, T("grass")));
        p1 = gs.addPlayer("A", gs.newTeam(), Player::Controller::Human, RC("humans"), "#ff0000");
        hero = createHero(gs, reg, p1, "H", C("knight"), {0, 0});
    }

    void areaSpellsHitTheNeighboursToo() {
        add("militia", 10);
        // Three monster stacks side by side.
        const ArmyId m = gs.addArmy(PlayerId(), {1, 0});
        for (const char* u : {"militia", "pikeman", "archer"}) gs.army(m)->addUnits(U(u), 10, reg.unit(U(u))->maxHp);
        gs.hero(hero)->spells << S("frost_ring");
        gs.hero(hero)->spellPoints = 50;
        gs.hero(hero)->traits[TR("magic")] = 5;
        Battle b(gs, reg, mine(), m, 1);
        // Move the defenders together: put them next to each other for the test.
        QList<int> ids;
        for (const BattleUnit& u : b.units()) if (u.side == 1) ids << u.id;
        QCOMPARE(ids.size(), 3);
        BattleAction cast;
        cast.type = BattleAction::Type::Cast;
        cast.spell = S("frost_ring");
        cast.target = ids[0];
        QList<BattleEvent> ev;
        QVERIFY2(b.act(cast, &ev).isEmpty(), "cast failed");
        int hit = 0;
        for (const BattleEvent& e : ev) hit += e.type == BattleEvent::Type::Cast && e.damage > 0;
        // The target always; neighbours within one hex too (the start column is 2 rows apart, so only the target here).
        QVERIFY(hit >= 1);
        int near = 0;
        for (const BattleUnit& u : b.units())
            if (u.side == 1 && u.id != ids[0] && hex::distance(u.pos, b.unit(ids[0])->pos) <= 1) ++near;
        QCOMPARE(hit, 1 + near);
    }

    void defendersWithAHeroCanRetreatButNotInTown() {
        add("militia", 10);
        const PlayerId p2 = gs.addPlayer("B", gs.newTeam(), Player::Controller::Human, RC("humans"), "#0000ff");
        const HeroId enemy = createHero(gs, reg, p2, "E", C("knight"), {1, 0});
        gs.army(gs.hero(enemy)->army)->addUnits(U("pikeman"), 5, 10);
        {
            Battle b(gs, reg, mine(), gs.hero(enemy)->army, 1);
            QVERIFY(!b.whyNoRetreat(1).isEmpty());  // no town to flee to: no way out
            QVERIFY(!b.whyNoSurrender(1).isEmpty());
        }
        gs.addTown("Refuge", RC("humans"), {6, 6}, p2);
        {
            Battle b(gs, reg, mine(), gs.hero(enemy)->army, 1);
            QVERIFY(b.whyNoRetreat(1).isEmpty());
            QVERIFY(b.whyNoSurrender(1).isEmpty());
            QVERIFY(b.surrenderCost(1) > 0);
        }
        const TownId town = gs.addTown("T", RC("humans"), {1, 0}, p2);
        Q_UNUSED(town);
        Battle b(gs, reg, mine(), gs.hero(enemy)->army, 1);
        QVERIFY(!b.whyNoRetreat(1).isEmpty());  // in its town it stands
    }

    void surrenderPaysTheWinnerAndKeepsTheArmy() {
        add("militia", 10);
        const PlayerId p2 = gs.addPlayer("B", gs.newTeam(), Player::Controller::Human, RC("humans"), "#0000ff");
        const HeroId enemy = createHero(gs, reg, p2, "E", C("knight"), {1, 0});
        gs.army(gs.hero(enemy)->army)->addUnits(U("pikeman"), 50, 10);
        gs.player(p1)->resources[R("gold")] = 1000;
        gs.player(p2)->resources[R("gold")] = 0;
        gs.addTown("Home", RC("humans"), {6, 6}, p1);
        Battle b(gs, reg, mine(), gs.hero(enemy)->army, 1);
        QVERIFY(b.whyNoSurrender(0).isEmpty());
        const int cost = b.surrenderCost(0);
        // Let the attacker surrender on its first turn.
        while (!b.isOver() && b.activeUnit()->side != 0) b.act(chooseBattleAction(b));
        BattleAction s;
        s.type = BattleAction::Type::Surrender;
        QVERIFY(b.act(s).isEmpty());
        QVERIFY(b.isOver());
        const BattleReport r = applyBattleResult(gs, reg, b);
        QVERIFY(r.surrendered);
        QVERIFY(gs.hero(hero));
        QVERIFY(gs.army(mine())->totalUnits() > 0);
        QCOMPARE(gs.player(p2)->resources.value(R("gold")), std::min(cost, 1000));
        QCOMPARE(gs.player(p1)->resources.value(R("gold")), 1000 - std::min(cost, 1000));
        // He leaves for his town with the army (cannot be caught again on the spot).
        QCOMPARE(r.retreatedTo, QString("Home"));
        QCOMPARE(gs.army(mine())->pos, (Pos{6, 6}));
    }

    void beatenComputerSidesGiveUp() {
        add("militia", 2);
        const PlayerId p2 = gs.addPlayer("B", gs.newTeam(), Player::Controller::Ai, RC("humans"), "#0000ff");
        const HeroId enemy = createHero(gs, reg, p2, "E", C("knight"), {1, 0});
        gs.army(gs.hero(enemy)->army)->addUnits(U("pikeman"), 50, 10);
        BattleAction a;
        {
            Battle cornered(gs, reg, mine(), gs.hero(enemy)->army, 1);
            QVERIFY(!chooseGiveUp(cornered, 0, 100000, &a));  // no town to flee to: fights
        }
        gs.addTown("Home", RC("humans"), {6, 6}, p1);
        Battle b(gs, reg, mine(), gs.hero(enemy)->army, 1);
        QVERIFY(!chooseGiveUp(b, 1, 0, &a));  // the strong side fights on
        QVERIFY(chooseGiveUp(b, 0, 0, &a));   // no gold: retreat
        QCOMPARE(a.type, BattleAction::Type::Retreat);
        QVERIFY(chooseGiveUp(b, 0, b.surrenderCost(0), &a));  // gold enough: surrender
        QCOMPARE(a.type, BattleAction::Type::Surrender);
        // Even sides fight.
        add("pikeman", 50);
        Battle even(gs, reg, mine(), gs.hero(enemy)->army, 1);
        QVERIFY(!chooseGiveUp(even, 0, 100000, &a));
    }

    void monstersDoNotTakeSurrender() {
        add("militia", 10);
        gs.addTown("Home", RC("humans"), {6, 6}, p1);
        Battle b(gs, reg, mine(), monsters("pikeman", 5), 1);
        QVERIFY(!b.whyNoSurrender(0).isEmpty());
        QVERIFY(b.whyNoRetreat(0).isEmpty());
    }

    void hexGeometry() {
        QCOMPARE(hex::distance({0, 0}, {0, 0}), 0);
        QCOMPARE(hex::distance({0, 0}, {14, 0}), 14);
        QCOMPARE(hex::neighbours({5, 5}).size(), 6);
        QCOMPARE(hex::neighbours({0, 0}).size(), 2);
        for (const Pos n : hex::neighbours({4, 3})) QCOMPARE(hex::distance({4, 3}, n), 1);
        for (const Pos n : hex::neighbours({4, 4})) QCOMPARE(hex::distance({4, 4}, n), 1);
        QVERIFY(hex::adjacent({4, 4}, {3, 5}));   // even row: down-left
        QVERIFY(hex::adjacent({4, 3}, {5, 4}));   // odd row: down-right
        QVERIFY(!hex::adjacent({4, 4}, {5, 5}));
    }

    void startPositionsAndOrder() {
        add("militia", 10);
        add("armoured_eagle", 2);
        const ArmyId m = monsters("pikeman", 5);
        Battle b(gs, reg, mine(), m, 1);
        QCOMPARE(b.units().size(), 3);
        for (const BattleUnit& u : b.units()) QCOMPARE(u.pos.x, u.side == 0 ? 0 : hex::kWidth - 1);
        QCOMPARE(b.activeUnit()->def->id, U("armoured_eagle"));  // initiative 12
        QCOMPARE(b.round(), 1);
    }

    void movingWithinBattleSpeed() {
        add("militia", 10);  // battle speed 4
        Battle b(gs, reg, mine(), monsters("battle_snail", 1), 1);
        const BattleUnit* me = b.activeUnit();
        QCOMPARE(me->side, 0);
        QVERIFY(!b.act(move({6, me->pos.y})).isEmpty());   // too far
        QVERIFY(b.act(move({4, me->pos.y})).isEmpty());
        QCOMPARE(b.units()[0].pos, (Pos{4, 5}));
        QCOMPARE(b.activeUnit()->side, 1);                  // the snail's turn
    }

    void meleeGetsOneRetaliationPerRound() {
        add("pikeman", 10);
        gs.army(mine())->stacks.append(UnitStack(U("pikeman"), 10, 10));  // a second, separate stack
        Battle b(gs, reg, mine(), monsters("battle_snail", 20), 1);
        // Walk both pikemen next to the snail over a few rounds.
        while (!b.isOver() && b.round() < 6) {
            const BattleUnit* me = b.activeUnit();
            if (me->side == 1) { b.act(defend()); continue; }
            QList<BattleEvent> ev;
            const BattleUnit* snail = nullptr;
            for (const BattleUnit& u : b.units()) if (u.side == 1) snail = &u;
            Pos from;
            if (b.attackHex(*me, *snail, &from)) {
                const int retaliatedBefore = snail->retaliated;
                QVERIFY(b.act(attack(snail->id), &ev).isEmpty());
                int retaliations = 0;
                for (const BattleEvent& e : ev) retaliations += e.type == BattleEvent::Type::Retaliate;
                QCOMPARE(retaliations, retaliatedBefore ? 0 : 1);
            } else {
                b.act(chooseBattleAction(b));
            }
        }
    }

    void shootingNeedsTheTargetInRange() {
        add("archer", 10);  // range 5; the armies start 14 hexes apart
        {
            Battle b(gs, reg, mine(), monsters("pikeman", 50), 1);
            const BattleUnit* archer = b.activeUnit();
            QCOMPARE(b.shootRange(*archer), 5);
            QVERIFY(b.canShoot(*archer));
            QVERIFY(!b.canShootAt(*archer, *b.unit(2)));
            QVERIFY(b.act(shoot(2)).contains("out of range"));
            // The battle AI walks toward range instead of shooting at nothing.
            const BattleAction a = chooseBattleAction(b);
            QCOMPARE(a.type, BattleAction::Type::Move);
            QVERIFY(hex::distance(a.hex, b.unit(2)->pos) < hex::distance(archer->pos, b.unit(2)->pos));
        }
        // The hero's Attack Range trait adds to it.
        gs.hero(hero)->traits[TR("attack_range")] = 9;
        Battle b(gs, reg, mine(), monsters("pikeman", 50), 1);
        QCOMPARE(b.shootRange(*b.activeUnit()), 14);
        QVERIFY(b.canShootAt(*b.activeUnit(), *b.unit(2)));
        gs.hero(hero)->traits[TR("attack_range")] = 0;
    }

    void archersShootWithoutRetaliationUntilEnemyIsNext() {
        add("archer", 10);
        gs.hero(hero)->traits[TR("attack_range")] = 10;  // reach across the field
        Battle b(gs, reg, mine(), monsters("pikeman", 50), 1);
        const BattleUnit* archer = b.activeUnit();
        const int shots = archer->shotsLeft;
        QVERIFY(b.canShoot(*archer));
        QList<BattleEvent> ev;
        QVERIFY(b.act(shoot(2), &ev).isEmpty());
        QCOMPARE(b.unit(1)->shotsLeft, shots - 1);
        for (const BattleEvent& e : ev) QVERIFY(e.type != BattleEvent::Type::Retaliate);
        QVERIFY(b.unit(2)->stack.count() < 50);
        // Now the pikemen act: they have no ranged attack.
        QCOMPARE(b.activeUnit()->id, 2);
        QVERIFY(!b.act(shoot(1)).isEmpty());
    }

    void defendingForeverIsADraw() {
        add("militia", 5);
        Battle b(gs, reg, mine(), monsters("militia", 5), 1);
        while (!b.isOver()) QVERIFY(b.act(defend()).isEmpty());
        QCOMPARE(b.outcome(), Battle::Outcome::Draw);
        QCOMPARE(b.round(), reg.balance().battle.maxRounds);
        QVERIFY(!b.act(defend()).isEmpty());
    }

    void aiPlaysToTheEndAndIsDeterministic() {
        add("pikeman", 20);
        add("archer", 6);
        QJsonObject first;
        for (int run = 0; run < 2; ++run) {
            const ArmyId m = monsters("militia", 25);
            Battle b(gs, reg, mine(), m, 42);
            QList<BattleEvent> all;
            while (!b.isOver()) QVERIFY(b.act(chooseBattleAction(b), &all).isEmpty());
            QVERIFY(b.outcome() != Battle::Outcome::Running);
            QJsonObject j = battleToJson(b);
            j.insert("events", int(all.size()));
            if (run == 0) first = j;
            else QCOMPARE(j, first);
            gs.armies().remove(m);
        }
    }

    void applyingTheResultChangesTheArmies() {
        add("pikeman", 30);
        const ArmyId m = monsters("militia", 4);
        Battle b(gs, reg, mine(), m, 5);
        while (!b.isOver()) b.act(chooseBattleAction(b));
        QCOMPARE(b.outcome(), Battle::Outcome::AttackerWon);
        const BattleReport r = applyBattleResult(gs, reg, b);
        QVERIFY(r.attackerWon());
        QVERIFY(!gs.army(m));
        QCOMPARE(r.defender.losses.value(U("militia")), 4);
        QCOMPARE(gs.hero(hero)->experience, r.experience);
    }

    // ---- depth: abilities, hero traits, wait, retreat, rocks
    void rocksBlockWalkers() {
        add("militia", 5);
        Battle b(gs, reg, mine(), monsters("militia", 5), 1);
        QCOMPARE(b.obstacles().size(), reg.balance().battle.obstacles);
        for (const Pos p : b.obstacles()) {
            QVERIFY(p.x >= 3 && p.x <= hex::kWidth - 4);
            QVERIFY(b.blocked(p));
            for (const BattleUnit& u : b.units()) QVERIFY(!b.reachable(u).contains(p));
        }
    }

    void flyersPassOverStacks() {
        add("armoured_eagle", 2);  // flying, speed 12
        gs.army(mine())->stacks.append(UnitStack(U("militia"), 1, 6));
        const ArmyId m = monsters("militia", 3);
        Battle b(gs, reg, mine(), m, 1);
        const BattleUnit* eagle = b.activeUnit();
        QVERIFY(eagle->def->has(Ability::Flying));
        // Every free hex within its speed is reachable, stacks or rocks in between or not.
        int free = 0;
        for (int y = 0; y < hex::kHeight; ++y)
            for (int x = 0; x < hex::kWidth; ++x)
                free += (!b.blocked({x, y}) && hex::distance(eagle->pos, {x, y}) <= 12 && Pos{x, y} != eagle->pos);
        QCOMPARE(b.reachable(*eagle).size() - 1, free);
    }

    void noRetaliationAgainstWraiths() {
        gs.army(mine())->addUnits(U("wraith"), 5, 18);
        Battle b(gs, reg, mine(), monsters("pikeman", 5), 1);
        // Over the battle the wraith attacks, and nothing ever strikes back at it.
        int attacks = 0;
        while (!b.isOver()) {
            QList<BattleEvent> ev;
            b.act(chooseBattleAction(b), &ev);
            for (const BattleEvent& e : ev) {
                attacks += (e.type == BattleEvent::Type::Attack && e.unit == 1);
                QVERIFY(!(e.type == BattleEvent::Type::Retaliate && e.target == 1));
            }
        }
        QVERIFY(attacks > 0);
    }

    void marksmenShootTwice() {
        add("elf_marksman", 4);
        gs.hero(hero)->traits[TR("attack_range")] = 10;
        Battle b(gs, reg, mine(), monsters("treant", 3), 1);
        const int shots = b.activeUnit()->shotsLeft;
        QList<BattleEvent> ev;
        QVERIFY(b.act(shoot(2), &ev).isEmpty());
        int shotsFired = 0;
        for (const BattleEvent& e : ev) shotsFired += e.type == BattleEvent::Type::Shoot;
        QCOMPARE(shotsFired, 2);
        QCOMPARE(b.unit(1)->shotsLeft, shots - 2);
    }

    void goodMoraleGivesAnExtraAction() {
        gs.hero(hero)->traits[TR("morale")] = 20;  // 100%
        add("militia", 5);
        Battle b(gs, reg, mine(), monsters("battle_snail", 1), 1);
        QList<BattleEvent> ev;
        QVERIFY(b.act(move({2, 5}), &ev).isEmpty());
        QCOMPARE(ev.last().type, BattleEvent::Type::Morale);
        QCOMPARE(b.activeUnit()->id, 1);  // again
        QVERIFY(b.act(move({3, 5}), &ev).isEmpty());
        QCOMPARE(b.activeUnit()->side, 1);  // only once per round
    }

    void badMoraleMakesStacksFalterButNotUndead() {
        gs.hero(hero)->traits[TR("morale")] = -20;
        add("militia", 5);
        gs.army(mine())->stacks.append(UnitStack(U("skeleton"), 5, 6));
        Battle b(gs, reg, mine(), monsters("battle_snail", 1), 1);
        // The militia always falters; the skeletons (undead) act.
        QCOMPARE(b.activeUnit()->def->id, U("skeleton"));
    }

    void luckDoublesDamage() {
        gs.hero(hero)->traits[TR("luck")] = 20;
        add("militia", 10);
        Battle b(gs, reg, mine(), monsters("battle_snail", 50), 1);
        while (!b.isOver() && b.round() < 4) {
            QList<BattleEvent> ev;
            b.act(chooseBattleAction(b), &ev);
            for (const BattleEvent& e : ev)
                if (e.type == BattleEvent::Type::Attack && b.unit(e.unit)->side == 0) QVERIFY(e.lucky);
        }
    }

    void hitPointsTraitRaisesMaxHpInBattleOnly() {
        gs.hero(hero)->traits[TR("hit_points")] = 4;  // +20%
        add("pikeman", 10);
        const ArmyId m = monsters("militia", 2);
        Battle b(gs, reg, mine(), m, 1);
        QCOMPARE(b.unit(1)->stack.maxHp(), 12);
        QCOMPARE(b.unit(1)->stack.count(), 10);
        while (!b.isOver()) b.act(chooseBattleAction(b));
        applyBattleResult(gs, reg, b);
        QCOMPARE(gs.army(mine())->stacks[0].maxHp(), 10);
    }

    void waitingActsLater() {
        add("militia", 5);
        Battle b(gs, reg, mine(), monsters("pikeman", 5), 1);
        BattleAction wait;
        wait.type = BattleAction::Type::Wait;
        QCOMPARE(b.activeUnit()->id, 1);
        QVERIFY(b.act(wait).isEmpty());
        QCOMPARE(b.activeUnit()->id, 2);
        QVERIFY(b.act(defend()).isEmpty());
        QCOMPARE(b.activeUnit()->id, 1);  // its turn comes back
        QVERIFY(!b.act(wait).isEmpty());  // only once per round
        QCOMPARE(b.round(), 1);
    }

    void attackerCanRetreat() {
        add("militia", 5);
        const ArmyId m = monsters("bone_dragon", 3);
        BattleAction retreat;
        retreat.type = BattleAction::Type::Retreat;
        {
            Battle cornered(gs, reg, mine(), m, 1);
            QVERIFY(!cornered.whyNoRetreat(0).isEmpty());  // no town of his: he has to fight
        }
        gs.addTown("Home", RC("humans"), {6, 6}, p1);
        Battle b(gs, reg, mine(), m, 1);
        while (b.activeUnit()->side != 0) b.act(defend());
        QVERIFY(b.act(retreat).isEmpty());
        QCOMPARE(b.outcome(), Battle::Outcome::DefenderWon);
        const BattleReport r = applyBattleResult(gs, reg, b);
        QVERIFY(r.retreated);
        QVERIFY(!r.loserHeroKilled);
        QVERIFY(gs.hero(hero));
        QVERIFY(gs.army(mine())->stacks.isEmpty());
        QCOMPARE(r.retreatedTo, QString("Home"));
    }

    void aRetreatingHeroFleesToTheNearestTown() {
        add("militia", 5);
        gs.addTown("Far", RC("humans"), {14, 14}, p1);
        gs.addTown("Near", RC("humans"), {5, 3}, p1);
        const TownId taken = gs.addTown("Nearest", RC("humans"), {2, 2}, p1);
        createHero(gs, reg, p1, "Other", C("knight"), gs.town(taken)->pos);  // a hero already stands there
        Battle b(gs, reg, mine(), monsters("bone_dragon", 3), 1);
        BattleAction retreat;
        retreat.type = BattleAction::Type::Retreat;
        while (b.activeUnit()->side != 0) b.act(defend());
        QVERIFY(b.act(retreat).isEmpty());
        const BattleReport r = applyBattleResult(gs, reg, b);
        QCOMPARE(r.retreatedTo, QString("Near"));
        QCOMPARE(gs.army(mine())->pos, (Pos{5, 3}));
        QCOMPARE(gs.hero(hero)->movementPoints, 0);
    }

    void raceAbilitiesAfterABattle() {
        // Undead raise skeletons, orcs take gold: both from the enemy HP killed.
        for (const char* race : {"undead", "orcs", "humans"}) {
            init();
            gs.player(p1)->race = RC(race);
            add("bone_dragon", 4);
            const ArmyId m = monsters("militia", 30);
            const int gold = gs.player(p1)->resources.value(R("gold"));
            const BattleReport r = resolveBattle(gs, reg, mine(), m);
            QVERIFY(r.attackerWon());
            const int killedHp = 30 * reg.unit(U("militia"))->maxHp;
            if (QLatin1String(race) == QLatin1String("undead")) {
                QCOMPARE(r.raisedUnit, U("skeleton"));
                QCOMPARE(r.raisedCount, killedHp * 15 / 100 / reg.unit(U("skeleton"))->maxHp);
                bool has = false;
                for (const UnitStack& st : gs.army(mine())->stacks) has = has || st.unitTypeId() == U("skeleton");
                QVERIFY(has);
            } else if (QLatin1String(race) == QLatin1String("orcs")) {
                QCOMPARE(r.lootGold, killedHp * 25 / 100);
                QCOMPARE(gs.player(p1)->resources.value(R("gold")), gold + r.lootGold);
            } else {
                QCOMPARE(r.raisedCount, 0);
                QCOMPARE(r.lootGold, 0);
            }
        }
    }

    void raceAbilitiesEverywhereElse() {
        // Humans trade better, sylvan heroes see farther, dwarven walls hold better.
        gs.player(p1)->race = RC("orcs");
        const int plain = tradeEfficiency(gs, reg, p1);
        gs.player(p1)->race = RC("humans");
        QCOMPARE(tradeEfficiency(gs, reg, p1), plain + 10);
        Hero elf;
        elf.classId = C("ranger");
        Hero knight;
        knight.classId = C("knight");
        if (reg.heroClass(C("ranger")) && reg.heroClass(C("ranger"))->race == RC("sylvan"))
            QCOMPARE(heroVisionRadius(elf, reg), heroVisionRadius(knight, reg) + reg.race(RC("sylvan")).visionBonus);
        const TownId t = gs.addTown("Hold", RC("dwarves"), {2, 2}, p1);
        gs.town(t)->buildings << B("village_hall") << B("fort");
        gs.army(gs.town(t)->garrison)->addUnits(U("pikeman"), 5, 10);
        add("militia", 5);
        gs.player(p1)->race = RC("humans");
        const int human = Battle(gs, reg, mine(), gs.town(t)->garrison, 1).townDefense();
        gs.player(p1)->race = RC("dwarves");
        QCOMPARE(Battle(gs, reg, mine(), gs.town(t)->garrison, 1).townDefense(), human + 4);
    }

    void townWallsHelpTheDefender() {
        add("militia", 5);
        const TownId t = gs.addTown("Fort", RC("humans"), {2, 2}, PlayerId());
        Army& garrison = *gs.army(gs.town(t)->garrison);
        garrison.addUnits(U("pikeman"), 5, 10);
        Battle open(gs, reg, mine(), gs.town(t)->garrison, 1);
        QCOMPARE(open.townDefense(), 0);
        gs.town(t)->buildings << B("village_hall") << B("fort") << B("citadel");
        Battle walled(gs, reg, mine(), gs.town(t)->garrison, 1);
        QCOMPARE(walled.townDefense(), 6);
        QCOMPARE(battleToJson(walled)["townDefense"].toInt(), 6);
    }

    void jsonForTheClient() {
        add("archer", 3);
        Battle b(gs, reg, mine(), monsters("militia", 3), 1);
        const QJsonObject j = battleToJson(b);
        QCOMPARE(j["width"].toInt(), hex::kWidth);
        QCOMPARE(j["units"].toArray().size(), 2);
        QCOMPARE(j["active"].toInt(), 1);
        QVERIFY(j["canShoot"].toBool());
        QVERIFY(!j["reachable"].toArray().isEmpty());
    }
};

QTEST_GUILESS_MAIN(TstHexBattle)
#include "tst_hexbattle.moc"
