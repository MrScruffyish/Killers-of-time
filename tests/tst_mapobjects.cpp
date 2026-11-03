#include <QJsonArray>
#include <QtTest>

#include "core/content/ContentRegistry.h"
#include "core/model/PlayerView.h"
#include "core/rules/GameSetup.h"
#include "core/rules/HeroRules.h"
#include "core/rules/Movement.h"

using namespace kot::core;

class TstMapObjects : public QObject {
    Q_OBJECT
    ContentRegistry reg;
    GameState gs;
    PlayerId p1, p2;

    MapObjectId treasure(Pos pos, ResourceAmounts res) {
        MapObject o;
        o.kind = MapObject::Kind::Treasure;
        o.pos = pos;
        o.resources = std::move(res);
        return gs.addMapObject(o);
    }
    MapObjectId artifact(Pos pos, const QString& id) {
        MapObject o;
        o.kind = MapObject::Kind::Artifact;
        o.pos = pos;
        o.artifact = id;
        return gs.addMapObject(o);
    }

private slots:
    void initTestCase() {
        QStringList errors;
        QVERIFY2(reg.loadFromDirectory(KOT_DATA_DIR, &errors), qPrintable(errors.join("\n")));
    }
    void init() {
        gs = GameState(5);
        gs.setMap(GameMap(20, 20, "grass"));
        p1 = gs.addPlayer("A", gs.newTeam(), Player::Controller::Human, "humans", "#ff0000");
        p2 = gs.addPlayer("B", gs.newTeam(), Player::Controller::Human, "humans", "#0000ff");
        gs.setTurnOrder({p1, p2});
    }

    void artifactsLoad() {
        QVERIFY(reg.artifacts().size() >= 10);
        QCOMPARE(reg.artifact("iron_sword")->traits.value("attack"), 2);
    }

    void treasureGoesToThePlayer() {
        const HeroId h = createHero(gs, reg, p1, "H", "knight", {0, 0});
        gs.hero(h)->movementPoints = 1000;
        const MapObjectId t = treasure({3, 0}, {{"gold", 700}, {"gems", 2}});
        const int gold = gs.player(p1)->resources.value("gold");
        const MoveResult r = moveHero(gs, reg, h, {3, 0});
        QVERIFY2(r.ok && r.arrived, qPrintable(r.error));
        QCOMPARE(r.foundResources.value("gold"), 700);
        QCOMPARE(gs.player(p1)->resources.value("gold"), gold + 700);
        QCOMPARE(gs.player(p1)->resources.value("gems"), 2);
        QVERIFY(!gs.mapObjects().contains(t));
    }

    void artifactsBoostTheHero() {
        const HeroId h = createHero(gs, reg, p1, "H", "knight", {0, 0});
        gs.hero(h)->movementPoints = 1000;
        const int attack = gs.hero(h)->trait("attack");
        artifact({2, 0}, "dragon_blade");
        const MoveResult r = moveHero(gs, reg, h, {2, 0});
        QVERIFY(r.ok && r.arrived);
        QCOMPARE(r.foundArtifacts, QStringList({"dragon_blade"}));
        QCOMPARE(gs.hero(h)->artifacts, QStringList({"dragon_blade"}));
        QCOMPARE(gs.hero(h)->trait("attack"), attack + 4);
        QVERIFY(gs.mapObjects().isEmpty());
    }

    void aSecondWeaponGoesToTheBackpack() {
        for (const ArtifactDef& a : reg.artifacts()) QVERIFY2(artifactSlotCount(a.slot) > 0, qPrintable(a.id));
        const HeroId h = createHero(gs, reg, p1, "H", "knight", {0, 0});
        gs.hero(h)->movementPoints = 2000;
        const int attack = gs.hero(h)->trait("attack");
        artifact({2, 0}, "iron_sword");
        artifact({4, 0}, "dragon_blade");
        QVERIFY(moveHero(gs, reg, h, {2, 0}).ok);
        const MoveResult second = moveHero(gs, reg, h, {4, 0});
        QVERIFY(second.ok);
        QCOMPARE(second.stowedArtifacts, QStringList({"dragon_blade"}));
        Hero& hero = *gs.hero(h);
        QCOMPARE(hero.artifacts, QStringList({"iron_sword"}));  // the first is worn
        QCOMPARE(hero.backpack, QStringList({"dragon_blade"}));  // the second is carried
        QCOMPARE(hero.trait("attack"), attack + 2);
        // Wearing the blade puts the sword in the backpack and swaps the bonus.
        QString why;
        QVERIFY2(equipArtifact(hero, reg, "dragon_blade", {}, &why), qPrintable(why));
        QCOMPARE(hero.artifacts, QStringList({"dragon_blade"}));
        QCOMPARE(hero.backpack, QStringList({"iron_sword"}));
        QCOMPARE(hero.trait("attack"), attack + 4);
        QVERIFY(unequipArtifact(hero, reg, "dragon_blade"));
        QCOMPARE(hero.trait("attack"), attack);
        QVERIFY(!unequipArtifact(hero, reg, "dragon_blade"));  // not worn any more
        // Saved and loaded with the backpack.
        GameState loaded;
        QStringList errors;
        QVERIFY2(GameState::fromJson(gs.toJson(), reg, &loaded, &errors), qPrintable(errors.join("\n")));
        QCOMPARE(loaded.hero(h)->backpack.size(), 2);
    }

