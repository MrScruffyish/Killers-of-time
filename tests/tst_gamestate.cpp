#include <QJsonDocument>
#include <QTemporaryDir>
#include <QtTest>

#include "core/content/ContentRegistry.h"
#include "core/rules/HeroRules.h"

using namespace kot::core;

class TstGameState : public QObject {
    Q_OBJECT

    ContentRegistry reg;

    GameState makeState() {
        GameState gs(42);
        gs.setMap(GameMap(8, 6, "grass"));
        gs.map().setTerrain({2, 3}, "sea");
        gs.map().setTerrain({4, 4}, "mountains");
        const TeamId t1 = gs.newTeam();
        const TeamId t2 = gs.newTeam();
        const PlayerId a = gs.addPlayer("Alice", t1, Player::Controller::Human, "humans", "#ff0000");
        const PlayerId b = gs.addPlayer("Bot", t2, Player::Controller::Ai, "humans", "#0000ff");
        gs.player(b)->aiProfile = "passive";
        gs.player(a)->resources["gold"] = 1000;
        gs.player(a)->resources["wood"] = 5;
        const HeroId h = createHero(gs, reg, a, "Arthur", "knight", {1, 1});
        Army* army = gs.army(gs.hero(h)->army);
        army->addUnits("militia", 12, 6);
        army->stacks[0].applyDamage(20);  // wounded stack must survive saving
        addExperience(*gs.hero(h), reg, 1500);
        const TownId town = gs.addTown("Capital", "humans", {1, 1}, a);
        gs.town(town)->buildings << "village_hall" << "barracks";
        gs.town(town)->recruitPool["barracks"] = 7;
        gs.town(town)->lastBuildDay = 3;
        gs.town(town)->importedUnits["pikeman"] = 4;
        const MapDwellingId dw = gs.addMapDwelling("archer", {2, 2}, 9, a);
        Q_UNUSED(dw);
        gs.addMine("gold", 1000, {5, 2}, a);
        gs.addMine("wood", 2, {6, 2});  // neutral
        gs.rng().nextInt(1, 100);
        gs.rng().nextInt(1, 100);
        gs.setDay(7);
        return gs;
    }

    QJsonObject edited(const GameState& gs, const QString& from, const QString& to) {
        QString text = QString::fromUtf8(QJsonDocument(gs.toJson()).toJson(QJsonDocument::Compact));
        if (!text.contains(from)) qFatal("test bug: '%s' not in save", qPrintable(from));
        text.replace(from, to);
        return QJsonDocument::fromJson(text.toUtf8()).object();
    }

    void expectLoadError(const QJsonObject& json, const QString& needle) {
        GameState out;
        QStringList errors;
        QVERIFY2(!GameState::fromJson(json, reg, &out, &errors), "load should have failed");
        QVERIFY2(errors.join("\n").contains(needle),
                 qPrintable("expected '" + needle + "' in:\n" + errors.join("\n")));
    }

private slots:
    void initTestCase() {
        QStringList errors;
        QVERIFY2(reg.loadFromDirectory(KOT_DATA_DIR, &errors), qPrintable(errors.join("\n")));
    }

    void buildingBlocks() {
        GameState gs = makeState();
        QCOMPARE(gs.players().size(), 2);
        QCOMPARE(gs.heroes().size(), 1);
        QCOMPARE(gs.towns().size(), 1);
        QCOMPARE(gs.mines().size(), 2);
        QCOMPARE(gs.armies().size(), 2);  // hero army + town garrison
        QCOMPARE(gs.turnOrder().size(), 2);
        QCOMPARE(gs.currentPlayer(), gs.turnOrder()[0]);
        QVERIFY(gs.map().inBounds({7, 5}));
        QVERIFY(!gs.map().inBounds({8, 5}));
        QVERIFY(!gs.map().inBounds({-1, 0}));
        QCOMPARE(gs.map().terrainAt({2, 3}), QString("sea"));
    }

    void idsAreUniqueAcrossKinds() {
        GameState gs = makeState();
        QSet<quint32> ids;
        for (const Player& p : gs.players()) ids.insert(p.id.value());
        for (const Hero& h : gs.heroes()) ids.insert(h.id.value());
        for (const Army& a : gs.armies()) ids.insert(a.id.value());
        for (const Town& t : gs.towns()) ids.insert(t.id.value());
        for (const Mine& m : gs.mines()) ids.insert(m.id.value());
        QCOMPARE(ids.size(), 2 + 1 + 2 + 1 + 2);
    }

