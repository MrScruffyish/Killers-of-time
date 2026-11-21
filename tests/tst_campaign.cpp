#include <QDir>
#include <QJsonArray>
#include <QTemporaryDir>
#include <QtTest>
#include <cstdlib>

#include "TestContent.h"

#include "core/content/ContentRegistry.h"
#include "core/rules/Campaign.h"
#include "core/rules/HeroRules.h"
#include "core/rules/Scenario.h"
#include "net/Protocol.h"
#include "server/GameSession.h"

using namespace kot;
using namespace kot::core;

class TstCampaign : public QObject {
    Q_OBJECT
    ContentRegistry reg;

    static net::SlotInfo slot(int index, bool ai, const QString& name, int team) {
        net::SlotInfo s;
        s.index = index;
        s.kind = ai ? net::SlotKind::Ai : net::SlotKind::Human;
        s.clientId = ai ? 0 : 1;
        s.name = name;
        s.race = "humans";
        s.team = team;
        s.color = QStringList{"#d62728", "#1f77b4", "#2ca02c", "#ff7f0e"}[index];
        s.ready = true;
        s.aiProfile = ai ? "passive" : "";
        return s;
    }

private slots:
    void initTestCase() {
        QStringList errors;
        QVERIFY2(reg.loadFromDirectory(KOT_DATA_DIR, &errors), qPrintable(errors.join("\n")));
        useContent(reg);
    }

    void campaignsLoad() {
        const QList<CampaignDef> list = listCampaigns(QDir(KOT_DATA_DIR).filePath("campaigns"));
        QVERIFY(!list.isEmpty());
        for (const CampaignDef& c : list)
            for (const QString& s : c.scenarios)
                QVERIFY2(QFile::exists(QDir(KOT_DATA_DIR).filePath("scenarios/" + s + ".json")), qPrintable(c.id + ": " + s));
    }

    void theGreatWarStarts() {
        QTemporaryDir saves;
        net::LobbyState lobby;
        lobby.settings.campaign = "great_war";
        lobby.slotList = {slot(0, false, "Ann", 1), slot(1, true, "Bot", 2)};
        server::GameSession s(reg);
        s.setCampaignDir(saves.path());
        QString error;
        QVERIFY2(s.start(lobby, &error), qPrintable(error));
        QCOMPARE(s.state().map().width(), 56);  // mountain_kings
        QCOMPARE(s.state().player(s.playerOfClient(1))->race, RC("dwarves"));
        QCOMPARE(s.state().victory().type, VictoryCondition::Type::HoldTown);
    }

    void heroesCarryOver() {
        GameState a(1);
        a.setMap(GameMap(8, 8, T("grass")));
        const PlayerId pa = a.addPlayer("A", a.newTeam(), Player::Controller::Human, RC("humans"), "#ff0000");
        const HeroId h = createHero(a, reg, pa, "Veteran", C("wizard"), {1, 1});
        addExperience(*a.hero(h), reg, 5000);
        a.hero(h)->artifacts << A("iron_sword");
        a.hero(h)->spells << S("far_sight");
        a.army(a.hero(h)->army)->addUnits(U("pikeman"), 25, 10);
        a.army(a.hero(h)->army)->stacks[0].setExperience(70);
        const QJsonObject snap = heroSnapshot(a, reg, h);
        // Only identity and level are recorded: no army, spells or loot to grow safely.
        QStringList keys = snap.keys();
        keys.sort();
        QCOMPARE(keys, QStringList({"class", "level", "name"}));
        QCOMPARE(snap.value("name").toString(), QString("Veteran"));
        QCOMPARE(snap.value("level").toInt(), a.hero(h)->level);

        GameState b(2);
        b.setMap(GameMap(8, 8, T("grass")));
        const PlayerId pb = b.addPlayer("B", b.newTeam(), Player::Controller::Human, RC("undead"), "#ff0000");
        const HeroId fresh = createHero(b, reg, pb, "Rookie", C("death_knight"), {2, 2});
        b.army(b.hero(fresh)->army)->addUnits(U("pikeman"), 7, 10);  // the chapter's own army
        QCOMPARE(applyCarriedHeroes(b, reg, pb, {snap}, 0), 1);
        const Hero* now = b.hero(fresh);
        QCOMPARE(now->name, QString("Veteran"));
        QCOMPARE(now->classId, C("wizard"));
        QCOMPARE(now->level, a.hero(h)->level);
        QCOMPARE(now->experience, 0);
        QCOMPARE(now->pendingTraitPicks, 0);
        QVERIFY(now->spells.isEmpty());     // the chapter's spells, not the carried ones
        QVERIFY(now->artifacts.isEmpty());  // likewise the carried loot
        QVERIFY(now->backpack.isEmpty());
        QCOMPARE(b.army(now->army)->stacks.size(), 1);
        QCOMPARE(b.army(now->army)->stacks[0].count(), 7);
        // Traits follow the carried class and level (wizard: +8 spell points per level).
        QCOMPARE(now->trait(TR("spell_points_max")), 30 + 8 * (now->level - 1));
        QCOMPARE(bestHeroes(a, pa, 1), QList<HeroId>({h}));
    }

