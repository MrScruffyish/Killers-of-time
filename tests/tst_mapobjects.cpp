#include <QJsonArray>
#include <QtTest>

#include "TestContent.h"

#include "core/content/ContentRegistry.h"
#include "core/model/PlayerView.h"
#include "core/rules/ArmyRules.h"
#include "core/rules/GameSetup.h"
#include "core/rules/HeroRules.h"
#include "core/rules/MapSites.h"
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
        o.artifact = A(qPrintable(id));
        return gs.addMapObject(o);
    }

private slots:
    void initTestCase() {
        QStringList errors;
        QVERIFY2(reg.loadFromDirectory(KOT_DATA_DIR, &errors), qPrintable(errors.join("\n")));
        useContent(reg);
    }
    void init() {
        gs = GameState(5);
        gs.setMap(GameMap(20, 20, T("grass")));
        p1 = gs.addPlayer("A", gs.newTeam(), Player::Controller::Human, RC("humans"), "#ff0000");
        p2 = gs.addPlayer("B", gs.newTeam(), Player::Controller::Human, RC("humans"), "#0000ff");
        gs.setTurnOrder({p1, p2});
    }

    void artifactsLoad() {
        QVERIFY(reg.artifacts().size() >= 10);
        QCOMPARE(reg.artifact(A("iron_sword"))->traits.value(TR("attack")), 2);
    }

    void treasureGoesToThePlayer() {
        const HeroId h = createHero(gs, reg, p1, "H", C("knight"), {0, 0});
        gs.hero(h)->movementPoints = 1000;
        const MapObjectId t = treasure({3, 0}, amounts({{"gold", 700}, {"gems", 2}}));
        const int gold = gs.player(p1)->resources.value(R("gold"));
        const MoveResult r = moveHero(gs, reg, h, {3, 0});
        QVERIFY2(r.ok && r.arrived, qPrintable(r.error));
        QCOMPARE(r.foundResources.value(R("gold")), 700);
        QCOMPARE(gs.player(p1)->resources.value(R("gold")), gold + 700);
        QCOMPARE(gs.player(p1)->resources.value(R("gems")), 2);
        QVERIFY(!gs.mapObjects().contains(t));
    }

    void artifactsBoostTheHero() {
        const HeroId h = createHero(gs, reg, p1, "H", C("knight"), {0, 0});
        gs.hero(h)->movementPoints = 1000;
        const int attack = gs.hero(h)->trait(TR("attack"));
        artifact({2, 0}, "dragon_blade");
        const MoveResult r = moveHero(gs, reg, h, {2, 0});
        QVERIFY(r.ok && r.arrived);
        QCOMPARE(r.foundArtifacts, QList<ArtifactId>({A("dragon_blade")}));
        QCOMPARE(gs.hero(h)->artifacts, QList<ArtifactId>({A("dragon_blade")}));
        QCOMPARE(gs.hero(h)->trait(TR("attack")), attack + 4);
        QVERIFY(gs.mapObjects().isEmpty());
    }

    void aSecondWeaponGoesToTheBackpack() {
        for (const ArtifactDef& a : reg.artifacts()) QVERIFY2(artifactSlotCount(a.slot) > 0, qPrintable(a.key));
        const HeroId h = createHero(gs, reg, p1, "H", C("knight"), {0, 0});
        gs.hero(h)->movementPoints = 2000;
        const int attack = gs.hero(h)->trait(TR("attack"));
        artifact({2, 0}, "iron_sword");
        artifact({4, 0}, "dragon_blade");
        QVERIFY(moveHero(gs, reg, h, {2, 0}).ok);
        const MoveResult second = moveHero(gs, reg, h, {4, 0});
        QVERIFY(second.ok);
        QCOMPARE(second.stowedArtifacts, QList<ArtifactId>({A("dragon_blade")}));
        Hero& hero = *gs.hero(h);
        QCOMPARE(hero.artifacts, QList<ArtifactId>({A("iron_sword")}));  // the first is worn
        QCOMPARE(hero.backpack, QList<ArtifactId>({A("dragon_blade")}));  // the second is carried
        QCOMPARE(hero.trait(TR("attack")), attack + 2);
        // Wearing the blade puts the sword in the backpack and swaps the bonus.
        QString why;
        QVERIFY2(equipArtifact(hero, reg, A("dragon_blade"), {}, &why), qPrintable(why));
        QCOMPARE(hero.artifacts, QList<ArtifactId>({A("dragon_blade")}));
        QCOMPARE(hero.backpack, QList<ArtifactId>({A("iron_sword")}));
        QCOMPARE(hero.trait(TR("attack")), attack + 4);
        QVERIFY(unequipArtifact(hero, reg, A("dragon_blade")));
        QCOMPARE(hero.trait(TR("attack")), attack);
        QVERIFY(!unequipArtifact(hero, reg, A("dragon_blade")));  // not worn any more
        // Saved and loaded with the backpack.
        GameState loaded;
        QStringList errors;
        QVERIFY2(GameState::fromJson(gs.toJson(reg), reg, &loaded, &errors), qPrintable(errors.join("\n")));
        QCOMPARE(loaded.hero(h)->backpack.size(), 2);
    }

    void heroesSideBySideSwapArtifactsAndUnits() {
        const HeroId a = createHero(gs, reg, p1, "A", C("knight"), {5, 5});
        const HeroId b = createHero(gs, reg, p1, "B", C("knight"), {6, 6});  // diagonal neighbour
        const int attack = gs.hero(b)->trait(TR("attack"));
        QVERIFY(takeArtifact(*gs.hero(a), reg, A("iron_sword")));  // worn
        QString why;
        QVERIFY2(giveArtifact(gs, reg, a, b, A("iron_sword"), &why), qPrintable(why));
        QVERIFY(gs.hero(a)->artifacts.isEmpty());
        QCOMPARE(gs.hero(b)->artifacts, QList<ArtifactId>({A("iron_sword")}));  // worn by the other, with its bonus
        QCOMPARE(gs.hero(b)->trait(TR("attack")), attack + 2);
        QVERIFY(!giveArtifact(gs, reg, a, b, A("iron_sword"), &why));  // a has none any more
        // Units too.
        gs.army(gs.hero(a)->army)->addUnits(U("militia"), 5, 6);
        QVERIFY(moveUnits(gs, gs.hero(a)->army, gs.hero(b)->army, 0, 5).ok);
        // Not when they stand apart, nor with someone else's hero.
        gs.army(gs.hero(b)->army)->pos = {8, 6};
        QVERIFY(!giveArtifact(gs, reg, b, a, A("iron_sword"), &why));
        QVERIFY(why.contains("side by side"));
        QVERIFY(!moveUnits(gs, gs.hero(b)->army, gs.hero(a)->army, 0, 1).ok);
        const HeroId enemy = createHero(gs, reg, p2, "E", C("knight"), {7, 6});
        QVERIFY(!giveArtifact(gs, reg, b, enemy, A("iron_sword"), &why));
        QVERIFY(why.contains("different players"));
    }

    void twoRingsAreWorn() {
        const HeroId h = createHero(gs, reg, p1, "H", C("knight"), {0, 0});
        Hero& hero = *gs.hero(h);
        QVERIFY(takeArtifact(hero, reg, A("ring_of_vigor")));
        QVERIFY(takeArtifact(hero, reg, A("travellers_ring")));
        QVERIFY(!takeArtifact(hero, reg, A("ring_of_the_sages")));  // a third ring is carried
        QString why;
        QVERIFY(!equipArtifact(hero, reg, A("ring_of_the_sages"), A("iron_sword"), &why));  // not the same place
        QVERIFY(equipArtifact(hero, reg, A("ring_of_the_sages"), A("travellers_ring")));
        QVERIFY(hero.artifacts.contains(A("ring_of_vigor")) && hero.backpack == QList<ArtifactId>({A("travellers_ring")}));
    }

    void sitesGiveEachHeroOnce() {
        const HeroId h = createHero(gs, reg, p1, "H", C("knight"), {0, 0});
        gs.hero(h)->movementPoints = 5000;
        const auto site = [&](Pos pos, const QString& id) {
            MapObject o;
            o.kind = MapObject::Kind::Site;
            o.pos = pos;
            o.site = id;
            return gs.addMapObject(o);
        };
        const MapObjectId arena = site({3, 0}, "arena");
        const int att = gs.hero(h)->trait(TR("attack")), def = gs.hero(h)->trait(TR("defense"));
        MoveResult r = moveHero(gs, reg, h, {3, 0});
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(r.visitedSite, QString("arena"));
        QCOMPARE(gs.hero(h)->trait(TR("attack")) + gs.hero(h)->trait(TR("defense")), att + def + 1);
        QVERIFY(gs.mapObjects().contains(arena));  // it stays
        QVERIFY(!siteGivesSomething(gs, gs.mapObjects()[arena], *gs.hero(h)));
        // The well: once a day.
        const MapObjectId well = site({6, 0}, "magic_well");
        gs.hero(h)->spellPoints = 0;
        QVERIFY(moveHero(gs, reg, h, {6, 0}).ok);
        QCOMPARE(gs.hero(h)->spellPoints, maxSpellPoints(*gs.hero(h), reg));
        QVERIFY(!siteGivesSomething(gs, gs.mapObjects()[well], *gs.hero(h)));
        gs.setDay(gs.day() + 1);
        QVERIFY(siteGivesSomething(gs, gs.mapObjects()[well], *gs.hero(h)));
        // The shrine teaches a spell; saved with who visited.
        site({9, 0}, "shrine");
        const qsizetype spells = gs.hero(h)->spells.size();
        r = moveHero(gs, reg, h, {9, 0});
        QVERIFY(r.siteResult.startsWith("learned"));
        QCOMPARE(gs.hero(h)->spells.size(), spells + 1);
        GameState loaded;
        QStringList errors;
        QVERIFY2(GameState::fromJson(gs.toJson(reg), reg, &loaded, &errors), qPrintable(errors.join("\n")));
        QVERIFY(!siteGivesSomething(loaded, loaded.mapObjects()[arena], *loaded.hero(h)));
    }

    // Quest huts: the hermit asks once (a level, resources or an artifact) and pays the first who brings it.
    void questHutsPayOnce() {
        const HeroId h = createHero(gs, reg, p1, "H", C("knight"), {0, 0});
        gs.hero(h)->movementPoints = 5000;
        const auto hut = [&](Pos pos, Quest q) {
            MapObject o;
            o.kind = MapObject::Kind::Site;
            o.pos = pos;
            o.site = QStringLiteral("quest_hut");
            o.quest = std::move(q);
            return gs.addMapObject(o);
        };
        Quest levelQuest;
        levelQuest.needLevel = 3;
        levelQuest.rewardResources = amounts({{"gold", 2500}});
        levelQuest.rewardArtifact = A("iron_sword");
        const MapObjectId a = hut({3, 0}, levelQuest);
        QVERIFY(!siteGivesSomething(gs, gs.mapObjects()[a], *gs.hero(h)));
        MoveResult r = moveHero(gs, reg, h, {3, 0});
        QVERIFY2(r.ok, qPrintable(r.error));
        QVERIFY2(r.siteResult.contains("level 3"), qPrintable(r.siteResult));  // he hears the wish
        const int gold = gs.player(p1)->resources.value(R("gold"));
        gs.hero(h)->level = 3;
        gs.hero(h)->movementPoints = 5000;
        QVERIFY(moveHero(gs, reg, h, {4, 0}).ok);
        r = moveHero(gs, reg, h, {3, 0});
        QVERIFY2(r.siteResult.contains("granted"), qPrintable(r.siteResult));
        QCOMPARE(gs.player(p1)->resources.value(R("gold")), gold + 2500);
        QVERIFY(gs.hero(h)->artifacts.contains(A("iron_sword")) || gs.hero(h)->backpack.contains(A("iron_sword")));
        QVERIFY(gs.mapObjects()[a].quest.done);
        QVERIFY(!siteGivesSomething(gs, gs.mapObjects()[a], *gs.hero(h)));

        // An artifact asked for is given up; resources are paid.
        Quest bring;
        bring.needArtifact = A("iron_sword");
        bring.rewardExperience = 1000;
        const MapObjectId b = hut({6, 0}, bring);
        QVERIFY(siteGivesSomething(gs, gs.mapObjects()[b], *gs.hero(h)));
        const int xp = gs.hero(h)->experience;
        gs.hero(h)->movementPoints = 5000;
        QVERIFY(moveHero(gs, reg, h, {6, 0}).ok);
        QVERIFY(!gs.hero(h)->artifacts.contains(A("iron_sword")) && !gs.hero(h)->backpack.contains(A("iron_sword")));
        QCOMPARE(gs.hero(h)->experience, xp + 1000);
        Quest pay;
        pay.needResources = amounts({{"wood", 5}});
        pay.rewardResources = amounts({{"gold", 1000}});
        const MapObjectId c = hut({9, 0}, pay);
        gs.player(p1)->resources[R("wood")] = 7;
        gs.hero(h)->movementPoints = 5000;
        QVERIFY(moveHero(gs, reg, h, {9, 0}).ok);
        QCOMPARE(gs.player(p1)->resources.value(R("wood")), 2);

        // Saved, and the view tells the wish.
        GameState loaded;
        QStringList errors;
        QVERIFY2(GameState::fromJson(gs.toJson(reg), reg, &loaded, &errors), qPrintable(errors.join("\n")));
        QVERIFY(loaded.mapObjects()[a].quest.done);
        QCOMPARE(loaded.mapObjects()[c].quest.needResources, pay.needResources);
        QVERIFY(!describeQuest(pay, reg).isEmpty());
    }

    void objectsBlockTheWay() {
        for (int y = 0; y < 20; ++y)
            if (y != 5) gs.map().setTerrain({5, y}, T("sea"));
        treasure({5, 5}, amounts({{"wood", 5}}));
        const HeroId h = createHero(gs, reg, p1, "H", C("knight"), {0, 5});
        gs.hero(h)->movementPoints = 5000;
        const MoveResult r = moveHero(gs, reg, h, {9, 5});
        QVERIFY(!r.ok);  // the only way across is the treasure tile
    }

    void savedAndShownOnlyInSight() {
        treasure({1, 1}, amounts({{"gold", 500}}));
        artifact({18, 18}, "spyglass");
        createHero(gs, reg, p1, "H", C("knight"), {0, 0});
        GameState loaded;
        QStringList errors;
        QVERIFY2(GameState::fromJson(gs.toJson(reg), reg, &loaded, &errors), qPrintable(errors.join("\n")));
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
