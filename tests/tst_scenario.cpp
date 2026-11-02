#include <QDir>
#include <QTemporaryDir>
#include <QtTest>

#include "core/content/ContentRegistry.h"
#include "core/rules/GameSetup.h"
#include "core/rules/Scenario.h"
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

private slots:
    void initTestCase() {
        QStringList errors;
        QVERIFY2(reg.loadFromDirectory(KOT_DATA_DIR, &errors), qPrintable(errors.join("\n")));
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

    void saveLoadAndStart() {
        QTemporaryDir tmp;
        const GameState map = makeMap({"undead", "sylvan"});
        QString error;
        QVERIFY(saveScenario(tmp.filePath("test.json"), "test", "Test", "Two races", map, &error));
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
        QCOMPARE(gs.player(ids[0])->race, QString("undead"));  // the scenario decides
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
        QCOMPARE(session.state().player(session.playerOfClient(1))->race, QString("humans"));  // slot 1 of the scenario
    }
};

QTEST_GUILESS_MAIN(TstScenario)
#include "tst_scenario.moc"