    void carriedLevelCanBeCapped() {
        GameState a(1);
        a.setMap(GameMap(8, 8, T("grass")));
        const PlayerId pa = a.addPlayer("A", a.newTeam(), Player::Controller::Human, RC("humans"), "#ff0000");
        const HeroId h = createHero(a, reg, pa, "Veteran", C("wizard"), {1, 1});
        a.hero(h)->level = 9;
        QJsonObject old = heroSnapshot(a, reg, h);
        // Old progress files may still hold these fields; they are ignored.
        old.insert("experience", 12770);
        old.insert("army", QJsonArray{QJsonObject{{"unit", "pikeman"}, {"count", 99}}});
        old.insert("spells", QJsonArray{"far_sight"});

        GameState b(2);
        b.setMap(GameMap(8, 8, T("grass")));
        const PlayerId pb = b.addPlayer("B", b.newTeam(), Player::Controller::Human, RC("undead"), "#ff0000");
        const HeroId fresh = createHero(b, reg, pb, "Rookie", C("death_knight"), {2, 2});
        QCOMPARE(applyCarriedHeroes(b, reg, pb, {old}, 5), 1);
        QCOMPARE(b.hero(fresh)->level, 5);
        QCOMPARE(b.hero(fresh)->experience, 0);
        QVERIFY(b.hero(fresh)->spells.isEmpty());
        QVERIFY(b.army(b.hero(fresh)->army)->stacks.isEmpty());
    }

    void goalArtifactsAreGivenOnce() {
        GameState g(1);
        g.setMap(GameMap(8, 8, T("grass")));
        const PlayerId p = g.addPlayer("A", g.newTeam(), Player::Controller::Human, RC("humans"), "#ff0000");
        const HeroId h = createHero(g, reg, p, "Hero", C("wizard"), {1, 1});
        QCOMPARE(giveGoalArtifacts(g, reg, p, {"dragon_blade", "iron_sword"}), 2);
        const Hero* hero = g.hero(h);
        QVERIFY(hero->artifacts.contains(A("dragon_blade")) || hero->backpack.contains(A("dragon_blade")));
        QVERIFY(hero->artifacts.contains(A("iron_sword")) || hero->backpack.contains(A("iron_sword")));
        QCOMPARE(giveGoalArtifacts(g, reg, p, {"dragon_blade"}), 0);  // no duplicates
        if (hero->artifacts.contains(A("dragon_blade")))
            QCOMPARE(hero->trait(TR("attack")), 1 + 4);  // wizard base 1 + blade 4
    }

