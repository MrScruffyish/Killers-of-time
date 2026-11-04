#include <QJsonArray>
#include <QtTest>

#include "core/content/ContentRegistry.h"
#include "core/model/PlayerView.h"
#include "core/rules/HeroRules.h"
#include "core/rules/Vision.h"

using namespace kot::core;

class TstPlayerView : public QObject {
    Q_OBJECT
    ContentRegistry reg;
    GameState gs;
    PlayerId p1, p2;
    HeroId hero1, hero2;
    TownId town2;

    static bool hasId(const QJsonArray& a, int id) {
        for (const QJsonValue& v : a) if (v.toObject().value("id").toInt() == id) return true;
        return false;
    }
    static QJsonObject find(const QJsonArray& a, int id) {
        for (const QJsonValue& v : a) if (v.toObject().value("id").toInt() == id) return v.toObject();
        return {};
    }
    QJsonObject view(PlayerId p) {
        updateExplored(gs, reg, gs.teamOf(p));
        return buildPlayerView(gs, reg, p);
    }

private slots:
    void initTestCase() {
        QStringList errors;
        QVERIFY2(reg.loadFromDirectory(KOT_DATA_DIR, &errors), qPrintable(errors.join("\n")));
    }

    void init() {
        gs = GameState(1);
        gs.setMap(GameMap(40, 20, "grass"));
        p1 = gs.addPlayer("A", gs.newTeam(), Player::Controller::Human, "humans", "#ff0000");
        p2 = gs.addPlayer("B", gs.newTeam(), Player::Controller::Human, "humans", "#0000ff");
        gs.player(p1)->resources = {{"gold", 1234}};
        gs.player(p2)->resources = {{"gold", 777}};
        hero1 = createHero(gs, reg, p1, "Ann", "knight", {5, 5});
        hero2 = createHero(gs, reg, p2, "Bob", "knight", {30, 5});
        town2 = gs.addTown("Fort", "humans", {30, 5}, p2);
        gs.army(gs.town(town2)->garrison)->addUnits("pikeman", 7, 10);
        gs.army(gs.hero(hero2)->army)->addUnits("militia", 4, 6);
        gs.army(gs.hero(hero1)->army)->addUnits("militia", 9, 6);
        gs.army(gs.hero(hero1)->army)->stacks[0].applyDamage(5);
    }

    void basicFields() {
        const QJsonObject v = view(p1);
        QCOMPARE(v["day"].toInt(), 1);
        QCOMPARE(v["you"].toInt(), int(p1.value()));
        QCOMPARE(v["currentPlayer"].toInt(), int(gs.currentPlayer().value()));
        QCOMPARE(v["players"].toArray().size(), 2);
    }

    void ownHeroIsShownInFullAndTheFarEnemyIsNot() {
        const QJsonObject v = view(p1);
        const QJsonArray heroes = v["heroes"].toArray();
        QVERIFY(hasId(heroes, hero1.value()));
        QVERIFY(!hasId(heroes, hero2.value()));
        const QJsonObject mine = find(heroes, hero1.value());
        QVERIFY(mine.contains("traits"));
        QVERIFY(mine.contains("movementPoints"));
        QVERIFY(mine.contains("spellPoints"));
    }

    void resourcesArePrivate() {
        const QJsonObject v = view(p1);
        QCOMPARE(v["resources"].toObject()["gold"].toInt(), 1234);
        for (const QJsonValue& pl : v["players"].toArray()) QVERIFY(!pl.toObject().contains("resources"));
        QVERIFY(!QJsonDocument(v).toJson().contains("777"));  // the other player's gold appears nowhere
    }

    void unexploredTilesAreMaskedAndSeenTilesAreNot() {
        const QJsonObject map = view(p1)["map"].toObject();
        const QJsonArray tiles = map["tiles"].toArray();
        QCOMPARE(tiles.size(), 40 * 20);
        QVERIFY(tiles[5 * 40 + 5].toInt() >= 0);      // around the hero
        QCOMPARE(tiles[5 * 40 + 30].toInt(), -1);     // far away: unknown
        QCOMPARE(tiles[19 * 40 + 39].toInt(), -1);
        const QByteArray visible = QByteArray::fromBase64(map["visible"].toString().toLatin1());
        QCOMPARE(visible.size(), 800);
        QVERIFY(visible[5 * 40 + 5] == 1);
        QVERIFY(visible[5 * 40 + 30] == 0);
    }

