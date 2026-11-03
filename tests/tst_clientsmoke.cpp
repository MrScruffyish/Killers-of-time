#include <QCheckBox>
#include <QDir>
#include <QLabel>
#include <QPushButton>
#include <QTabWidget>
#include <QtTest>

#include "client/Art.h"
#include "client/BattleView.h"
#include "client/GamePage.h"
#include "client/HeroDialog.h"
#include "client/MainWindow.h"
#include "client/LobbyPage.h"
#include "client/MapView.h"
#include "client/TownDialog.h"
#include "core/content/ContentRegistry.h"
#include "net/LobbyClient.h"
#include "server/GameServer.h"

using namespace kot;
using namespace kot::client;

// Drives the real client windows against a real server (offscreen display):
// connect, lobby, start, select hero, click to move, open the town, build, end turn.
class TstClientSmoke : public QObject {
    Q_OBJECT
    core::ContentRegistry reg;

    static void shot(QWidget* w, const QString& name) {
        const QString dir = qEnvironmentVariable("KOT_SCREENSHOT_DIR");
        if (!dir.isEmpty()) w->grab().save(QDir(dir).filePath(name + ".png"));
    }

private slots:
    void initTestCase() {
        QStringList errors;
        QVERIFY2(reg.loadFromDirectory(KOT_DATA_DIR, &errors), qPrintable(errors.join("\n")));
        qputenv("KOT_DATA_DIR", KOT_DATA_DIR);
        qApp->setStyleSheet(art::styleSheet());  // the real look
    }

