#include <QtTest>

#include "TestContent.h"

#include "core/content/ContentRegistry.h"
#include "core/magic/Spells.h"
#include "core/rules/GameSetup.h"
#include "core/rules/HeroRules.h"
#include "core/rules/Movement.h"
#include "core/rules/Pathfinding.h"
#include "core/rules/TownRules.h"
#include "core/rules/TurnCycle.h"

using namespace kot::core;

class TstRaces : public QObject {
    Q_OBJECT
    ContentRegistry reg;
    GameState gs;
    PlayerId p1;

    TownId town(RaceId race) {
        const TownId t = gs.addTown("T", race, {5, 5}, p1);
        return t;
    }

private slots:
    void initTestCase() {
        QStringList errors;
        QVERIFY2(reg.loadFromDirectory(KOT_DATA_DIR, &errors), qPrintable(errors.join("\n")));
        useContent(reg);
    }
    void init() {
        gs = GameState(4);
        gs.setMap(GameMap(20, 20, T("grass")));
        p1 = gs.addPlayer("A", gs.newTeam(), Player::Controller::Human, RC("undead"), "#ff0000");
        gs.player(p1)->resources = amounts({{"gold", 50000}, {"wood", 50}, {"stone", 50}, {"mercury", 10}, {"gems", 10}});
    }

    void racesLoad() {
        QCOMPARE(reg.playableRaceKeys(), QStringList({"ashborn", "dwarves", "humans", "orcs", "sylvan", "tidefolk", "undead"}));
        QVERIFY(reg.race(RC("undead")).growthPercent > 0);  // the undead grow fast (the number is balance)
        QCOMPARE(reg.race(RC("sylvan")).terrainCostPercent.value(T("forest").index()), 50);
        QCOMPARE(reg.race(RaceId()).growthPercent, 0);  // defaults
        QVERIFY(reg.raceId("neutral").isValid() && !reg.playableRaces().contains(reg.raceId("neutral")));
        for (const QString& race : reg.playableRaceKeys()) {
            int dwellings = 0;
            for (const BuildingDef& b : reg.buildings())
                for (const BuildingEffect& e : b.effects)
                    dwellings += (b.race == reg.raceId(race) && e.type == BuildingEffect::Type::Dwelling);
            QVERIFY2(dwellings >= 4, qPrintable(race));
        }
    }

    void sharedAndOwnBuildings() {
        const TownId t = town(RC("undead"));
        QVERIFY(whyCannotBuild(gs, reg, t, B("tavern"), p1).isEmpty());          // any race
        QVERIFY(whyCannotBuild(gs, reg, t, B("barracks"), p1).contains("cannot be built"));  // humans only
        QVERIFY(whyCannotBuild(gs, reg, t, B("cursed_graveyard"), p1).isEmpty());
    }

    void growthFollowsTheRace() {
        const TownId u = town(RC("undead"));
        QVERIFY(buildBuilding(gs, reg, u, B("cursed_graveyard"), p1).ok);
        const int grown = (reg.unit(U("skeleton"))->weeklyGrowth * (100 + reg.race(RC("undead")).growthPercent) + 50) / 100;
        QCOMPARE(gs.town(u)->recruitPool.value(B("cursed_graveyard")), grown);
        QCOMPARE(townGrowth(gs, *gs.town(u), *reg.unit(U("skeleton")), reg), grown);
        const TownId s = gs.addTown("S", RC("sylvan"), {9, 9}, p1);
        QVERIFY(buildBuilding(gs, reg, s, B("woodcutters_lodge"), p1).ok);
        QCOMPARE(gs.town(s)->recruitPool.value(B("woodcutters_lodge")), (12 * 125 + 50) / 100);  // 15
    }

    void undeadTownsYieldLessGold() {
        town(RC("undead"));
        QCOMPARE(dailyIncome(gs, reg, p1).value(R("gold")), reg.balance().economy.townDailyGold * 90 / 100);
    }

    void sylvanHeroesAreFastInForests() {
        for (int x = 0; x < 20; ++x)
            for (int y = 0; y < 20; ++y) gs.map().setTerrain({x, y}, T("forest"));
        const PlayerId p2 = gs.addPlayer("B", gs.newTeam(), Player::Controller::Human, RC("sylvan"), "#00ff00");
        const HeroId elf = createHero(gs, reg, p2, "E", C("ranger"), {0, 0});
        const HeroId knight = createHero(gs, reg, p1, "K", C("knight"), {0, 1});
        gs.hero(elf)->movementPoints = gs.hero(knight)->movementPoints = 1000;
        const MoveResult a = moveHero(gs, reg, elf, {15, 0});
        const MoveResult b = moveHero(gs, reg, knight, {15, 1});
        QVERIFY(a.ok && b.ok);
        QVERIFY2(a.stepsTaken > b.stepsTaken, qPrintable(QString("%1 vs %2").arg(a.stepsTaken).arg(b.stepsTaken)));
    }

    void mageGuildTeachesTheRaceSchools() {
        const TownId t = town(RC("undead"));
        gs.town(t)->buildings << B("village_hall") << B("mage_guild");
        const HeroId h = createHero(gs, reg, p1, "K", C("knight"), {5, 5});
        const QList<SpellId> learned = learnSpellsInTown(gs, reg, h);
        QVERIFY(learned.contains(S("curse")));        // dark
        QVERIFY(learned.contains(S("stone_skin")));   // earth
        QVERIFY(!learned.contains(S("magic_arrow"))); // air: not in an undead guild
        QVERIFY(!learned.contains(S("entangle")));    // nature
    }

    void allRacesCanStartAGame() {
        GameSetupParams params;
        params.seed = 77;
        params.players = {{"H", false, {}, "humans", 1, "#ff0000"},
                          {"U", true, "simple", "undead", 2, "#00ff00"},
                          {"S", true, "simple", "sylvan", 3, "#0000ff"}};
        GameState game;
        QList<PlayerId> ids;
        QString error;
        QVERIFY2(createGame(reg, params, &game, &ids, &error), qPrintable(error));
        for (int i = 0; i < 3; ++i) {
            bool hero = false, town = false;
            for (const Hero& h : game.heroes())
                hero = hero || (h.owner == ids[i] && reg.heroClass(h.classId)->race == reg.raceId(params.players[i].race));
            for (const Town& t : game.towns()) town = town || (t.owner == ids[i] && t.race == reg.raceId(params.players[i].race));
            QVERIFY(hero && town);
        }
    }
};

QTEST_GUILESS_MAIN(TstRaces)
#include "tst_races.moc"
