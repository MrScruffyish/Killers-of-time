#include <QtTest>

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

    TownId town(const QString& race) {
        const TownId t = gs.addTown("T", race, {5, 5}, p1);
        return t;
    }

private slots:
    void initTestCase() {
        QStringList errors;
        QVERIFY2(reg.loadFromDirectory(KOT_DATA_DIR, &errors), qPrintable(errors.join("\n")));
    }
    void init() {
        gs = GameState(4);
        gs.setMap(GameMap(20, 20, "grass"));
        p1 = gs.addPlayer("A", gs.newTeam(), Player::Controller::Human, "undead", "#ff0000");
        gs.player(p1)->resources = {{"gold", 50000}, {"wood", 50}, {"stone", 50}, {"mercury", 10}, {"gems", 10}};
    }

    void racesLoad() {
        QCOMPARE(reg.raceIds(), QStringList({"humans", "sylvan", "undead"}));
        QCOMPARE(reg.race("undead").growthPercent, 25);
        QCOMPARE(reg.race("sylvan").terrainCostPercent.value("forest"), 50);
        QCOMPARE(reg.race("nobody").growthPercent, 0);  // defaults
        for (const QString& race : reg.raceIds()) {
            int dwellings = 0;
            for (const BuildingDef& b : reg.buildings())
                for (const BuildingEffect& e : b.effects)
                    dwellings += (b.race == race && e.type == BuildingEffect::Type::Dwelling);
            QVERIFY2(dwellings >= 4, qPrintable(race));
        }
    }

    void sharedAndOwnBuildings() {
        const TownId t = town("undead");
        QVERIFY(whyCannotBuild(gs, reg, t, "tavern", p1).isEmpty());          // any race
        QVERIFY(whyCannotBuild(gs, reg, t, "barracks", p1).contains("cannot be built"));  // humans only
        QVERIFY(whyCannotBuild(gs, reg, t, "cursed_graveyard", p1).isEmpty());
    }

    void growthFollowsTheRace() {
        const TownId u = town("undead");
        QVERIFY(buildBuilding(gs, reg, u, "cursed_graveyard", p1).ok);
        QCOMPARE(gs.town(u)->recruitPool.value("cursed_graveyard"), (14 * 125 + 50) / 100);  // 18
        const TownId s = gs.addTown("S", "sylvan", {9, 9}, p1);
        QVERIFY(buildBuilding(gs, reg, s, "woodcutters_lodge", p1).ok);
        QCOMPARE(gs.town(s)->recruitPool.value("woodcutters_lodge"), (12 * 85 + 50) / 100);  // 10
    }

    void undeadTownsYieldLessGold() {
        town("undead");
        QCOMPARE(dailyIncome(gs, reg, p1).value("gold"), reg.balance().economy.townDailyGold * 80 / 100);
    }

    void sylvanHeroesAreFastInForests() {
        for (int x = 0; x < 20; ++x)
            for (int y = 0; y < 20; ++y) gs.map().setTerrain({x, y}, "forest");
        const PlayerId p2 = gs.addPlayer("B", gs.newTeam(), Player::Controller::Human, "sylvan", "#00ff00");
        const HeroId elf = createHero(gs, reg, p2, "E", "ranger", {0, 0});
        const HeroId knight = createHero(gs, reg, p1, "K", "knight", {0, 1});
        gs.hero(elf)->movementPoints = gs.hero(knight)->movementPoints = 1000;
        const MoveResult a = moveHero(gs, reg, elf, {15, 0});
        const MoveResult b = moveHero(gs, reg, knight, {15, 1});
        QVERIFY(a.ok && b.ok);
        QVERIFY2(a.stepsTaken > b.stepsTaken, qPrintable(QString("%1 vs %2").arg(a.stepsTaken).arg(b.stepsTaken)));
    }

    void mageGuildTeachesTheRaceSchools() {
        const TownId t = town("undead");
        gs.town(t)->buildings << "village_hall" << "mage_guild";
        const HeroId h = createHero(gs, reg, p1, "K", "knight", {5, 5});
        const QStringList learned = learnSpellsInTown(gs, reg, h);
        QVERIFY(learned.contains("curse"));        // dark
        QVERIFY(learned.contains("stone_skin"));   // earth
        QVERIFY(!learned.contains("magic_arrow")); // air: not in an undead guild
        QVERIFY(!learned.contains("entangle"));    // nature
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
                hero = hero || (h.owner == ids[i] && reg.heroClass(h.classId)->race == params.players[i].race);
            for (const Town& t : game.towns()) town = town || (t.owner == ids[i] && t.race == params.players[i].race);
            QVERIFY(hero && town);
        }
    }
};

QTEST_GUILESS_MAIN(TstRaces)
#include "tst_races.moc"
