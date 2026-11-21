#include <QJsonArray>
#include <QtTest>

#include "TestContent.h"

#include "client/MovePlanner.h"
#include "client/Panels.h"
#include "client/Sound.h"
#include "client/ViewModel.h"
#include "core/content/ContentRegistry.h"
#include "core/model/PlayerView.h"
#include "core/rules/GameSetup.h"
#include "core/rules/Movement.h"

using namespace kot;
using namespace kot::client;

class TstViewModel : public QObject {
    Q_OBJECT
    core::ContentRegistry reg;
    core::GameState gs;
    QList<core::PlayerId> ids;

private slots:
    void initTestCase() {
        QStringList errors;
        QVERIFY2(reg.loadFromDirectory(KOT_DATA_DIR, &errors), qPrintable(errors.join("\n")));
        useContent(reg);
        core::GameSetupParams p;
        p.seed = 4;
        p.players = {{"Ann", false, "", "humans", 1, "#d62728"}, {"Bob", false, "", "humans", 2, "#1f77b4"}};
        QString error;
        QVERIFY2(core::createGame(reg, p, &gs, &ids, &error), qPrintable(error));
    }

    void parsesARealView() {
        GameView v;
        QString error;
        QVERIFY2(parseGameView(core::buildPlayerView(gs, reg, ids[0]), &v, &error), qPrintable(error));
        QCOMPARE(v.day, 1);
        QCOMPARE(v.you, int(ids[0].value()));
        QVERIFY(v.isMyTurn());
        QCOMPARE(v.width, 64);
        QCOMPARE(v.height, 64);
        QCOMPARE(v.tiles.size(), 64 * 64);
        QCOMPARE(v.players.size(), 2);
        QVERIFY(v.myTeam() > 0);
        QCOMPARE(v.resources.value(kot::core::R("gold")), 10000);
    }

    void resourceBarOrdersGoldFirstThenCheapest() {
        const QList<core::ResourceId> order = ResourceBar::orderedResources(reg);
        QVERIFY(order.size() >= 3);
        QVERIFY(reg.resource(order.first()).isCurrency);  // gold leads
        for (int i = 1; i < order.size(); ++i) {
            const core::ResourceDef& prev = reg.resource(order[i - 1]);
            const core::ResourceDef& cur = reg.resource(order[i]);
            QVERIFY2(!cur.isCurrency, "only the currency leads");
            QVERIFY2(prev.value <= cur.value,
                     qPrintable(QStringLiteral("%1 (%2) before %3 (%4)")
                                    .arg(prev.key).arg(prev.value).arg(cur.key).arg(cur.value)));
        }
    }

    void myThingsAreComplete() {
        GameView v;
        QVERIFY(parseGameView(core::buildPlayerView(gs, reg, ids[0]), &v));
        QCOMPARE(v.myHeroes().size(), 1);
        QCOMPARE(v.myTowns().size(), 1);
        const ViewHero* h = v.myHeroes().first();
        QVERIFY(h->own);
        QVERIFY(h->movementPoints > 0);
        QVERIFY(h->traits.value(kot::core::TR("spell_points_max")) > 0);
        QCOMPARE(v.heroAt(h->pos), h);
        const ViewArmy* army = v.army(h->army);
        QVERIFY(army && !army->stacks.isEmpty());
        QVERIFY(army->stacks[0].totalHp > 0);
        const ViewTown* t = v.myTowns().first();
        QVERIFY(t->mine && t->own);
        QCOMPARE(v.townAt(t->pos), t);
        QVERIFY(v.army(t->garrison));
    }

    void townOptionsComeFromTheServer() {
        GameView v;
        QVERIFY(parseGameView(core::buildPlayerView(gs, reg, ids[0]), &v));
        const ViewTown* t = v.myTowns().first();
        QVERIFY(!t->buildOptions.isEmpty());
        QString barracksWhy = "?", archeryWhy;
        for (const BuildOption& o : t->buildOptions) {
            if (o.id == kot::core::B("barracks")) barracksWhy = o.why;
            if (o.id == kot::core::B("archery_range")) archeryWhy = o.why;
        }
        QVERIFY(barracksWhy.isEmpty());                 // can be built now
        QVERIFY(archeryWhy.contains("Barracks"));        // needs the barracks first
        QVERIFY(t->recruitOptions.isEmpty());            // no dwelling yet
    }

    // B on the map steps through the towns that can still build today.
    void nextTownToBuildSkipsThoseThatCannot() {
        const auto town = [](int id, bool mine, int builtDay, const QString& why) {
            ViewTown t;
            t.id = id;
            t.name = QStringLiteral("Town %1").arg(id);
            t.mine = t.own = mine;
            t.lastBuildDay = builtDay;
            t.buildOptions = {{kot::core::B("barracks"), why}};
            return t;
        };
        GameView v;
        v.day = 3;
        v.towns = {town(1, true, 0, {}),            // can build
                   town(2, true, 3, {}),            // built today
                   town(3, true, 0, "needs gold"),  // cannot build now
                   town(4, true, 0, {}),            // can build
                   town(5, false, 0, {})};          // not mine
        QVERIFY(v.towns[0].canBuildToday(v.day));
        QVERIFY(!v.towns[1].canBuildToday(v.day));
        QVERIFY(!v.towns[2].canBuildToday(v.day));
        QCOMPARE(v.nextTownToBuild(0), 1);  // from nothing: the first
        QCOMPARE(v.nextTownToBuild(1), 4);  // skips the built and the blocked one
        QCOMPARE(v.nextTownToBuild(4), 1);  // wraps around
        QCOMPARE(v.nextTownToBuild(3), 4);  // from a town that cannot build: the next one
        v.day = 4;
        QCOMPARE(v.nextTownToBuild(1), 2);  // a new day: yesterday's builder can build again
        v.towns = {town(1, true, 4, {})};   // the only town has built today
        QCOMPARE(v.nextTownToBuild(0), 0);
        v.towns.clear();
        QCOMPARE(v.nextTownToBuild(0), 0);
    }