    void jsonRoundTrip() {
        GameState original = makeState();
        GameState loaded;
        QStringList errors;
        QVERIFY2(GameState::fromJson(original.toJson(), reg, &loaded, &errors),
                 qPrintable(errors.join("\n")));
        QCOMPARE(loaded.toJson(), original.toJson());

        QCOMPARE(loaded.day(), 7);
        QCOMPARE(loaded.map().terrainAt({4, 4}), QString("mountains"));
        const Hero& h = loaded.heroes().first();
        QCOMPARE(h.name, QString("Arthur"));
        QCOMPARE(h.level, 2);
        QCOMPARE(h.pendingTraitPicks, 1);
        const UnitStack& s = loaded.army(h.army)->stacks[0];
        QCOMPARE(s.totalHp(), 12 * 6 - 20);  // wounded stack preserved
        QCOMPARE(s.count(), 9);
        QCOMPARE(loaded.towns().first().buildings, QStringList({"village_hall", "barracks"}));
        QCOMPARE(loaded.towns().first().recruitPool.value("barracks"), 7);
        QCOMPARE(loaded.towns().first().lastBuildDay, 3);
        QCOMPARE(loaded.towns().first().importedUnits.value("pikeman"), 4);
        QCOMPARE(loaded.mapDwellings().size(), 1);
        QCOMPARE(loaded.mapDwellings().first().available, 9);
        QCOMPARE(loaded.mapDwellings().first().unitId, QString("archer"));
        QVERIFY(!loaded.mines().last().owner.isValid());
    }

    void rngContinuesIdenticallyAfterLoad() {
        GameState original = makeState();
        GameState loaded;
        QVERIFY(GameState::fromJson(original.toJson(), reg, &loaded, nullptr));
        for (int i = 0; i < 50; ++i)
            QCOMPARE(loaded.rng().nextInt(0, 1000000), original.rng().nextInt(0, 1000000));
    }

    void newIdsAfterLoadDoNotCollide() {
        GameState original = makeState();
        GameState loaded;
        QVERIFY(GameState::fromJson(original.toJson(), reg, &loaded, nullptr));
        QCOMPARE(loaded.newId<HeroId>().value(), original.newId<HeroId>().value());
    }

    void fileRoundTrip() {
        QTemporaryDir tmp;
        const QString path = tmp.filePath("game.json");
        GameState original = makeState();
        QString error;
        QVERIFY2(original.saveToFile(path, &error), qPrintable(error));
        GameState loaded;
        QStringList errors;
        QVERIFY2(GameState::loadFromFile(path, reg, &loaded, &errors), qPrintable(errors.join("\n")));
        QCOMPARE(loaded.toJson(), original.toJson());
    }

    void loadMissingFile() {
        GameState out;
        QStringList errors;
        QVERIFY(!GameState::loadFromFile("/nonexistent/save.json", reg, &out, &errors));
        QVERIFY(errors.join("\n").contains("cannot open"));
    }

    void rejectsUnsupportedVersion() {
        GameState gs = makeState();
        expectLoadError(edited(gs, "\"formatVersion\":1", "\"formatVersion\":99"),
                        "unsupported formatVersion 99");
    }

    void rejectsUnknownTerrain() {
        GameState gs = makeState();
        expectLoadError(edited(gs, "\"sea\"", "\"seaX\""), "unknown terrain 'seaX'");
    }

    void rejectsOutOfBoundsPosition() {
        GameState gs = makeState();
        expectLoadError(edited(gs, "\"x\":6", "\"x\":60"), "is outside the 8x6 map");
    }

    void rejectsUnknownUnit() {
        GameState gs = makeState();
        expectLoadError(edited(gs, "\"unit\":\"militia\"", "\"unit\":\"ghost\""),
                        "unknown unit 'ghost'");
    }

    void rejectsUnknownHeroClass() {
        GameState gs = makeState();
        expectLoadError(edited(gs, "\"class\":\"knight\"", "\"class\":\"jester\""),
                        "unknown hero class 'jester'");
    }

    void rejectsMissingHeroArmy() {
        GameState gs = makeState();
        const quint32 armyId = gs.heroes().first().army.value();
        expectLoadError(edited(gs, QString("\"army\":%1").arg(armyId), "\"army\":9999"),
                        "army 9999 does not exist");
    }

    void rejectsDuplicateIds() {
        GameState gs = makeState();
        const quint32 last = gs.mines().last().id.value();
        const quint32 first = gs.mines().first().id.value();
        expectLoadError(edited(gs, QString("\"id\":%1,\"resource\"").arg(last),
                               QString("\"id\":%1,\"resource\"").arg(first)),
                        "is used twice");
    }

    void rejectsEmptyObject() {
        GameState out;
        QStringList errors;
        QVERIFY(!GameState::fromJson(QJsonObject(), reg, &out, &errors));
        QVERIFY(errors.join("\n").contains("missing required field 'formatVersion'"));
    }

    void failedLoadLeavesOutputUntouched() {
        GameState out = makeState();
        const QJsonObject before = out.toJson();
        QStringList errors;
        QVERIFY(!GameState::fromJson(QJsonObject(), reg, &out, &errors));
        QCOMPARE(out.toJson(), before);
    }
};

QTEST_APPLESS_MAIN(TstGameState)
#include "tst_gamestate.moc"
