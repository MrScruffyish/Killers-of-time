#include <QtTest>

#include "core/content/ContentRegistry.h"
#include "core/rules/HeroRules.h"
#include "core/rules/Vision.h"

using namespace kot::core;

class TstVision : public QObject {
    Q_OBJECT
    ContentRegistry reg;
    GameState gs;
    PlayerId p1, p2;
    TeamId t1, t2;

    static bool seen(const QByteArray& v, int width, Pos p) { return v[p.y * width + p.x] != 0; }

private slots:
    void initTestCase() {
        QStringList errors;
        QVERIFY2(reg.loadFromDirectory(KOT_DATA_DIR, &errors), qPrintable(errors.join("\n")));
    }

    void init() {
        gs = GameState(1);
        gs.setMap(GameMap(30, 30, "grass"));
        t1 = gs.newTeam();
        t2 = gs.newTeam();
        p1 = gs.addPlayer("A", t1, Player::Controller::Human, "humans", "#ff0000");
        p2 = gs.addPlayer("B", t2, Player::Controller::Human, "humans", "#0000ff");
    }

    void heroSeesARoundArea() {
        const HeroId h = createHero(gs, reg, p1, "H", "knight", {15, 15});
        gs.hero(h)->traits["scouting_radius"] = 3;
        const QByteArray v = visibleTiles(gs, t1, reg);
        QVERIFY(seen(v, 30, {15, 15}));
        QVERIFY(seen(v, 30, {18, 15}));   // straight, distance 3
        QVERIFY(seen(v, 30, {18, 16}));
        QVERIFY(!seen(v, 30, {18, 17}));  // corner is cut off
        QVERIFY(!seen(v, 30, {19, 15}));
    }

    void biggerRadiusSeesMore() {
        const HeroId h = createHero(gs, reg, p1, "H", "knight", {15, 15});
        gs.hero(h)->traits["scouting_radius"] = 6;
        QVERIFY(seen(visibleTiles(gs, t1, reg), 30, {21, 15}));
    }

    void edgesDoNotCrash() {
        createHero(gs, reg, p1, "H", "knight", {0, 0});
        createHero(gs, reg, p1, "G", "knight", {29, 29});
        const QByteArray v = visibleTiles(gs, t1, reg);
        QVERIFY(seen(v, 30, {0, 0}));
        QVERIFY(seen(v, 30, {29, 29}));
    }

    void teamsShareVisionOthersDoNot() {
        const PlayerId ally = gs.addPlayer("C", t1, Player::Controller::Ai, "humans", "#00ff00");
        createHero(gs, reg, ally, "Ally", "knight", {5, 5});
        createHero(gs, reg, p2, "Enemy", "knight", {25, 25});
        const QByteArray v1 = visibleTiles(gs, t1, reg);
        QVERIFY(seen(v1, 30, {5, 5}));
        QVERIFY(!seen(v1, 30, {25, 25}));
        const QByteArray v2 = visibleTiles(gs, t2, reg);
        QVERIFY(seen(v2, 30, {25, 25}));
        QVERIFY(!seen(v2, 30, {5, 5}));
    }

    void ownedTownsAndMinesGiveVision() {
        gs.addTown("T", "humans", {10, 10}, p1);
        gs.addMine("gold", 1000, {20, 20}, p1);
        gs.addMine("wood", 2, {3, 20});  // neutral: gives nothing
        const QByteArray v = visibleTiles(gs, t1, reg);
        QVERIFY(seen(v, 30, {14, 10}));   // town radius 5
        QVERIFY(seen(v, 30, {20, 21}));   // mine radius 2
        QVERIFY(!seen(v, 30, {3, 20}));
    }

    void exploredMemoryStaysAfterLeaving() {
        const HeroId h = createHero(gs, reg, p1, "H", "knight", {5, 5});
        gs.hero(h)->traits["scouting_radius"] = 3;
        updateExplored(gs, reg, t1);
        gs.army(gs.hero(h)->army)->pos = {20, 20};
        updateExplored(gs, reg, t1);

        const QByteArray v = visibleTiles(gs, t1, reg);
        QCOMPARE(tileVisibility(gs, t1, v, {20, 20}), Visibility::Visible);
        QCOMPARE(tileVisibility(gs, t1, v, {5, 5}), Visibility::Explored);
        QCOMPARE(tileVisibility(gs, t1, v, {12, 12}), Visibility::Unexplored);
        QCOMPARE(tileVisibility(gs, t1, v, {-1, 0}), Visibility::Unexplored);
        // The other team has explored nothing.
        QCOMPARE(tileVisibility(gs, t2, QByteArray(900, 0), {5, 5}), Visibility::Unexplored);
    }
};

QTEST_APPLESS_MAIN(TstVision)
#include "tst_vision.moc"
