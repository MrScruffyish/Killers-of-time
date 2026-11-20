#include <QDir>
#include <QSet>
#include <QTemporaryDir>
#include <QtTest>

#include "core/content/ContentRegistry.h"
#include "core/rules/GameSetup.h"
#include "core/rules/Scenario.h"
#include "core/rules/ScenarioGen.h"
#include "core/rules/Campaign.h"
#include "core/rules/Movement.h"
#include "core/rules/Pathfinding.h"
#include "core/rules/TurnCycle.h"
#include "server/GameSession.h"

using namespace kot;
using namespace kot::core;

class TstScenario : public QObject {
    Q_OBJECT
    ContentRegistry reg;

    GameState makeMap(const QStringList& races, quint64 seed = 9) {
        GameSetupParams p;
        p.seed = seed;
        p.mapWidth = p.mapHeight = 32;
        for (int i = 0; i < races.size(); ++i) p.players.append({QString("P%1").arg(i), false, {}, races[i], i + 1, "#ff0000"});
        GameState gs;
        createGame(reg, p, &gs, nullptr, nullptr);
        return gs;
    }

    // Tiles (towns, mines, map objects) that cannot be walked to from the first
    // town: a map that needs boats lists them.
    static QStringList unreachableOnFoot(const GameState& gs, const ContentRegistry& reg) {
        const MoveCostGrid grid(gs.map(), reg, MoveMode::Foot);
        QList<Pos> targets;
        for (const Town& t : gs.towns()) targets << t.pos;
        for (const Mine& m : gs.mines()) targets << m.pos;
        for (const MapObject& o : gs.mapObjects()) targets << o.pos;
        if (targets.isEmpty()) return {};
        const QHash<Pos, int> reach = reachableTiles(grid, targets.first(), 1000000000);
        QStringList out;
        for (const Pos t : targets) {
            bool near = reach.contains(t);
            for (int dy = -1; dy <= 1 && !near; ++dy)
                for (int dx = -1; dx <= 1 && !near; ++dx) near = reach.contains(Pos{t.x + dx, t.y + dy});
            if (!near) out << QStringLiteral("(%1,%2)").arg(t.x).arg(t.y);
        }
        return out;
    }

private slots:
    void initTestCase() {
        QStringList errors;
        QVERIFY2(reg.loadFromDirectory(KOT_DATA_DIR, &errors), qPrintable(errors.join("\n")));
    }