    void theEnemyIsNotInTheView() {
        GameView v;
        QVERIFY(parseGameView(core::buildPlayerView(gs, reg, ids[0]), &v));
        QCOMPARE(v.heroes.size(), 1);
        QCOMPARE(v.towns.size(), 1);
        QVERIFY(v.known(v.myHeroes().first()->pos));
        QVERIFY(v.inSight(v.myHeroes().first()->pos));
        QVERIFY(!v.known({-1, 0}));
        const core::Pos enemy = gs.towns().last().pos;
        QVERIFY(!v.known(enemy));
    }

    void plannerAgreesWithTheServer() {
        GameView v;
        QVERIFY(parseGameView(core::buildPlayerView(gs, reg, ids[0]), &v));
        const ViewHero* h = v.myHeroes().first();
        MovePlanner planner;
        planner.prepare(v, reg, h->id);
        QVERIFY(planner.ready());
        QVERIFY(!planner.reachableToday().isEmpty());
        // The planner can only reason about what is in the view: hostile field armies
        // hidden by fog of war are absent from it and cannot be planned around. Give
        // the server the same information by dropping those armies from the copy
        // (keeping the moving hero's own army and town garrisons, which the rules need).
        QList<quint32> visibleArmies;
        for (const ViewArmy& a : v.armies) visibleArmies.append(quint32(a.id));
        QList<core::ArmyId> garrisons;
        for (const core::Town& t : gs.towns())
            if (t.garrison.isValid()) garrisons.append(t.garrison);
        const core::ArmyId movingArmy = gs.hero(core::HeroId(quint32(h->id)))->army;
        // Every tile reachable today is walked to in full by the server too, or the
        // server stops at the zone of control the planner showed.
        int checked = 0, ambushes = 0;
        for (const core::Pos t : planner.reachableToday()) {
            core::GameState copy = gs;
            for (auto it = copy.armies().begin(); it != copy.armies().end();) {
                const core::Army& a = it.value();
                if (a.id == movingArmy || garrisons.contains(a.id) || visibleArmies.contains(a.id.value()))
                    ++it;
                else
                    it = copy.armies().erase(it);
            }
            const MovePlan plan = planner.plan(t);
            QVERIFY(plan.found);
            QCOMPARE(plan.affordable, plan.steps.size());
            const core::MoveResult r = core::moveHero(copy, reg, core::HeroId(quint32(h->id)), t);
            QVERIFY2(r.ok, qPrintable(r.error));
            if (plan.ambushAt >= 0) {
                ++ambushes;
                QVERIFY(r.ambushed);
                QCOMPARE(r.stepsTaken, plan.ambushAt + 1);
            } else {
                QVERIFY(r.arrived && !r.ambushed);
                QCOMPARE(r.stepsTaken, plan.steps.size());
            }
            if (++checked > 40) break;
        }
        qInfo("checked %d moves, %d of them ambushed", checked, ambushes);
        // Unexplored land is not planned through.
        core::Pos dark{-1, -1};
        for (int i = 0; i < v.tiles.size() && dark.x < 0; ++i)
            if (v.tiles[i] < 0) dark = {i % v.width, i / v.width};
        QVERIFY(dark.x >= 0);
        QVERIFY(!planner.plan(dark).found);
        // Not even right at the edge of the known world.
        for (const core::Pos t : planner.reachableToday())
            for (const core::Pos n : {core::Pos{t.x + 1, t.y}, core::Pos{t.x - 1, t.y}, core::Pos{t.x, t.y + 1}})
                if (v.inBounds(n) && v.tiles[n.y * v.width + n.x] < 0) QVERIFY(!planner.plan(n).found);
    }

    void soundEffectsAreValidWavs() {
        using sound::Effect;
        for (const Effect e : {Effect::Click, Effect::Step, Effect::Clash, Effect::Spell, Effect::Coins, Effect::Bell,
                               Effect::Drums, Effect::Fanfare, Effect::Defeat}) {
            const QByteArray w = sound::effectWav(e);
            QVERIFY(w.startsWith("RIFF"));
            QCOMPARE(w.mid(8, 4), QByteArray("WAVE"));
            QVERIFY(w.size() > 44 + 1000);  // header + some sound
            int loud = 0;
            for (int i = 44; i + 1 < w.size(); i += 2) {
                const qint16 v = qint16(quint8(w[i]) | (quint8(w[i + 1]) << 8));
                loud = std::max<int>(loud, std::abs(v));
            }
            QVERIFY(loud > 1000);  // not silent
        }
    }

    void rejectsInconsistentData() {
        QJsonObject json = core::buildPlayerView(gs, reg, ids[0]);
        QJsonObject map = json["map"].toObject();
        map["width"] = 63;
        json["map"] = map;
        GameView v;
        QString error;
        QVERIFY(!parseGameView(json, &v, &error));
        QVERIFY(error.contains("size"));

        QVERIFY(!parseGameView({}, &v, &error));
        QVERIFY(error.contains("no map"));

        QJsonObject bad = core::buildPlayerView(gs, reg, ids[0]);
        QJsonObject m2 = bad["map"].toObject();
        QJsonArray tiles = m2["tiles"].toArray();
        tiles.removeLast();  // one tile short
        m2["tiles"] = tiles;
        bad["map"] = m2;
        QVERIFY(!parseGameView(bad, &v, &error));
        QVERIFY(error.contains("does not match its tiles"));
    }
};

QTEST_APPLESS_MAIN(TstViewModel)
#include "tst_viewmodel.moc"
