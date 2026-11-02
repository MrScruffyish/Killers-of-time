#include <QDir>
#include <QTemporaryDir>
#include <QtTest>

#include "core/content/ContentRegistry.h"
#include "core/rules/Campaign.h"
#include "core/rules/HeroRules.h"
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
        s.color = QStringList{"#d62728", "#1f77b4", "#2ca02c"}[index];
        s.ready = true;
        s.aiProfile = ai ? "passive" : "";
        return s;
    }

private slots:
    void initTestCase() {
        QStringList errors;
        QVERIFY2(reg.loadFromDirectory(KOT_DATA_DIR, &errors), qPrintable(errors.join("\n")));
    }

    void campaignsLoad() {
        const QList<CampaignDef> list = listCampaigns(QDir(KOT_DATA_DIR).filePath("campaigns"));
        QVERIFY(!list.isEmpty());
        for (const CampaignDef& c : list)
            for (const QString& s : c.scenarios)
                QVERIFY2(QFile::exists(QDir(KOT_DATA_DIR).filePath("scenarios/" + s + ".json")), qPrintable(c.id + ": " + s));
    }

    void heroesCarryOver() {
        GameState a(1);
        a.setMap(GameMap(8, 8, "grass"));
        const PlayerId pa = a.addPlayer("A", a.newTeam(), Player::Controller::Human, "humans", "#ff0000");
        const HeroId h = createHero(a, reg, pa, "Veteran", "wizard", {1, 1});
        addExperience(*a.hero(h), reg, 5000);
        a.hero(h)->artifacts << "iron_sword";
        a.hero(h)->spells << "far_sight";
        a.army(a.hero(h)->army)->addUnits("pikeman", 25, 10);
        a.army(a.hero(h)->army)->stacks[0].setExperience(70);
        const QJsonObject snap = heroSnapshot(a, h);

        GameState b(2);
        b.setMap(GameMap(8, 8, "grass"));
        const PlayerId pb = b.addPlayer("B", b.newTeam(), Player::Controller::Human, "undead", "#ff0000");
        const HeroId fresh = createHero(b, reg, pb, "Rookie", "death_knight", {2, 2});
        QCOMPARE(applyCarriedHeroes(b, reg, pb, {snap}, true), 1);
        const Hero* now = b.hero(fresh);
        QCOMPARE(now->name, QString("Veteran"));
        QCOMPARE(now->classId, QString("wizard"));
        QCOMPARE(now->level, a.hero(h)->level);
        QVERIFY(now->artifacts.contains("iron_sword"));
        QVERIFY(now->spells.contains("far_sight"));
        QCOMPARE(b.army(now->army)->stacks[0].count(), 25);
        QCOMPARE(b.army(now->army)->stacks[0].experience(), 70);
        QCOMPARE(bestHeroes(a, pa, 1), QList<HeroId>({h}));
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
};

QTEST_GUILESS_MAIN(TstCampaign)
#include "tst_campaign.moc"
