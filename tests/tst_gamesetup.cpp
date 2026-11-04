#include <QSet>
#include <QtTest>

#include "core/content/ContentRegistry.h"
#include "core/rules/GameSetup.h"
#include "core/rules/TurnCycle.h"
#include "core/rules/Movement.h"

using namespace kot::core;

class TstGameSetup : public QObject {
    Q_OBJECT
    ContentRegistry reg;

    static GameSetupParams params(quint64 seed = 3, int mapSize = 64) {
        GameSetupParams p;
        p.mapWidth = mapSize;
        p.mapHeight = mapSize;
        p.seed = seed;
        p.players = {
            {"Ann", false, "", "humans", 1, "#d62728"},
            {"Bob", false, "", "humans", 2, "#1f77b4"},
            {"Bot", true, "passive", "humans", 2, "#2ca02c"},
        };
        return p;
    }

    static int dist(Pos a, Pos b) { return std::max(std::abs(a.x - b.x), std::abs(a.y - b.y)); }

private slots:
    void initTestCase() {
        QStringList errors;
        QVERIFY2(reg.loadFromDirectory(KOT_DATA_DIR, &errors), qPrintable(errors.join("\n")));
    }

    void difficultyChangesTheStart() {
        const auto make = [&](const QString& difficulty, const QString& aiLevel, GameState* gs, QList<PlayerId>* ids) {
            GameSetupParams params;
            params.seed = 12;
            params.difficulty = difficulty;
            PlayerSetup ai{"Bot", true, "simple", "humans", 2, "#00ff00"};
            ai.aiDifficulty = aiLevel;
            params.players = {{"Ann", false, {}, "humans", 1, "#ff0000"}, ai};
            QString error;
            return createGame(reg, params, gs, ids, &error);
        };
        GameState easy, expert;
        QList<PlayerId> e, x;
        QVERIFY(make("easy", "normal", &easy, &e));
        QVERIFY(make("expert", "hard", &expert, &x));
        const int startGold = reg.balance().economy.startingResources.value("gold");
        QCOMPARE(easy.player(e[0])->resources.value("gold"), startGold * 150 / 100);
        QCOMPARE(expert.player(x[0])->resources.value("gold"), startGold * 50 / 100);
        QCOMPARE(easy.player(e[1])->incomePercent, -20);
        QCOMPARE(expert.player(x[1])->incomePercent, 40 + 15);
        QCOMPARE(expert.difficulty(), QString("expert"));
        const auto monsters = [](const GameState& gs) {
            int n = 0;
            for (const Army& a : gs.armies()) if (!a.owner.isValid()) n += a.totalUnits();
            return n;
        };
        QVERIFY(monsters(expert) > monsters(easy));
        // The AI's bonus shows in its income; it is saved.
        GameState loaded;
        QStringList errors;
        QVERIFY(GameState::fromJson(expert.toJson(), reg, &loaded, &errors));
        QCOMPARE(loaded.player(x[1])->incomePercent, 55);
        QCOMPARE(loaded.difficulty(), QString("expert"));
        QCOMPARE(dailyIncome(expert, reg, x[1]).value("gold"),
                 dailyIncome(expert, reg, x[0]).value("gold") * 155 / 100);
    }

    void createsPlayersTownsAndHeroes() {
        GameState gs;
        QList<PlayerId> ids;
        QString error;
        QVERIFY2(createGame(reg, params(), &gs, &ids, &error), qPrintable(error));
        QCOMPARE(ids.size(), 3);
        QCOMPARE(gs.players().size(), 3);
        int owned = 0, neutral = 0;
        for (const Town& t : gs.towns()) (t.owner.isValid() ? owned : neutral)++;
        QCOMPARE(owned, 3);
        QVERIFY(neutral >= 1);  // free towns to conquer
        QCOMPARE(gs.heroes().size(), 3);
        QCOMPARE(gs.turnOrder().size(), 3);
        QCOMPARE(gs.currentPlayer(), ids[0]);
        QCOMPARE(gs.day(), 1);
        QCOMPARE(gs.player(ids[2])->controller, Player::Controller::Ai);
        QCOMPARE(gs.player(ids[2])->aiProfile, QString("passive"));
        for (const PlayerId id : ids) {
            QCOMPARE(gs.player(id)->resources.value("gold"), 10000);
            QCOMPARE(gs.player(id)->resources.value("wood"), 20);
        }
        // Teams: Bob and Bot together, Ann alone.
        QCOMPARE(gs.player(ids[1])->team, gs.player(ids[2])->team);
        QVERIFY(gs.player(ids[0])->team != gs.player(ids[1])->team);
    }