    void farObjectsAreHiddenNearOnesShown() {
        const MineId near = gs.addMine("gold", 1000, {7, 5});
        const MineId far = gs.addMine("wood", 2, {35, 15});
        const MapDwellingId d = gs.addMapDwelling("archer", {37, 2}, 4);
        const QJsonObject v = view(p1);
        QVERIFY(hasId(v["mines"].toArray(), near.value()));
        QVERIFY(!hasId(v["mines"].toArray(), far.value()));
        QVERIFY(!hasId(v["mapDwellings"].toArray(), d.value()));
        QVERIFY(!hasId(v["towns"].toArray(), town2.value()));
    }

    void ownThingsAreAlwaysShownEvenOutOfSight() {
        const MineId mine = gs.addMine("gold", 1000, {35, 15}, p1);
        const MapDwellingId d = gs.addMapDwelling("archer", {37, 2}, 4, p1);
        const QJsonObject v = view(p1);
        QVERIFY(hasId(v["mines"].toArray(), mine.value()));
        const QJsonObject dwelling = find(v["mapDwellings"].toArray(), d.value());
        QCOMPARE(dwelling["available"].toInt(), 4);
    }

    void visibleEnemiesHaveNoSecrets() {
        gs.army(gs.hero(hero2)->army)->pos = {8, 5};  // walks up to Ann's hero
        const QJsonObject v = view(p1);
        const QJsonObject enemy = find(v["heroes"].toArray(), hero2.value());
        QVERIFY(!enemy.isEmpty());
        QCOMPARE(enemy["name"].toString(), QString("Bob"));
        QVERIFY(enemy.contains("level"));
        QVERIFY(!enemy.contains("traits"));
        QVERIFY(!enemy.contains("spellPoints"));
        QVERIFY(!enemy.contains("movementPoints"));

        const QJsonObject army = find(v["armies"].toArray(), gs.hero(hero2)->army.value());
        QCOMPARE(army["stacks"].toArray()[0].toObject()["count"].toInt(), 4);
        QVERIFY(!army["stacks"].toArray()[0].toObject().contains("totalHp"));
    }

    void ownArmiesShowExactHp() {
        const QJsonObject v = view(p1);
        const QJsonObject army = find(v["armies"].toArray(), gs.hero(hero1)->army.value());
        const QJsonObject stack = army["stacks"].toArray()[0].toObject();
        QCOMPARE(stack["totalHp"].toInt(), 9 * 6 - 5);
        QCOMPARE(stack["count"].toInt(), 9);
    }

    void enemyTownIsVisibleButItsGarrisonAndStockAreNot() {
        gs.army(gs.hero(hero1)->army)->pos = {27, 5};  // Ann stands next to Bob's town
        const QJsonObject v = view(p1);
        const QJsonObject town = find(v["towns"].toArray(), town2.value());
        QVERIFY(!town.isEmpty());
        QCOMPARE(town["name"].toString(), QString("Fort"));
        QVERIFY(!town.contains("buildings"));
        QVERIFY(!town.contains("recruitPool"));
        QVERIFY(!hasId(v["armies"].toArray(), gs.town(town2)->garrison.value()));  // garrison is secret
    }

    void teammatesSeeEachOtherInFull() {
        const PlayerId ally = gs.addPlayer("C", gs.player(p1)->team, Player::Controller::Ai, "humans", "#00ff00");
        const HeroId allyHero = createHero(gs, reg, ally, "Cee", "wizard", {20, 15});
        const QJsonObject v = view(p1);
        const QJsonObject h = find(v["heroes"].toArray(), allyHero.value());
        QVERIFY(!h.isEmpty());
        QVERIFY(h.contains("traits"));
        QVERIFY(!QJsonDocument(v).toJson().isEmpty());
    }

    void exploredTerrainIsRememberedObjectsAreNot() {
        const MineId mine = gs.addMine("gold", 1000, {7, 5});
        QVERIFY(hasId(view(p1)["mines"].toArray(), mine.value()));
        gs.army(gs.hero(hero1)->army)->pos = {30, 15};  // walks far away
        const QJsonObject v = view(p1);
        QVERIFY(v["map"].toObject()["tiles"].toArray()[5 * 40 + 7].toInt() >= 0);  // terrain remembered
        QVERIFY(!hasId(v["mines"].toArray(), mine.value()));                        // the mine is out of sight
    }

    void viewsDifferBetweenPlayers() {
        QVERIFY(view(p1) != view(p2));
        QCOMPARE(view(p2)["you"].toInt(), int(p2.value()));
        QCOMPARE(view(p2)["resources"].toObject()["gold"].toInt(), 777);
    }
};

QTEST_APPLESS_MAIN(TstPlayerView)
#include "tst_playerview.moc"