    void goalArtifactCarriesToNextChapter() {
        QTemporaryDir saves;
        const QString progressPath = QDir(saves.path()).filePath("campaign_intrinsic-1.json");
        QString error;
        // Start straight at chapter 2 (index 1), the Dragon Blade chapter.
        CampaignProgress seed;
        seed.campaign = "intrinsic-1";
        seed.next = 1;
        QVERIFY(saveProgress(progressPath, seed, &error));

        net::LobbyState lobby;
        lobby.settings.campaign = "intrinsic-1";
        lobby.slotList = {slot(0, false, "Ann", 1), slot(1, true, "Bot", 2), slot(2, true, "Bot2", 3)};
        server::GameSession s(reg);
        s.setCampaignDir(saves.path());
        QVERIFY2(s.start(lobby, &error), qPrintable(error));
        GameState& gs = s.state();
        const PlayerId ann = s.playerOfClient(1);
        HeroId annHero;
        for (const Hero& h : gs.heroes())
            if (h.owner == ann) annHero = h.id;
        QVERIFY(annHero.isValid());
        const ArtifactId prize = gs.victory().targetArtifact;
        QVERIFY(prize.isValid());
        takeArtifact(*gs.hero(annHero), reg, prize);
        QVERIFY(gs.hero(annHero)->artifacts.contains(prize) || gs.hero(annHero)->backpack.contains(prize));
        takeArtifact(*gs.hero(annHero), reg, A("iron_sword"));  // ordinary loot, must not carry
        for (const Hero& h : QList<Hero>(gs.heroes().values()))
            if (h.owner != ann) {
                gs.armies().remove(h.army);
                gs.heroes().remove(h.id);
            }
        for (Town& t : gs.towns())
            if (t.owner != ann) t.owner = PlayerId();
        s.recordCampaignResult();
        CampaignProgress p;
        QVERIFY(loadProgress(progressPath, &p));
        QCOMPARE(p.next, 2);
        QCOMPARE(p.artifacts, QStringList({QStringLiteral("dragon_blade")}));

        // Chapter 3: the carried hero brings the Dragon Blade but not the ordinary sword.
        lobby.slotList = {slot(0, false, "Ann", 1), slot(1, true, "Bot", 2), slot(2, true, "Bot2", 3),
                          slot(3, true, "Bot3", 4)};
        server::GameSession next(reg);
        next.setCampaignDir(saves.path());
        QVERIFY2(next.start(lobby, &error), qPrintable(error));
        const PlayerId ann2 = next.playerOfClient(1);
        bool hasPrize = false, hasLoot = false;
        for (const Hero& h : next.state().heroes())
            if (h.owner == ann2) {
                hasPrize = hasPrize || h.artifacts.contains(prize) || h.backpack.contains(prize);
                hasLoot = hasLoot || h.artifacts.contains(A("iron_sword")) || h.backpack.contains(A("iron_sword"));
            }
        QVERIFY(hasPrize);
        QVERIFY(!hasLoot);
    }

