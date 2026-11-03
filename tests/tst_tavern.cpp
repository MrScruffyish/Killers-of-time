#include <QSet>
#include <QtTest>

#include "core/content/ContentRegistry.h"
#include "core/rules/HeroRules.h"
#include "core/rules/Tavern.h"
#include "core/rules/TownRules.h"
#include "core/rules/TurnCycle.h"

using namespace kot::core;

class TstTavern : public QObject {
    Q_OBJECT
    ContentRegistry reg;
    GameState gs;
    PlayerId p1, p2;
    TownId town;

    Player& me() { return *gs.player(p1); }
    Town& t() { return *gs.town(town); }
    int heroCount(PlayerId p) {
        int n = 0;
        for (const Hero& h : gs.heroes()) n += (h.owner == p) ? 1 : 0;
        return n;
    }

private slots:
    void initTestCase() {
        QStringList errors;
        QVERIFY2(reg.loadFromDirectory(KOT_DATA_DIR, &errors), qPrintable(errors.join("\n")));
    }

    void init() {
        gs = GameState(7);
        gs.setMap(GameMap(20, 20, "grass"));
        p1 = gs.addPlayer("A", gs.newTeam(), Player::Controller::Human, "humans", "#ff0000");
        gs.player(p1)->race = "orcs";  // a race without a trade bonus (humans get +10%): plain trade numbers
        p2 = gs.addPlayer("B", gs.newTeam(), Player::Controller::Human, "humans", "#0000ff");
        me().resources = {{"gold", 10000}, {"wood", 50}};
        town = gs.addTown("Home", "humans", {3, 3}, p1);
        t().buildings << "tavern";
        refreshTavernOffers(gs, reg);
    }

    void offersTwoDifferentHumanHeroes() {
        QCOMPARE(t().tavernOffers.size(), 2);
        QVERIFY(t().tavernOffers[0] != t().tavernOffers[1]);
        for (const QString& id : t().tavernOffers) QVERIFY(reg.heroTemplate(id));
    }

    void noTavernNoOffers() {
        const TownId other = gs.addTown("Plain", "humans", {9, 9}, p1);
        refreshTavernOffers(gs, reg);
        QVERIFY(gs.town(other)->tavernOffers.isEmpty());
        QVERIFY(hasTavern(t(), reg));
        QVERIFY(!hasTavern(*gs.town(other), reg));
    }

    void offersAreReproducibleFromTheSeed() {
        GameState a(99), b(99);
        for (GameState* g : {&a, &b}) {
            g->setMap(GameMap(5, 5, "grass"));
            const PlayerId p = g->addPlayer("P", g->newTeam(), Player::Controller::Human, "humans", "#fff000");
            const TownId tn = g->addTown("T", "humans", {1, 1}, p);
            g->town(tn)->buildings << "tavern";
            refreshTavernOffers(*g, reg);
        }
        QCOMPARE(a.towns().first().tavernOffers, b.towns().first().tavernOffers);
    }

    void buildingATavernStocksTheOffers() {
        const TownId fresh = gs.addTown("Fresh", "humans", {9, 9}, p1);
        QVERIFY(gs.town(fresh)->tavernOffers.isEmpty());
        QVERIFY(buildBuilding(gs, reg, fresh, "tavern", p1).ok);
        QCOMPARE(gs.town(fresh)->tavernOffers.size(), 2);
    }

    void hiringCreatesTheHeroInTheTown() {
        const QString offered = t().tavernOffers[0];
        const HireResult r = hireHero(gs, reg, town, p1, offered);
        QVERIFY2(r.ok, qPrintable(r.error));
        const Hero* h = gs.hero(r.hero);
        QVERIFY(h);
        QCOMPARE(h->owner, p1);
        QCOMPARE(h->templateId, offered);
        QCOMPARE(h->name, reg.heroTemplate(offered)->name);
        QCOMPARE(h->classId, reg.heroTemplate(offered)->classId);
        QCOMPARE(gs.army(h->army)->pos, (Pos{3, 3}));
        QCOMPARE(me().resources.value("gold"), 7500);
        QVERIFY(!t().tavernOffers.contains(offered));
    }

    void oneHeroInTheTownAtATime() {
        QVERIFY(hireHero(gs, reg, town, p1, t().tavernOffers[0]).ok);
        t().lastHireWeek = 0;  // only the hero standing there stops the next one
        const HireResult r = hireHero(gs, reg, town, p1, t().tavernOffers[0]);
        QVERIFY(!r.ok);
        QVERIFY(r.error.contains("already in the town"));
        QCOMPARE(me().resources.value("gold"), 7500);
    }

    void oneHeroAWeekPerTavern() {
        const HireResult first = hireHero(gs, reg, town, p1, t().tavernOffers[0]);
        QVERIFY(first.ok);
        gs.army(gs.hero(first.hero)->army)->pos = {6, 6};  // he walks out
        const HireResult again = hireHero(gs, reg, town, p1, t().tavernOffers[0]);
        QVERIFY(!again.ok);
        QVERIFY2(again.error.contains("this week"), qPrintable(again.error));
        while ((gs.day() - 1) / 7 + 1 == t().lastHireWeek) gs.setDay(gs.day() + 1);  // the week turns
        QVERIFY(hireHero(gs, reg, town, p1, t().tavernOffers[0]).ok);
    }