    void generatedScenarioHasItsSettings() {
        ScenarioGenParams p;
        p.id = "gen";
        p.width = p.height = 40;
        p.seed = 5;
        p.races = {"humans", "random", "orcs"};
        p.victory = "gold";
        GameState gs;
        QString error;
        QVERIFY2(generateScenario(reg, p, &gs, &error), qPrintable(error));
        QCOMPARE(gs.map().width(), 40);
        QCOMPARE(gs.players().size(), 3);
        const QList<Player> players = gs.players().values();
        QCOMPARE(players[0].race, reg.raceId("humans"));
        QVERIFY(reg.playableRaces().contains(players[1].race));
        QCOMPARE(gs.victory().type, VictoryCondition::Type::Gold);
        QCOMPARE(gs.victory().amount, 50000);
        // It saves and loads as a scenario like any other.
        QTemporaryDir tmp;
        QVERIFY(saveScenario(tmp.filePath("gen.json"), "gen", "Gen", "", gs, reg, &error));
        Scenario s;
        QStringList errors;
        QVERIFY2(loadScenario(tmp.filePath("gen.json"), reg, &s, &errors), qPrintable(errors.join("\n")));
        QCOMPARE(s.info.races.size(), 3);

        p.victory = "capital";
        QVERIFY(generateScenario(reg, p, &gs, &error));
        QCOMPARE(gs.victory().type, VictoryCondition::Type::HoldTown);
        QVERIFY(gs.town(gs.victory().town));
        // The slay goal places one hero-less horde of the purple eater and points at it.
        p.victory = "slay";
        QVERIFY2(generateScenario(reg, p, &gs, &error), qPrintable(error));
        QCOMPARE(gs.victory().type, VictoryCondition::Type::SlayMonster);
        const Army* monster = gs.army(gs.victory().targetArmy);
        QVERIFY(monster);
        QVERIFY(!monster->owner.isValid());
        QVERIFY(!monster->hasHero());
        QCOMPARE(monster->stacks.first().unitTypeId(), reg.unitId("flying_purple_people_eater"));
        QVERIFY(saveScenario(tmp.filePath("slay.json"), "slay", "Slay", "", gs, reg, &error));
        Scenario slay;
        QVERIFY2(loadScenario(tmp.filePath("slay.json"), reg, &slay, &errors), qPrintable(errors.join("\n")));
        GameState fromScenario;
        QVERIFY2(GameState::fromJson(slay.game, reg, &fromScenario, &errors), qPrintable(errors.join("\n")));
        QCOMPARE(fromScenario.victory().type, VictoryCondition::Type::SlayMonster);
        QVERIFY(fromScenario.army(fromScenario.victory().targetArmy));
        // A different boss can be named instead of the purple eater.
        p.victory = "slay";
        p.monster = "elder_kraken";
        QVERIFY2(generateScenario(reg, p, &gs, &error), qPrintable(error));
        monster = gs.army(gs.victory().targetArmy);
        QVERIFY(monster);
        QCOMPARE(monster->stacks.first().unitTypeId(), reg.unitId("elder_kraken"));
        // The find goal guarantees that its artifact is on the map, and saves it.
        p.victory = "artifact";
        p.artifact = "crown_of_kings";
        QVERIFY2(generateScenario(reg, p, &gs, &error), qPrintable(error));
        const ArtifactId prize = reg.artifactId("crown_of_kings");
        QVERIFY(prize.isValid());
        QCOMPARE(gs.victory().type, VictoryCondition::Type::FindArtifact);
        QCOMPARE(gs.victory().targetArtifact, prize);
        bool placed = false;
        for (const MapObject& o : gs.mapObjects())
            if (o.kind == MapObject::Kind::Artifact && o.artifact == prize) placed = true;
        QVERIFY(placed);
        QVERIFY(saveScenario(tmp.filePath("find.json"), "find", "Find", "", gs, reg, &error));
        Scenario find;
        QVERIFY2(loadScenario(tmp.filePath("find.json"), reg, &find, &errors), qPrintable(errors.join("\n")));
        GameState fromFind;
        QVERIFY2(GameState::fromJson(find.game, reg, &fromFind, &errors), qPrintable(errors.join("\n")));
        QCOMPARE(fromFind.victory().type, VictoryCondition::Type::FindArtifact);
        QCOMPARE(fromFind.victory().targetArtifact, prize);
        // Exploring uses victoryAmount as the percentage of land to reveal.
        p.victory = "explore";
        p.victoryAmount = 70;
        QVERIFY2(generateScenario(reg, p, &gs, &error), qPrintable(error));
        QCOMPARE(gs.victory().type, VictoryCondition::Type::ExploreMap);
        QCOMPARE(gs.victory().amount, 70);
        // Bad settings are refused.
        p.races = {"humans"};
        QVERIFY(!generateScenario(reg, p, &gs, &error));
        p.races = {"humans", "no_such_race"};
        QVERIFY(!generateScenario(reg, p, &gs, &error));
        QVERIFY(!validGeneratedId("../evil"));
        QVERIFY(validGeneratedId("my_map-2"));
    }

    void generatedCampaignGrows() {
        QTemporaryDir tmp;
        CampaignGenParams p;
        p.id = "gc";
        p.name = "Generated";
        p.chapters = 3;
        p.playerRace = "dwarves";
        p.seed = 3;
        QString error;
        QVERIFY2(generateCampaign(reg, p, tmp.filePath("scenarios"), tmp.filePath("campaigns"), &error), qPrintable(error));
        CampaignDef def;
        QStringList errors;
        QVERIFY2(loadCampaign(tmp.filePath("campaigns/gc.json"), &def, &errors), qPrintable(errors.join("\n")));
        QCOMPARE(def.scenarios, QStringList({"gc_1", "gc_2", "gc_3"}));
        int lastWidth = 0, lastPlayers = 0;
        for (const QString& id : def.scenarios) {
            Scenario s;
            QVERIFY2(loadScenario(tmp.filePath("scenarios/" + id + ".json"), reg, &s, &errors), qPrintable(errors.join("\n")));
            QCOMPARE(s.info.races.first(), QString("dwarves"));
            QVERIFY(s.info.width > lastWidth);
            QVERIFY(s.info.races.size() > lastPlayers);
            lastWidth = s.info.width;
            lastPlayers = int(s.info.races.size());
        }
    }