    void chaptersFollowEachOther() {
        QTemporaryDir saves;
        net::LobbyState lobby;
        lobby.settings.campaign = "first_war";
        lobby.slotList = {slot(0, false, "Ann", 1), slot(1, true, "Bot", 2)};
        server::GameSession first(reg);
        first.setCampaignDir(saves.path());
        QString error;
        QVERIFY2(first.start(lobby, &error), qPrintable(error));
        QCOMPARE(first.state().map().width(), 48);  // two_kingdoms
        QCOMPARE(first.viewFor(first.playerOfClient(1))["campaign"].toObject()["chapter"].toInt(), 1);

        // Ann's hero grows, then the enemy is gone: the chapter is won.
        GameState& gs = first.state();
        const PlayerId ann = first.playerOfClient(1);
        HeroId annHero;
        for (const Hero& h : gs.heroes())
            if (h.owner == ann) annHero = h.id;
        gs.hero(annHero)->name = "Champion";
        addExperience(*gs.hero(annHero), reg, 3000);
        for (const Hero& h : QList<Hero>(gs.heroes().values()))
            if (h.owner != ann) {
                gs.armies().remove(h.army);
                gs.heroes().remove(h.id);
            }
        for (Town& t : gs.towns())
            if (t.owner != ann) t.owner = PlayerId();
        first.handle(1, net::Message{net::msg::endTurn, {}, 1});  // refused (game over), but recorded
        QCOMPARE(first.viewFor(ann)["campaign"].toObject()["result"].toString(), QString("won"));
        CampaignProgress p;
        QVERIFY(loadProgress(QDir(saves.path()).filePath("campaign_first_war.json"), &p));
        QCOMPARE(p.next, 1);
        QCOMPARE(p.heroes.size(), 1);
        QCOMPARE(p.heroes[0]["name"].toString(), QString("Champion"));

        // Chapter two: three realms, three players; the champion leads again.
        lobby.slotList = {slot(0, false, "Ann", 1), slot(1, true, "Bot", 2), slot(2, true, "Bot2", 3)};
        server::GameSession second(reg);
        second.setCampaignDir(saves.path());
        QVERIFY2(second.start(lobby, &error), qPrintable(error));
        QCOMPARE(second.state().map().width(), 64);
        bool champion = false;
        for (const Hero& h : second.state().heroes())
            champion = champion || (h.owner == second.playerOfClient(1) && h.name == "Champion" && h.level > 1);
        QVERIFY(champion);
        QCOMPARE(second.viewFor(second.playerOfClient(1))["campaign"].toObject()["chapter"].toInt(), 2);
    }
    void intrinsicCampaignIsShipped() {
        CampaignDef def;
        QStringList errors;
        QVERIFY2(loadCampaign(QDir(KOT_DATA_DIR).filePath(QStringLiteral("campaigns/intrinsic-1.json")), &def, &errors),
                 qPrintable(errors.join("\n")));
        QCOMPARE(def.id, QStringLiteral("intrinsic-1"));
        QCOMPARE(def.name, QStringLiteral("The Purple Scourge"));
        QCOMPARE(def.scenarios.size(), 5);
        QCOMPARE(def.carryHeroes, 1);
        QCOMPARE(def.maxLevel, 0);
        const QList<VictoryCondition::Type> expected = {
            VictoryCondition::Type::SlayMonster, VictoryCondition::Type::FindArtifact, VictoryCondition::Type::ExploreMap,
            VictoryCondition::Type::Days, VictoryCondition::Type::Conquest};
        QList<GameState> games;
        for (int c = 0; c < def.scenarios.size(); ++c) {
            Scenario s;
            QVERIFY2(loadScenario(QDir(KOT_DATA_DIR).filePath(QStringLiteral("scenarios/%1.json").arg(def.scenarios[c])), reg, &s,
                                  &errors),
                     qPrintable(errors.join("\n")));
            GameState gs;
            QVERIFY2(GameState::fromJson(s.game, reg, &gs, &errors), qPrintable(errors.join("\n")));
            QCOMPARE(gs.victory().type, expected[c]);
            games << gs;
        }
        QVERIFY(games[0].army(games[0].victory().targetArmy));
        const ArtifactId prize = games[1].victory().targetArtifact;
        QVERIFY(prize.isValid());
        // The chapter 2 prize must be a real expedition, not a pickup beside a starting
        // town: it should take at least 20 tiles to reach from every player's capital.
        bool placed = false;
        for (const MapObject& o : games[1].mapObjects()) {
            if (o.kind != MapObject::Kind::Artifact || o.artifact != prize) continue;
            placed = true;
            for (const Town& t : games[1].towns()) {
                if (!t.owner.isValid()) continue;
                const int dx = o.pos.x - t.pos.x, dy = o.pos.y - t.pos.y;
                const int distance = std::abs(dx) + std::abs(dy);
                QVERIFY2(distance >= 20,
                         qPrintable(QStringLiteral("goal artifact is only %1 tiles from a capital").arg(distance)));
            }
        }
        QVERIFY(placed);
        for (const GameState& gs : games)
            for (const MapObject& o : gs.mapObjects()) {
                QVERIFY2(!(o.kind == MapObject::Kind::Artifact && o.artifact == prize)
                             || gs.victory().targetArtifact == prize, "another chapter holds the campaign prize");
                QVERIFY2(!(o.site == QLatin1String("quest_hut") && o.quest.rewardArtifact == prize)
                             || gs.victory().targetArtifact == prize, "another quest gives the campaign prize");
            }
    }

    void allAiCampaignCarriesTheFirstSeat() {
        QTemporaryDir saves;
        net::LobbyState lobby;
        lobby.settings.campaign = "first_war";
        lobby.slotList = {slot(0, true, "Bot", 1), slot(1, true, "Bot2", 2)};
        server::GameSession s(reg);
        s.setCampaignDir(saves.path());
        QString error;
        QVERIFY2(s.start(lobby, &error), qPrintable(error));
        QCOMPARE(s.campaignChapter(), 0);

        // Win chapter one for the first AI seat and record it the way the playtest tool does.
        GameState& gs = s.state();
        const PlayerId first = gs.turnOrder().first();
        for (const Hero& h : QList<Hero>(gs.heroes().values()))
            if (h.owner != first) {
                gs.armies().remove(h.army);
                gs.heroes().remove(h.id);
            }
        for (Town& t : gs.towns())
            if (t.owner != first) t.owner = PlayerId();
        s.recordCampaignResult();
        CampaignProgress p;
        QVERIFY(loadProgress(QDir(saves.path()).filePath("campaign_first_war.json"), &p));
        QCOMPARE(p.next, 1);
        QVERIFY2(!p.heroes.isEmpty(), "the first AI seat should be the campaign player");
    }
};

QTEST_GUILESS_MAIN(TstCampaign)
#include "tst_campaign.moc"
