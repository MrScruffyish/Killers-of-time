#include <QJsonDocument>
#include <QTemporaryDir>
#include <QtTest>

#include "TestContent.h"
#include <QFile>

#include "core/Version.h"
#include "core/content/ContentRegistry.h"
#include "core/rules/HeroRules.h"

using namespace kot::core;

class TstGameState : public QObject {
    Q_OBJECT

    ContentRegistry reg;

    GameState makeState() {
        GameState gs(42);
        gs.setMap(GameMap(8, 6, T("grass")));
        gs.map().setTerrain({2, 3}, T("sea"));
        gs.map().setTerrain({4, 4}, T("mountains"));
        const TeamId t1 = gs.newTeam();
        const TeamId t2 = gs.newTeam();
        const PlayerId a = gs.addPlayer("Alice", t1, Player::Controller::Human, RC("humans"), "#ff0000");
        const PlayerId b = gs.addPlayer("Bot", t2, Player::Controller::Ai, RC("humans"), "#0000ff");
        gs.player(b)->aiProfile = "passive";
        gs.player(a)->resources[R("gold")] = 1000;
        gs.player(a)->resources[R("wood")] = 5;
        const HeroId h = createHero(gs, reg, a, "Arthur", C("knight"), {1, 1});
        Army* army = gs.army(gs.hero(h)->army);
        army->addUnits(U("militia"), 12, 6);
        army->stacks[0].applyDamage(20);  // wounded stack must survive saving
        addExperience(*gs.hero(h), reg, 1500);
        const TownId town = gs.addTown("Capital", RC("humans"), {1, 1}, a);
        gs.town(town)->buildings << B("village_hall") << B("barracks");
        gs.town(town)->recruitPool[B("barracks")] = 7;
        gs.town(town)->lastBuildDay = 3;
        gs.town(town)->importedUnits[U("pikeman")] = 4;
        gs.town(town)->tavernOffers << H("merlin") << H("elaine");
        gs.hero(h)->templateId = H("arthur");
        const MapDwellingId dw = gs.addMapDwelling(U("archer"), {2, 2}, 9, a);
        Q_UNUSED(dw);
        gs.addMine(R("gold"), 1000, {5, 2}, a);
        gs.addMine(R("wood"), 2, {6, 2});  // neutral
        gs.rng().nextInt(1, 100);
        gs.rng().nextInt(1, 100);
        gs.setDay(7);
        return gs;
    }

    QJsonObject edited(const GameState& gs, const QString& from, const QString& to) {
        QString text = QString::fromUtf8(QJsonDocument(gs.toJson(reg)).toJson(QJsonDocument::Compact));
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
        useContent(reg);
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
        QCOMPARE(gs.map().terrainAt({2, 3}), T("sea"));
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
        QVERIFY2(GameState::fromJson(original.toJson(reg), reg, &loaded, &errors),
                 qPrintable(errors.join("\n")));
        QCOMPARE(loaded.toJson(reg), original.toJson(reg));

        QCOMPARE(loaded.day(), 7);
        QCOMPARE(loaded.map().terrainAt({4, 4}), T("mountains"));
        const Hero& h = loaded.heroes().first();
        QCOMPARE(h.name, QString("Arthur"));
        QCOMPARE(h.level, 2);
        QCOMPARE(h.pendingTraitPicks, 1);
        const UnitStack& s = loaded.army(h.army)->stacks[0];
        QCOMPARE(s.totalHp(), 12 * 6 - 20);  // wounded stack preserved
        QCOMPARE(s.count(), 9);
        QCOMPARE(loaded.towns().first().buildings, QList<BuildingTypeId>({B("village_hall"), B("barracks")}));
        QCOMPARE(loaded.towns().first().recruitPool.value(B("barracks")), 7);
        QCOMPARE(loaded.towns().first().lastBuildDay, 3);
        QCOMPARE(loaded.towns().first().importedUnits.value(U("pikeman")), 4);
        QCOMPARE(loaded.towns().first().tavernOffers, QList<HeroTemplateId>({H("merlin"), H("elaine")}));
        QCOMPARE(loaded.heroes().first().templateId, H("arthur"));
        QCOMPARE(loaded.mapDwellings().size(), 1);
        QCOMPARE(loaded.mapDwellings().first().available, 9);
        QCOMPARE(loaded.mapDwellings().first().unitId, U("archer"));
        QVERIFY(!loaded.mines().last().owner.isValid());
    }

    void rngContinuesIdenticallyAfterLoad() {
        GameState original = makeState();
        GameState loaded;
        QVERIFY(GameState::fromJson(original.toJson(reg), reg, &loaded, nullptr));
        for (int i = 0; i < 50; ++i)
            QCOMPARE(loaded.rng().nextInt(0, 1000000), original.rng().nextInt(0, 1000000));
    }

    void newIdsAfterLoadDoNotCollide() {
        GameState original = makeState();
        GameState loaded;
        QVERIFY(GameState::fromJson(original.toJson(reg), reg, &loaded, nullptr));
        QCOMPARE(loaded.newId<HeroId>().value(), original.newId<HeroId>().value());
    }

    void fileRoundTrip() {
        QTemporaryDir tmp;
        const QString path = tmp.filePath("game.json");
        GameState original = makeState();
        QString error;
        QVERIFY2(original.saveToFile(path, reg, &error), qPrintable(error));
        GameState loaded;
        QStringList errors;
        QVERIFY2(GameState::loadFromFile(path, reg, &loaded, &errors), qPrintable(errors.join("\n")));
        QCOMPARE(loaded.toJson(reg), original.toJson(reg));
    }