    void generatedCampaignCanHaveTwentyChapters() {
        QTemporaryDir tmp;
        CampaignGenParams p;
        p.id = "long";
        p.chapters = 20;
        p.startSize = 32;
        p.playerRace = "humans";
        p.seed = 11;
        QString error;
        QVERIFY2(generateCampaign(reg, p, tmp.filePath("scenarios"), tmp.filePath("campaigns"), &error), qPrintable(error));
        CampaignDef def;
        QStringList errors;
        QVERIFY2(loadCampaign(tmp.filePath("campaigns/long.json"), &def, &errors), qPrintable(errors.join("\n")));
        QCOMPARE(def.scenarios.size(), 20);
        QList<GameState> games;
        QList<ArtifactId> targets;
        QSet<QString> bosses, prizes;
        for (int c = 0; c < def.scenarios.size(); ++c) {
            const QString& id = def.scenarios[c];
            Scenario s;
            QVERIFY2(loadScenario(tmp.filePath("scenarios/" + id + ".json"), reg, &s, &errors), qPrintable(errors.join("\n")));
            GameState gs;
            QVERIFY2(GameState::fromJson(s.game, reg, &gs, &errors), qPrintable(errors.join("\n")));
            games << gs;
            const VictoryCondition& v = gs.victory();
            targets << (v.type == VictoryCondition::Type::FindArtifact ? v.targetArtifact : ArtifactId());
            QVERIFY(s.info.races.size() >= 2 && s.info.races.size() <= 8);
            if (v.type == VictoryCondition::Type::SlayMonster) {
                const Army* army = gs.army(v.targetArmy);
                QVERIFY(army && !army->stacks.isEmpty());
                const UnitDef* unit = reg.unit(army->stacks.first().unitTypeId());
                QVERIFY(unit);
                bosses.insert(unit->key);
            } else if (v.type == VictoryCondition::Type::FindArtifact) {
                const ArtifactDef* artifact = reg.artifact(v.targetArtifact);
                QVERIFY(artifact);
                prizes.insert(artifact->key);
                bool placed = false;
                for (const MapObject& o : gs.mapObjects())
                    if (o.kind == MapObject::Kind::Artifact && o.artifact == v.targetArtifact) placed = true;
                QVERIFY(placed);
            } else if (v.type == VictoryCondition::Type::ExploreMap) {
                QVERIFY(v.amount >= 60 && v.amount <= 100);
            }
            if (c == def.scenarios.size() - 1) QCOMPARE(v.type, VictoryCondition::Type::Conquest);
        }
        // A chapter's prize is reserved for that chapter: no map object or quest
        // hut anywhere else in the campaign can hand it out early.
        QList<ArtifactId> reserved;
        for (ArtifactId a : targets)
            if (a.isValid() && !reserved.contains(a)) reserved << a;
        for (int c = 0; c < games.size(); ++c) {
            for (const MapObject& o : games[c].mapObjects()) {
                if (o.kind == MapObject::Kind::Artifact && reserved.contains(o.artifact))
                    QVERIFY2(o.artifact == targets[c], "a map holds another chapter's prize");
                if (o.site == QLatin1String("quest_hut") && reserved.contains(o.quest.rewardArtifact))
                    QVERIFY2(o.quest.rewardArtifact == targets[c], "a quest gives another chapter's prize");
            }
        }
        QVERIFY2(bosses.size() >= 2, "slay chapters should use different bosses");
        QVERIFY2(prizes.size() >= 2, "find chapters should use different artifacts");
        p.chapters = 21;
        QVERIFY(!generateCampaign(reg, p, tmp.filePath("scenarios"), tmp.filePath("campaigns"), &error));
        QVERIFY(error.contains(QStringLiteral("2 to 20")));
    }

