#include <QJsonArray>
#include <QtTest>

#include "client/ViewModel.h"
#include "core/content/ContentRegistry.h"
#include "core/model/PlayerView.h"
#include "core/rules/GameSetup.h"

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
        QCOMPARE(v.resources.value("gold"), 10000);
    }

    void myThingsAreComplete() {
        GameView v;
        QVERIFY(parseGameView(core::buildPlayerView(gs, reg, ids[0]), &v));
        QCOMPARE(v.myHeroes().size(), 1);
        QCOMPARE(v.myTowns().size(), 1);
        const ViewHero* h = v.myHeroes().first();
        QVERIFY(h->own);
        QVERIFY(h->movementPoints > 0);
        QVERIFY(h->traits.value("spell_points_max") > 0);
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
            if (o.id == "barracks") barracksWhy = o.why;
            if (o.id == "archery_range") archeryWhy = o.why;
        }
        QVERIFY(barracksWhy.isEmpty());                 // can be built now
        QVERIFY(archeryWhy.contains("Barracks"));        // needs the barracks first
        QVERIFY(t->recruitOptions.isEmpty());            // no dwelling yet
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
        tiles[0] = 99;
        m2["tiles"] = tiles;
        bad["map"] = m2;
        QVERIFY(!parseGameView(bad, &v, &error));
        QVERIFY(error.contains("palette"));
    }
};

QTEST_APPLESS_MAIN(TstViewModel)
#include "tst_viewmodel.moc"