    void loadMissingFile() {
        GameState out;
        QStringList errors;
        QVERIFY(!GameState::loadFromFile("/nonexistent/save.json", reg, &out, &errors));
        QVERIFY(errors.join("\n").contains("cannot open"));
    }

    void contentIdsAreNumbersBoundToTheData() {
        GameState gs = makeState();
        gs.player(gs.turnOrder().first())->resources = amounts({{"gold", 1234}, {"gems", 3}});
        // Numbers, with the checksum of the data they belong to.
        const QJsonObject numbers = gs.toJson(reg);
        QCOMPARE(numbers["contentChecksum"].toString(), reg.checksum());
        QVERIFY(numbers["players"].toArray()[0].toObject()["resources"].isArray());
        expectLoadError(edited(gs, QStringLiteral("\"contentChecksum\":\"%1\"").arg(reg.checksum()), "\"contentChecksum\":\"0000\""),
                        "made with other game data");
        // Text keys (scenarios, kot_saveview): the same game.
        const QJsonObject text = gs.toJson(reg, GameState::IdStyle::Text);
        QCOMPARE(text["contentIds"].toString(), QString("text"));
        QCOMPARE(text["players"].toArray()[0].toObject()["resources"].toObject()["gold"].toInt(), 1234);
        GameState fromText;
        QStringList errors;
        QVERIFY2(GameState::fromJson(text, reg, &fromText, &errors), qPrintable(errors.join("; ")));
        QCOMPARE(fromText.toJson(reg), numbers);
    }

    void rejectsUnsupportedVersion() {
        GameState gs = makeState();
        expectLoadError(edited(gs, "\"formatVersion\":3", "\"formatVersion\":99"), "saved by another version of the game");
        expectLoadError(edited(gs, "\"formatVersion\":3", "\"formatVersion\":2"), "saved by another version of the game");  // older too
    }

    void saveFilesCarryVersionAndListInfo() {
        QTemporaryDir dir;
        GameState gs = makeState();
        const QString path = dir.filePath("mygame.json");
        QString error;
        QVERIFY2(gs.saveToFile(path, reg, &error), qPrintable(error));
        SaveInfo info = inspectSave(path, reg);
        QCOMPARE(info.name, QString("mygame"));
        QCOMPARE(info.gameVersion, QString(kot::core::kGameVersion));
        QCOMPARE(info.formatVersion, kSaveFormatVersion);
        QVERIFY(!info.savedAt.isEmpty());
        QCOMPARE(info.status, SaveInfo::Status::Ok);
        QCOMPARE(info.players.size(), gs.players().size());
        QVERIFY(GameState::loadFromFile(path, reg, &gs, nullptr));
        // A broken one says why.
        QFile f(dir.filePath("broken.json"));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(QJsonDocument(edited(gs, "\"formatVersion\":3", "\"formatVersion\":7")).toJson());
        f.close();
        info = inspectSave(dir.filePath("broken.json"), reg);
        QCOMPARE(info.status, SaveInfo::Status::Incompatible);
        QVERIFY(info.problems.join(" ").contains("another version"));
    }

    void rejectsUnknownTerrain() {
        GameState gs = makeState();
        // Text ids (scenarios): an unknown key.
        QString text = QString::fromUtf8(QJsonDocument(gs.toJson(reg, GameState::IdStyle::Text)).toJson(QJsonDocument::Compact));
        QVERIFY(text.contains("\"sea\""));
        text.replace("\"sea\"", "\"seaX\"");
        expectLoadError(QJsonDocument::fromJson(text.toUtf8()).object(), "unknown terrain 'seaX'");
        // Numbers: a terrain id that does not exist.
        QJsonObject json = gs.toJson(reg);
        QJsonObject map = json["map"].toObject();
        QJsonArray tiles = map["tiles"].toArray();
        tiles[0] = 999;
        map["tiles"] = tiles;
        json["map"] = map;
        expectLoadError(json, "terrain 999");
    }

    void rejectsOutOfBoundsPosition() {
        GameState gs = makeState();
        expectLoadError(edited(gs, "\"x\":6", "\"x\":60"), "is outside the 8x6 map");
    }

    void rejectsUnknownUnit() {
        GameState gs = makeState();
        // Text ids: an unknown key; numbers: an id that does not exist.
        QString text = QString::fromUtf8(QJsonDocument(gs.toJson(reg, GameState::IdStyle::Text)).toJson(QJsonDocument::Compact));
        QVERIFY(text.contains("\"unit\":\"militia\""));
        text.replace("\"unit\":\"militia\"", "\"unit\":\"ghost\"");
        expectLoadError(QJsonDocument::fromJson(text.toUtf8()).object(), "unknown unit 'ghost'");
        expectLoadError(edited(gs, QStringLiteral("\"unit\":%1").arg(U("militia").index()), "\"unit\":9999"), "unknown unit 9999");
    }

    void rejectsUnknownHeroClass() {
        GameState gs = makeState();
        QString text = QString::fromUtf8(QJsonDocument(gs.toJson(reg, GameState::IdStyle::Text)).toJson(QJsonDocument::Compact));
        QVERIFY(text.contains("\"class\":\"knight\""));
        text.replace("\"class\":\"knight\"", "\"class\":\"jester\"");
        expectLoadError(QJsonDocument::fromJson(text.toUtf8()).object(), "unknown hero class");
        expectLoadError(edited(gs, QStringLiteral("\"class\":%1").arg(C("knight").index()), "\"class\":999"), "unknown hero class");
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
        const QJsonObject before = out.toJson(reg);
        QStringList errors;
        QVERIFY(!GameState::fromJson(QJsonObject(), reg, &out, &errors));
        QCOMPARE(out.toJson(reg), before);
    }
};

QTEST_APPLESS_MAIN(TstGameState)
#include "tst_gamestate.moc"