    void fixedGoalCampaignCanBeShipped() {
        QTemporaryDir tmp;
        CampaignGenParams p;
        p.id = "fixed";
        p.name = "Fixed";
        p.chapters = 5;
        p.playerRace = "humans";
        p.seed = 17;
        p.goals = {"slay", "artifact", "explore", "days", "conquest"};
        QString error;
        QVERIFY2(generateCampaign(reg, p, tmp.filePath("scenarios"), tmp.filePath("campaigns"), &error), qPrintable(error));
        CampaignDef def;
        QStringList errors;
        QVERIFY2(loadCampaign(tmp.filePath("campaigns/fixed.json"), &def, &errors), qPrintable(errors.join("\n")));
        QCOMPARE(def.scenarios.size(), 5);
        QCOMPARE(def.carryHeroes, 1);
        QVERIFY(def.carryArmy);
        const QList<VictoryCondition::Type> expected = {
            VictoryCondition::Type::SlayMonster, VictoryCondition::Type::FindArtifact, VictoryCondition::Type::ExploreMap,
            VictoryCondition::Type::Days,        VictoryCondition::Type::Conquest};
        ArtifactId prize;
        for (int c = 0; c < def.scenarios.size(); ++c) {
            Scenario s;
            QVERIFY2(loadScenario(tmp.filePath("scenarios/" + def.scenarios[c] + ".json"), reg, &s, &errors), qPrintable(errors.join("\n")));
            GameState gs;
            QVERIFY2(GameState::fromJson(s.game, reg, &gs, &errors), qPrintable(errors.join("\n")));
            const VictoryCondition& v = gs.victory();
            QCOMPARE(v.type, expected[c]);
            if (v.type == VictoryCondition::Type::SlayMonster) QVERIFY(gs.army(v.targetArmy));
            if (v.type == VictoryCondition::Type::FindArtifact) {
                prize = v.targetArtifact;
                QVERIFY(prize.isValid());
                bool placed = false;
                for (const MapObject& o : gs.mapObjects()) placed = placed || (o.kind == MapObject::Kind::Artifact && o.artifact == prize);
                QVERIFY(placed);
            }
            if (v.type == VictoryCondition::Type::ExploreMap) QVERIFY(v.amount >= 1 && v.amount <= 100);
            if (v.type == VictoryCondition::Type::Days) QVERIFY(v.amount > 0);
        }
        // The one artifact chapter's prize is kept out of all the others.
        QVERIFY(prize.isValid());
        for (const QString& id : def.scenarios) {
            Scenario s;
            QVERIFY2(loadScenario(tmp.filePath("scenarios/" + id + ".json"), reg, &s, &errors), qPrintable(errors.join("\n")));
            GameState gs;
            QVERIFY2(GameState::fromJson(s.game, reg, &gs, &errors), qPrintable(errors.join("\n")));
            for (const MapObject& o : gs.mapObjects()) {
                QVERIFY2(!(o.kind == MapObject::Kind::Artifact && o.artifact == prize)
                             || gs.victory().targetArtifact == prize, "another map holds the campaign prize");
                QVERIFY2(!(o.site == QLatin1String("quest_hut") && o.quest.rewardArtifact == prize)
                             || gs.victory().targetArtifact == prize, "another quest gives the campaign prize");
            }
        }
        p.goals = {"slay"};
        QVERIFY(!generateCampaign(reg, p, tmp.filePath("scenarios"), tmp.filePath("campaigns"), &error));
        QVERIFY(error.contains(QStringLiteral("one goal per chapter")));
    }