    void everyPlayerStartsInHisTownWithAnArmy() {
        GameState gs;
        QList<PlayerId> ids;
        QVERIFY(createGame(reg, params(), &gs, &ids));
        QSet<QString> templates;
        for (const PlayerId id : ids) {
            const Town* town = nullptr;
            for (const Town& t : gs.towns()) if (t.owner == id) town = &t;
            const Hero* hero = nullptr;
            for (const Hero& h : gs.heroes()) if (h.owner == id) hero = &h;
            QVERIFY(town && hero);
            QCOMPARE(gs.army(hero->army)->pos, town->pos);
            QVERIFY(gs.army(hero->army)->totalUnits() >= 10);
            QVERIFY(hero->movementPoints > 0);
            QCOMPARE(hero->movementPoints, armyMovementPoints(*gs.army(hero->army), hero, reg));
            QVERIFY(!hero->templateId.isEmpty());
            templates.insert(hero->templateId);
            QVERIFY(reg.terrainType(gs.map().terrainAt(town->pos))->walkable);
            QCOMPARE(town->race, QString("humans"));
        }
        QCOMPARE(templates.size(), 3);  // three different heroes
    }

    void startsAreSpreadOut() {
        GameState gs;
        QVERIFY(createGame(reg, params(), &gs, nullptr));
        QList<Pos> starts;
        for (const Town& t : gs.towns()) starts.append(t.pos);
        for (int i = 0; i < starts.size(); ++i)
            for (int j = i + 1; j < starts.size(); ++j)
                QVERIFY2(dist(starts[i], starts[j]) >= 8, "start positions too close");
    }

    void neutralObjectsAreOnLandAndSpaced() {
        GameState gs;
        QVERIFY(createGame(reg, params(), &gs, nullptr));
        QVERIFY(gs.mines().size() >= reg.resources().size());
        QVERIFY(gs.mapDwellings().size() >= 2);
        QList<Pos> all;
        for (const Town& t : gs.towns()) all.append(t.pos);
        for (const Mine& m : gs.mines()) {
            all.append(m.pos);
            QVERIFY(reg.terrainType(gs.map().terrainAt(m.pos))->walkable);
            QVERIFY(!m.owner.isValid());
        }
        for (const MapDwelling& d : gs.mapDwellings()) {
            all.append(d.pos);
            QVERIFY(reg.terrainType(gs.map().terrainAt(d.pos))->walkable);
            QVERIFY(d.available > 0);
        }
        for (int i = 0; i < all.size(); ++i)
            for (int j = i + 1; j < all.size(); ++j)
                QVERIFY2(dist(all[i], all[j]) >= 3, "objects too close together");
    }

    void everyTeamHasExploredItsSurroundings() {
        GameState gs;
        QList<PlayerId> ids;
        QVERIFY(createGame(reg, params(), &gs, &ids));
        for (const PlayerId id : ids) {
            const QByteArray& explored = gs.exploredMap(gs.player(id)->team);
            QVERIFY(explored.count(char(1)) > 20);
        }
    }

    void sameParamsSameGameDifferentSeedDifferentGame() {
        GameState a, b, c;
        QVERIFY(createGame(reg, params(5), &a, nullptr));
        QVERIFY(createGame(reg, params(5), &b, nullptr));
        QVERIFY(createGame(reg, params(6), &c, nullptr));
        QCOMPARE(a.toJson(), b.toJson());
        QVERIFY(a.toJson() != c.toJson());
    }

    void theNewGameCanBeSavedAndLoaded() {
        GameState gs;
        QVERIFY(createGame(reg, params(), &gs, nullptr));
        GameState loaded;
        QStringList errors;
        QVERIFY2(GameState::fromJson(gs.toJson(), reg, &loaded, &errors), qPrintable(errors.join("\n")));
        QCOMPARE(loaded.toJson(), gs.toJson());
    }

    void rejectsBadSetups() {
        GameState gs;
        QString error;
        GameSetupParams one = params();
        one.players.removeLast();
        one.players.removeLast();
        QVERIFY(!createGame(reg, one, &gs, nullptr, &error));
        QVERIFY(error.contains("two players"));

        GameSetupParams badRace = params();
        badRace.players[1].race = "elves";
        QVERIFY(!createGame(reg, badRace, &gs, nullptr, &error));
        QVERIFY(error.contains("elves"));

        GameSetupParams tiny = params(1, 4);
        QVERIFY(!createGame(reg, tiny, &gs, nullptr, &error));
        QVERIFY(error.contains("map size"));
    }

    void worksOnSeveralMapSizesAndSeeds() {
        for (int size : {32, 48, 96}) {
            for (quint64 seed = 1; seed <= 3; ++seed) {
                GameState gs;
                QString error;
                QVERIFY2(createGame(reg, params(seed, size), &gs, nullptr, &error),
                         qPrintable(QString("size %1 seed %2: %3").arg(size).arg(seed).arg(error)));
            }
        }
    }
};

QTEST_APPLESS_MAIN(TstGameSetup)
#include "tst_gamesetup.moc"