    void hiredHeroIsOfferedNowhereElse() {
        const TownId other = gs.addTown("Other", "humans", {9, 9}, p2);
        gs.town(other)->buildings << "tavern";
        gs.town(other)->tavernOffers = t().tavernOffers;
        const QString hired = t().tavernOffers[0];
        QVERIFY(hireHero(gs, reg, town, p1, hired).ok);
        QVERIFY(!gs.town(other)->tavernOffers.contains(hired));
        // Redrawing never offers a hero who is in the game.
        for (int i = 0; i < 20; ++i) {
            refreshTavernOffers(gs, reg);
            QVERIFY(!t().tavernOffers.contains(hired));
            QVERIFY(!gs.town(other)->tavernOffers.contains(hired));
        }
    }

    void offersDifferBetweenTownsInOneDraw() {
        const TownId other = gs.addTown("Other", "humans", {9, 9}, p2);
        gs.town(other)->buildings << "tavern";
        for (int i = 0; i < 20; ++i) {
            refreshTavernOffers(gs, reg);
            QSet<QString> all;
            for (const QString& s : t().tavernOffers) all.insert(s);
            for (const QString& s : gs.town(other)->tavernOffers) all.insert(s);
            QCOMPARE(all.size(), t().tavernOffers.size() + gs.town(other)->tavernOffers.size());
        }
    }

    void fewHeroesLeftMeansFewerOffers() {
        // Hire 5 of the 6 humans heroes into other towns' slots by creating them directly.
        const QStringList all = freeHeroTemplates(gs, reg, "humans");
        QCOMPARE(all.size(), 6);
        for (int i = 0; i < 5; ++i) {
            const HeroId h = createHero(gs, reg, p2, "X", reg.heroTemplate(all[i])->classId, {15, 15});
            gs.hero(h)->templateId = all[i];
        }
        refreshTavernOffers(gs, reg);
        QCOMPARE(t().tavernOffers, QStringList({all[5]}));
    }

    void releasedHeroesCanBeOfferedAgain() {
        const QString hired = t().tavernOffers[0];
        const HeroId h = hireHero(gs, reg, town, p1, hired).hero;
        gs.heroes().remove(h);  // e.g. the hero died
        QVERIFY(freeHeroTemplates(gs, reg, "humans").contains(hired));
    }

    void rejections() {
        const QString offered = t().tavernOffers[0];
        QVERIFY(hireHero(gs, reg, town, p2, offered).error.contains("not your town"));
        QVERIFY(hireHero(gs, reg, TownId(999), p1, offered).error.contains("unknown town"));
        QVERIFY(hireHero(gs, reg, town, p1, "merlin_not_offered").error.contains("not offered"));
        t().buildings.clear();
        QVERIFY(hireHero(gs, reg, town, p1, offered).error.contains("no tavern"));
        t().buildings << "tavern";
        me().resources["gold"] = 1000;
        QVERIFY(hireHero(gs, reg, town, p1, offered).error.contains("not enough gold"));
        QCOMPARE(heroCount(p1), 0);
    }

    void heroLimit() {
        for (int i = 0; i < 8; ++i) createHero(gs, reg, p1, "H", "knight", {15 + i % 3, 10 + i});
        QVERIFY(hireHero(gs, reg, town, p1, t().tavernOffers[0]).error.contains("more heroes"));
    }

    void tradeCanPayForTheHero() {
        me().resources = {{"gold", 0}, {"wood", 100}};
        const QString offered = t().tavernOffers[0];
        QVERIFY(hireHero(gs, reg, town, p1, offered).error.contains("not enough gold"));
        const HireResult r = hireHero(gs, reg, town, p1, offered, true);
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(me().resources.value("gold"), 0);
        QCOMPARE(me().resources.value("wood"), 100 - 32);  // 2500 gold worth of wood at 80%
    }

    void tradeCannotHelpWhenTooPoor() {
        me().resources = {{"gold", 10}, {"wood", 5}};
        const HireResult r = hireHero(gs, reg, town, p1, t().tavernOffers[0], true);
        QVERIFY(!r.ok);
        QVERIFY(r.error.contains("even by trading"));
        QCOMPARE(me().resources.value("wood"), 5);
        QCOMPARE(heroCount(p1), 0);
    }

    void offersRefreshEveryWeek() {
        gs.setDay(7);
        // Players without towns and heroes are out of the game and skipped; keep B in.
        createHero(gs, reg, p2, "H2", "knight", {19, 19});
        beginGame(gs, reg);
        endTurn(gs, reg);
        endTurn(gs, reg);  // day 8: a new week
        QCOMPARE(gs.day(), 8);
        QCOMPARE(t().tavernOffers.size(), 2);
    }
};

QTEST_APPLESS_MAIN(TstTavern)
#include "tst_tavern.moc"