    void mapsNeedNoBoats() {
        // Shipped scenarios and continent maps: everything can be walked to (only
        // maps made with islands need boats).
        const QDir dir(QDir(KOT_DATA_DIR).filePath("scenarios"));
        for (const QFileInfo& f : dir.entryInfoList({"*.json"}, QDir::Files)) {
            Scenario s;
            QStringList errors;
            QVERIFY2(loadScenario(f.filePath(), reg, &s, &errors), qPrintable(errors.join("\n")));
            GameState gs;
            QVERIFY2(GameState::fromJson(s.game, reg, &gs, &errors), qPrintable(errors.join("\n")));
            const QStringList cut = unreachableOnFoot(gs, reg);
            QVERIFY2(cut.isEmpty(), qPrintable(f.fileName() + ": needs a boat for " + cut.join(" ")));
        }
        for (quint64 seed = 1; seed <= 12; ++seed) {
            GameSetupParams params;
            params.seed = seed;
            params.mapWidth = params.mapHeight = seed % 2 ? 48 : 72;
            params.players = {{"A", false, {}, "humans", 1, "#ff0000"}, {"B", true, {}, "orcs", 2, "#0000ff"}, {"C", true, {}, "sylvan", 3, "#00ff00"}};
            GameState gs;
            QList<PlayerId> ids;
            QString why;
            QVERIFY2(createGame(reg, params, &gs, &ids, &why), qPrintable(why));
            const QStringList cut = unreachableOnFoot(gs, reg);
            QVERIFY2(cut.isEmpty(), qPrintable(QStringLiteral("seed %1: needs a boat for %2").arg(seed).arg(cut.join(" "))));
        }
    }

    void weeklyNumbersAreKeptForTheEndScreen() {
        GameSetupParams params;
        params.seed = 3;
        params.mapWidth = params.mapHeight = 40;
        params.players = {{"A", false, {}, "humans", 1, "#ff0000"}, {"B", false, {}, "orcs", 2, "#0000ff"}};
        GameState gs;
        QList<PlayerId> ids;
        QVERIFY(createGame(reg, params, &gs, &ids));
        QCOMPARE(gs.history().size(), 1);  // week 1 as it starts
        while (gs.day() < 15) endTurn(gs, reg);
        QCOMPARE(gs.history().size(), 3);
        const QJsonObject week2 = gs.history()[1].toObject();
        QCOMPARE(week2["week"].toInt(), 2);
        QVERIFY(week2["players"].toObject()[QString::number(ids[0].value())].toObject()["towns"].toInt() >= 1);
        GameState loaded;
        QStringList errors;
        QVERIFY2(GameState::fromJson(gs.toJson(reg), reg, &loaded, &errors), qPrintable(errors.join("\n")));
        QCOMPARE(loaded.history(), gs.history());
    }

    void everyScenarioInTheDataLoads() {
        const QDir dir(QDir(KOT_DATA_DIR).filePath("scenarios"));
        const QFileInfoList files = dir.entryInfoList({"*.json"}, QDir::Files);
        QVERIFY(!files.isEmpty());
        for (const QFileInfo& f : files) {
            Scenario s;
            QStringList errors;
            QVERIFY2(loadScenario(f.filePath(), reg, &s, &errors), qPrintable(errors.join("\n")));
            QCOMPARE(s.info.id, f.completeBaseName());
        }
        QCOMPARE(listScenarios(dir.path(), reg).size(), files.size());
    }

    void builtInGoalScenariosLoad() {
        struct Case {
            const char* id;
            VictoryCondition::Type type;
            int amount;
        };
        const QList<Case> cases = {
            {"beast_hunt", VictoryCondition::Type::SlayMonster, 0},
            {"lost_relic", VictoryCondition::Type::FindArtifact, 0},
            {"unexplored_west", VictoryCondition::Type::ExploreMap, 75},
            {"siege_of_the_capital", VictoryCondition::Type::HoldTown, 0},
            {"gold_rush", VictoryCondition::Type::Gold, 40000},
        };
        const QDir dir(KOT_DATA_DIR);
        for (const Case& c : cases) {
            Scenario s;
            QStringList errors;
            QVERIFY2(loadScenario(dir.filePath(QStringLiteral("scenarios/%1.json").arg(QString::fromLatin1(c.id))), reg, &s,
                                  &errors),
                     qPrintable(errors.join("\n")));
            GameState gs;
            QVERIFY2(GameState::fromJson(s.game, reg, &gs, &errors), qPrintable(errors.join("\n")));
            QCOMPARE(gs.victory().type, c.type);
            if (c.amount) QCOMPARE(gs.victory().amount, c.amount);
            if (c.type == VictoryCondition::Type::SlayMonster) QVERIFY(gs.army(gs.victory().targetArmy));
            if (c.type == VictoryCondition::Type::FindArtifact) {
                bool placed = false;
                for (const MapObject& o : gs.mapObjects())
                    placed = placed || (o.kind == MapObject::Kind::Artifact && o.artifact == gs.victory().targetArtifact);
                QVERIFY(placed);
            }
        }
    }