    void twoRingsAreWorn() {
        const HeroId h = createHero(gs, reg, p1, "H", "knight", {0, 0});
        Hero& hero = *gs.hero(h);
        QVERIFY(takeArtifact(hero, reg, "ring_of_vigor"));
        QVERIFY(takeArtifact(hero, reg, "travellers_ring"));
        QVERIFY(!takeArtifact(hero, reg, "ring_of_the_sages"));  // a third ring is carried
        QString why;
        QVERIFY(!equipArtifact(hero, reg, "ring_of_the_sages", "iron_sword", &why));  // not the same place
        QVERIFY(equipArtifact(hero, reg, "ring_of_the_sages", "travellers_ring"));
        QVERIFY(hero.artifacts.contains("ring_of_vigor") && hero.backpack == QStringList({"travellers_ring"}));
    }

    void objectsBlockTheWay() {
        for (int y = 0; y < 20; ++y)
            if (y != 5) gs.map().setTerrain({5, y}, "sea");
        treasure({5, 5}, {{"wood", 5}});
        const HeroId h = createHero(gs, reg, p1, "H", "knight", {0, 5});
        gs.hero(h)->movementPoints = 5000;
        const MoveResult r = moveHero(gs, reg, h, {9, 5});
        QVERIFY(!r.ok);  // the only way across is the treasure tile
    }

    void savedAndShownOnlyInSight() {
        treasure({1, 1}, {{"gold", 500}});
        artifact({18, 18}, "spyglass");
        createHero(gs, reg, p1, "H", "knight", {0, 0});
        GameState loaded;
        QStringList errors;
        QVERIFY2(GameState::fromJson(gs.toJson(), reg, &loaded, &errors), qPrintable(errors.join("\n")));
        QCOMPARE(loaded.mapObjects().size(), 2);
        const QJsonArray seen = buildPlayerView(gs, reg, p1)["mapObjects"].toArray();
        QCOMPARE(seen.size(), 1);  // the far artifact is out of sight
        QCOMPARE(seen[0].toObject()["kind"].toString(), QString("treasure"));
    }

    void setupPlacesGuardedTreasures() {
        GameSetupParams params;
        params.seed = 3;
        params.players = {{"A", false, {}, "humans", 1, "#ff0000"}, {"B", false, {}, "undead", 2, "#00ff00"}};
        GameState g;
        QString error;
        QVERIFY2(createGame(reg, params, &g, nullptr, &error), qPrintable(error));
        int treasures = 0, artifacts = 0;
        for (const MapObject& o : g.mapObjects()) {
            (o.kind == MapObject::Kind::Artifact ? artifacts : treasures)++;
            if (o.kind != MapObject::Kind::Artifact) continue;
            bool guarded = false;  // a neutral army right next to it
            for (const Army& a : g.armies())
                guarded = guarded || (!a.owner.isValid() && std::max(std::abs(a.pos.x - o.pos.x), std::abs(a.pos.y - o.pos.y)) == 1);
            QVERIFY(guarded);
        }
        QVERIFY(treasures >= 4);
        QVERIFY(artifacts >= 2);
        int guardedMines = 0;
        for (const Mine& m : g.mines())
            for (const Army& a : g.armies())
                guardedMines += (!a.owner.isValid() && std::max(std::abs(a.pos.x - m.pos.x), std::abs(a.pos.y - m.pos.y)) == 1);
        QVERIFY(guardedMines >= g.mines().size() / 2);
    }
};

QTEST_GUILESS_MAIN(TstMapObjects)
#include "tst_mapobjects.moc"