    void playThroughTheClient() {
        server::GameServer server({"humans"}, {"passive"});
        server.setContent(&reg);
        QVERIFY(server.start(0));

        MainWindow w;
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        QCOMPARE(w.currentPage(), QString("connect"));
        shot(&w, "1_connect");

        // Ann joins through the window, Bob is a bare client.
        w.connectToServer("127.0.0.1", server.port(), "Ann");
        QTRY_COMPARE_WITH_TIMEOUT(w.currentPage(), QString("lobby"), 5000);
        net::LobbyClient bob;
        bob.connectToServer("127.0.0.1", server.port(), "Bob");
        QTRY_COMPARE_WITH_TIMEOUT(w.client().lobby().members.size(), 2, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(bob.isConnected() && bob.lobby().members.size() == 2, 5000);  // Bob is fully in
        w.client().setSlot(2, {{"kind", "ai"}});
        QTRY_COMPARE_WITH_TIMEOUT(w.client().lobby().slotList[2].kind, net::SlotKind::Ai, 5000);
        shot(&w, "2_lobby");

        // Ready up: Ann by clicking her checkbox, Bob through the protocol.
        auto* ready = w.lobbyPage()->findChild<QCheckBox*>("ready0");
        QVERIFY(ready);
        QTest::mouseClick(ready, Qt::LeftButton, {}, QPoint(8, ready->height() / 2));  // on the box itself
        bob.setReady(true);
        QTRY_VERIFY_WITH_TIMEOUT(w.client().lobby().slotList[0].ready && w.client().lobby().slotList[1].ready, 5000);

        auto* start = w.lobbyPage()->findChild<QPushButton*>("startButton");
        QVERIFY(start && start->isEnabled());
        QTest::mouseClick(start, Qt::LeftButton);
        QTRY_COMPARE_WITH_TIMEOUT(w.currentPage(), QString("game"), 5000);

        GamePage* game = w.gamePage();
        QVERIFY(game);
        QTRY_VERIFY_WITH_TIMEOUT(!game->view().heroes.isEmpty(), 5000);
        QVERIFY(game->view().isMyTurn());
        QVERIFY(game->selectedHero() > 0);
        QApplication::processEvents();
        shot(&w, "3_game");

        // Click a free tile next to the hero: he walks there.
        const ViewHero hero = *game->view().hero(game->selectedHero());
        core::Pos target = hero.pos;
        for (int dy = -1; dy <= 1 && target == hero.pos; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                const core::Pos t{hero.pos.x + dx, hero.pos.y + dy};
                if (t == hero.pos || !game->view().known(t)) continue;
                const QString terrain = game->view().terrainPalette[game->view().tiles[t.y * game->view().width + t.x]];
                if (reg.terrainType(terrain)->walkable && !game->view().townAt(t)) { target = t; break; }
            }
        }
        QVERIFY(target != hero.pos);
        game->mapView()->centerOn(hero.pos);
        QTest::mouseClick(game->mapView()->viewport(), Qt::LeftButton, {}, game->mapView()->tileRect(target).center());
        QTRY_COMPARE_WITH_TIMEOUT(game->view().hero(hero.id)->pos, target, 5000);
        QVERIFY(game->view().hero(hero.id)->movementPoints < hero.movementPoints);
        QVERIFY(game->logText().contains("Move"));

        // Right-click the town: the dialog opens, Barracks can be built.
        const ViewTown town = *game->view().myTowns().first();
        game->mapView()->centerOn(town.pos);
        QTest::mouseClick(game->mapView()->viewport(), Qt::RightButton, {}, game->mapView()->tileRect(town.pos).center());
        QVERIFY(game->townDialog()->isVisible());
        QApplication::processEvents();
        shot(game->townDialog(), "4_town_buildings");
        auto* build = game->townDialog()->findChild<QPushButton*>("build_barracks");
        QVERIFY(build && build->isEnabled());
        QTest::mouseClick(build, Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(game->view().town(town.id)->buildings.contains("barracks"), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(game->townDialog()->findChild<QPushButton*>("recruit_militia") != nullptr, 5000);
        game->townDialog()->findChild<QTabWidget*>()->setCurrentIndex(1);
        QApplication::processEvents();
        shot(game->townDialog(), "5_town_recruit");
        auto* recruit = game->townDialog()->findChild<QPushButton*>("recruit_militia");
        QTest::mouseClick(recruit, Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(game->view().army(game->view().town(town.id)->garrison)->stacks.size() == 1, 5000);
        game->townDialog()->hide();

        // End the turn: Bob is next, the buttons lock; Bob ends his and it is Ann's day 2.
        auto* endTurn = game->findChild<QPushButton*>("endTurnButton");
        QVERIFY(endTurn && endTurn->isEnabled());
        QTest::mouseClick(endTurn, Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(!game->view().isMyTurn(), 5000);
        QVERIFY(!endTurn->isEnabled());
        bob.sendCommand("end_turn");
        QTRY_VERIFY_WITH_TIMEOUT(game->view().isMyTurn() && game->view().day == 2, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(endTurn->isEnabled(), 5000);
        QVERIFY(game->logText().contains("Day 2"));
        QApplication::processEvents();
        shot(&w, "6_game_day2");

        // The hero screen.
        QTest::mouseClick(game->findChild<QPushButton*>("heroButton"), Qt::LeftButton);
        QVERIFY(game->heroDialog()->isVisible());
        QVERIFY(game->heroDialog()->findChildren<QLabel*>().size() > 5);
        QApplication::processEvents();
        shot(game->heroDialog(), "6b_hero");
        game->heroDialog()->hide();

        // One-click action from the toolbar.
        auto* buy = game->findChild<QPushButton*>("buyAllButton");
        QTest::mouseClick(buy, Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(game->logText().contains("Buy all units: bought"), 5000);

        // A battle: monsters appear next to Ann's hero, she clicks them.
        core::GameState& gs = server.session()->state();
        const core::Hero* annHero = gs.hero(core::HeroId(quint32(hero.id)));
        QVERIFY(annHero);
        const core::Pos at = gs.army(annHero->army)->pos;
        core::Pos spot{-1, -1};
        for (int dy = -1; dy <= 1 && spot.x < 0; ++dy)
            for (int dx = -1; dx <= 1 && spot.x < 0; ++dx) {
                const core::Pos p{at.x + dx, at.y + dy};
                bool free = (dx || dy) && gs.map().inBounds(p) && reg.terrainType(gs.map().terrainAt(p))->walkable;
                for (const core::Army& a : gs.armies()) free = free && a.pos != p;
                for (const core::Town& t : gs.towns()) free = free && t.pos != p;
                for (const core::Mine& m : gs.mines()) free = free && m.pos != p;
                for (const core::MapDwelling& d : gs.mapDwellings()) free = free && d.pos != p;
                if (free) spot = p;
            }
        QVERIFY(spot.x >= 0);
        gs.army(gs.addArmy(core::PlayerId(), spot))->addUnits("militia", 4, 6);
        game->mapView()->centerOn(spot);
        QTest::mouseClick(game->mapView()->viewport(), Qt::LeftButton, {}, game->mapView()->tileRect(spot).center());
        QTRY_VERIFY_WITH_TIMEOUT(game->view().battle.present, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(game->battleView()->isVisible(), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(game->view().battle.myMove(), 5000);
        QApplication::processEvents();
        shot(&w, "7_battle");

        // Click a hex the active stack can reach: it moves there.
        const ViewBattle& vb = game->view().battle;
        const int activeId = vb.active;
        QVERIFY(!vb.reachable.isEmpty());
        const core::Pos dest = vb.reachable.last();
        QTest::mouseClick(game->battleView()->field(), Qt::LeftButton, {}, game->battleView()->hexCentre(dest));
        QTRY_VERIFY_WITH_TIMEOUT(!game->view().battle.present || game->view().battle.unit(activeId) == nullptr
                                     || game->view().battle.unit(activeId)->hex == dest, 5000);

        // Let the computer finish it.
        if (game->view().battle.present) {
            QTRY_VERIFY_WITH_TIMEOUT(game->view().battle.myMove(), 5000);
            auto* autoButton = game->battleView()->findChild<QPushButton*>("autoBattleButton");
            QTest::mouseClick(autoButton, Qt::LeftButton);
        }
        QTRY_VERIFY_WITH_TIMEOUT(!game->view().battle.present, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(game->mapView()->isVisible(), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(game->findChild<QWidget*>("battleResultBox") != nullptr, 5000);
        QApplication::processEvents();
        shot(&w, "8_after_battle");
    }
};

QTEST_MAIN(TstClientSmoke)
#include "tst_clientsmoke.moc"