    void saveLoadAndStart() {
        QTemporaryDir tmp;
        const GameState map = makeMap({"undead", "sylvan"});
        QString error;
        QVERIFY(saveScenario(tmp.filePath("test.json"), "test", "Test", "Two races", map, reg, &error));
        Scenario s;
        QStringList errors;
        QVERIFY2(loadScenario(tmp.filePath("test.json"), reg, &s, &errors), qPrintable(errors.join("\n")));
        QCOMPARE(s.info.races, QStringList({"undead", "sylvan"}));
        QCOMPARE(s.info.width, 32);

        GameSetupParams params;
        params.difficulty = "hard";
        PlayerSetup bot{"Bot", true, "simple", "humans", 2, "#00ff00"};
        params.players = {{"Ann", false, {}, "humans", 1, "#ff0000"}, bot};
        GameState gs;
        QList<PlayerId> ids;
        QVERIFY2(startScenario(s, reg, params, &gs, &ids, &error), qPrintable(error));
        QCOMPARE(ids.size(), 2);
        QCOMPARE(gs.player(ids[0])->name, QString("Ann"));
        QCOMPARE(gs.player(ids[0])->race, reg.raceId("undead"));  // the scenario decides
        QCOMPARE(gs.player(ids[1])->controller, Player::Controller::Ai);
        QCOMPARE(gs.player(ids[1])->incomePercent, reg.balance().level("hard").aiIncomePercent);
        QVERIFY(gs.teamOf(ids[0]) != gs.teamOf(ids[1]));
        QCOMPARE(gs.day(), 1);
        QCOMPARE(gs.currentPlayer(), ids[0]);
        QCOMPARE(gs.towns().size(), map.towns().size());
        bool named = false;
        for (const Town& t : gs.towns()) named = named || t.name == "Ann's capital";
        QVERIFY(named);

        params.players.removeLast();
        QVERIFY(!startScenario(s, reg, params, &gs, &ids, &error));
        QVERIFY(error.contains("2 players"));
    }

    void notAScenario() {
        QTemporaryDir tmp;
        QFile f(tmp.filePath("x.json"));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(R"({"format":"something"})");
        f.close();
        Scenario s;
        QStringList errors;
        QVERIFY(!loadScenario(tmp.filePath("x.json"), reg, &s, &errors));
        QVERIFY(errors.join(" ").contains("not a scenario"));
    }

    void sessionStartsFromTheLobbySetting() {
        net::LobbyState lobby;
        lobby.settings.scenario = "two_kingdoms";
        net::SlotInfo a;
        a.index = 0; a.kind = net::SlotKind::Human; a.clientId = 1; a.name = "Ann"; a.race = "sylvan"; a.team = 1; a.color = "#d62728"; a.ready = true;
        net::SlotInfo b;
        b.index = 1; b.kind = net::SlotKind::Ai; b.name = "Bot"; b.race = "humans"; b.team = 2; b.color = "#1f77b4"; b.ready = true; b.aiProfile = "simple";
        lobby.slotList = {a, b};
        server::GameSession session(reg);
        QString error;
        QVERIFY2(session.start(lobby, &error), qPrintable(error));
        QCOMPARE(session.state().map().width(), 48);
        QCOMPARE(session.state().player(session.playerOfClient(1))->race, reg.raceId("humans"));  // slot 1 of the scenario
    }
};

QTEST_GUILESS_MAIN(TstScenario)
#include "tst_scenario.moc"
