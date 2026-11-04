#include <QCheckBox>
#include <QDir>
#include <QJsonObject>
#include <QLabel>
#include <QPainter>
#include <QSettings>
#include <QTemporaryDir>
#include <QPushButton>
#include <QTabWidget>
#include <QComboBox>
#include <QtTest>
#include <tuple>

#include "client/Art.h"
#include "client/BattleResultDialog.h"
#include "client/BattleView.h"
#include "client/GamePage.h"
#include "client/HeroDialog.h"
#include "client/MainWindow.h"
#include "client/LobbyPage.h"
#include "client/MapView.h"
#include "client/MiniMap.h"
#include "client/Settings.h"
#include "client/SpellBook.h"
#include "client/TownDialog.h"
#include "client/EquipmentView.h"
#include "client/Toast.h"
#include "client/GameOverDialog.h"
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
        // Settings go to a throwaway folder, not the player's own config.
        static QTemporaryDir config;
        QSettings::setDefaultFormat(QSettings::NativeFormat);
        QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, config.path());
        QStringList errors;
        QVERIFY2(reg.loadFromDirectory(KOT_DATA_DIR, &errors), qPrintable(errors.join("\n")));
        QHash<QString, int> tiers;
        for (const core::UnitDef& u : reg.units()) tiers.insert(u.id, u.tier);
        art::setUnitTiers(tiers);
        qputenv("KOT_DATA_DIR", KOT_DATA_DIR);
        qApp->setStyleSheet(art::styleSheet());  // the real look
    }

    // Every unit's picture as a battle token, plus the map sprites: drawn without
    // errors; saved as a contact sheet when KOT_SCREENSHOT_DIR is set.
    void windowSizesAreRemembered() {
        {
            QWidget w;
            rememberGeometry(&w, "testWindow");
            w.show();
            w.resize(640, 410);
            QApplication::processEvents();
            w.hide();
        }
        QWidget again;
        rememberGeometry(&again, "testWindow");
        again.show();
        QApplication::processEvents();
        QCOMPARE(again.size(), QSize(640, 410));
        QVERIFY(!settingsFile().isEmpty());
    }

    // A small hand-made map with a lake, an island and a river, all in sight: the
    // coasts, features and edges (saved as 0_coast.png).
    void coastGallery() {
        GameView v;
        v.width = 16;
        v.height = 12;
        v.terrainPalette = {"grass", "sea", "sand", "forest", "hills", "mountains", "river", "dirt"};
        const QStringList rows = {
            "0000033330000000",
            "0011110333004400",
            "0111111100044440",
            "0111221110005500",
            "0112222111000500",
            "0111221111100000",
            "0011111111600000",
            "0000111116000777",
            "0000000006000777",
            "0003330006000000",
            "0033333006660000",
            "0003300000060000",
        };
        for (const QString& row : rows)
            for (const QChar c : row) v.tiles.append(c.digitValue());
        v.visible = QByteArray(v.width * v.height, '\1');
        // Monster groups of every size, along the bottom left.
        int armyId = 900;
        for (const auto& [x, n, unit] : {std::tuple{0, 3, "wolf"}, std::tuple{1, 7, "skeleton"}, std::tuple{2, 15, "orc_warrior"},
                                         std::tuple{3, 30, "pikeman"}, std::tuple{4, 80, "imp"}}) {
            ViewArmy a;
            a.id = armyId++;
            a.pos = {x, 11 - (x % 2)};
            ViewStack st;
            st.unit = unit;
            st.count = n;
            a.stacks = {st};
            if (n > 20) {
                ViewStack second;
                second.unit = "archer";
                second.count = n / 3;
                a.stacks << second;
            }
            v.armies << a;
        }
        MapView map(&reg);
        map.resize(16 * 48 + 20, 12 * 48 + 20);
        map.setTileSize(48);
        map.setView(v);
        map.show();
        QApplication::processEvents();
        const QPixmap pm = map.viewport()->grab();
        QVERIFY(!pm.isNull());
        const QString dir = qEnvironmentVariable("KOT_SCREENSHOT_DIR");
        if (!dir.isEmpty()) pm.save(QDir(dir).filePath("0_coast.png"));
        // The same map on the minimap, with half of it only remembered.
        for (int i = 0; i < v.visible.size(); ++i) v.visible[i] = (i % v.width) < 9 ? '\1' : '\0';
        MiniMap mini(&reg);
        mini.resize(300, 230);
        mini.setView(v);
        mini.setVisibleTiles(QRect(2, 2, 8, 6));
        mini.show();
        QApplication::processEvents();
        if (!dir.isEmpty()) mini.grab().save(QDir(dir).filePath("0_minimap.png"));
        // Every decoration on its terrain (saved as 0_decor.png).
        const QStringList kinds = {"grass", "dirt", "hills", "sand", "snow", "swamp", "forest", "sea"};
        QImage decor(6 * 72, int(kinds.size()) * 72, QImage::Format_ARGB32);
        QPainter dp(&decor);
        for (int k = 0; k < kinds.size(); ++k) {
            const core::TerrainDef* t = reg.terrainType(kinds[k]);
            QVERIFY(t);
            for (int variant = 0; variant < 6; ++variant) {
                dp.drawPixmap(variant * 72, k * 72, art::terrainTile(kinds[k], QColor(t->color), 72, variant % 4));
                const QPixmap& pm = art::decoration(kinds[k], variant, 72);
                if (!pm.isNull()) dp.drawPixmap(variant * 72, k * 72, pm);
            }
        }
        dp.end();
        if (!dir.isEmpty()) decor.save(QDir(dir).filePath("0_decor.png"));
    }

    void artGallery() {
        const QList<core::UnitDef> units = reg.units().values();
        const int cols = 6, cell = 110;
        QImage sheet(cols * cell + 640, ((units.size() + cols - 1) / cols + 1) * cell, QImage::Format_ARGB32);
        sheet.fill(QColor("#2b2118"));
        QPainter p(&sheet);
        for (int i = 0; i < units.size(); ++i) {
            const QPointF c((i % cols) * cell + cell / 2.0, (i / cols) * cell + cell / 2.0 - 8);
            art::drawUnitToken(p, c, 40, i % 2 ? QColor("#6e5a86") : QColor("#d62728"), units[i].name, 25, i % 4, false, false,
                               units[i].id, units[i].rank);
            p.setPen(Qt::white);
            p.drawText(QRectF(c.x() - cell / 2.0, c.y() + 42, cell, 16), Qt::AlignCenter, units[i].name);
        }
        const int y = ((units.size() + cols - 1) / cols) * cell + 10;
        int x = 10;
        for (const QPixmap& pm : {art::townSprite(QColor("#d62728"), 96), art::heroSprite(QColor("#1f77b4"), false, 64),
                                  art::mineSprite("gold", art::resourceColor("gold"), QColor("#999999"), 64),
                                  art::mineSprite("wood", art::resourceColor("wood"), QColor("#d62728"), 64),
                                  art::mineSprite("stone", art::resourceColor("stone"), QColor("#999999"), 64),
                                  art::mineSprite("gems", art::resourceColor("gems"), QColor("#1f77b4"), 64),
                                  art::dwellingSprite(QColor("#999999"), "unicorn", 64),
                                  art::chestSprite(art::resourceColor("wood"), 64), art::artifactSprite(64)}) {
            QVERIFY(!pm.isNull());
            p.drawPixmap(x, y, pm);
            x += pm.width() + 12;
        }
        // The mouse pointers, twice their size.
        int px = x + 10;
        using art::Pointer;
        for (const Pointer k : {Pointer::Normal, Pointer::Move, Pointer::MoveLater, Pointer::Attack, Pointer::MoveAttack, Pointer::Shoot,
                                Pointer::Enter, Pointer::Select, Pointer::Blocked}) {
            const QPixmap pm = art::pointerImage(k);
            QVERIFY(!pm.isNull());
            p.drawPixmap(QRect(px, y, 64, 64), pm);
            px += 70;
        }
        p.end();
        const QString dir = qEnvironmentVariable("KOT_SCREENSHOT_DIR");
        if (!dir.isEmpty()) sheet.save(QDir(dir).filePath("0_units.png"));
        // Every unit's picture big, on battle ground (0_units_big.png).
        {
            const int big = 128, bcols = 12;
            QImage bigSheet(bcols * big, int((units.size() + bcols - 1) / bcols) * big, QImage::Format_ARGB32);
            bigSheet.fill(QColor("#9a8458"));
            QPainter bp(&bigSheet);
            for (int i = 0; i < units.size(); ++i) {
                const QPixmap& pm = art::unitIcon(units[i].id, units[i].rank, big);
                QVERIFY(!pm.isNull());
                bp.drawPixmap((i % bcols) * big, (i / bcols) * big, pm);
            }
            bp.end();
            if (!dir.isEmpty()) bigSheet.save(QDir(dir).filePath("0_units_big.png"));
        }
        // Icons (0_icons.png): every resource at three sizes, the special mines, every button icon.
        {
            const QStringList res = {"gold", "wood", "stone", "grain", "gems", "sulphur", "coal", "mercury", "papyrus"};
            const QStringList buttons = {"build", "buy", "wagon", "end", "spell", "market", "defend", "auto", "retreat",
                                         "catapult", "town", "army", "tavern"};
            QImage icons(int(res.size()) * 136 + 20, 128 + 40 + 24 + 140 + 80 + 180 + 160, QImage::Format_ARGB32);
            icons.fill(QColor("#3a2c1e"));
            QPainter ip(&icons);
            for (int i = 0; i < res.size(); ++i) {
                ip.drawPixmap(10 + i * 136, 10, art::resourceIcon(res[i], 128));
                ip.drawPixmap(10 + i * 136, 146, art::resourceIcon(res[i], 40));
                ip.drawPixmap(56 + i * 136, 158, art::resourceIcon(res[i], 20));
            }
            int mx = 10;
            for (const QString& r : {"gold", "wood", "stone", "gems", "sulphur"}) {
                ip.drawPixmap(mx, 196, art::mineSprite(r, art::resourceColor(r), QColor("#d62728"), 128));
                mx += 140;
            }
            ip.drawPixmap(mx, 196, art::mineSprite("gems", art::resourceColor("gems"), QColor("#d62728"), 64));
            for (int i = 0; i < buttons.size(); ++i) {
                const QPixmap pm = art::buttonIcon(buttons[i]).pixmap(64, 64);
                QVERIFY(!pm.isNull());
                ip.drawPixmap(10 + i * 70, 340, pm);
            }
            {  // places to visit, fresh and visited (0_sites.png)
                QImage sites(10 * 104, 104, QImage::Format_ARGB32);
                sites.fill(QColor("#5d8a3a"));
                QPainter sp(&sites);
                int sx = 0;
                for (const QString site : {"shrine", "magic_well", "obelisk", "arena", "learning_stone"})
                    for (const bool glow : {true, false}) sp.drawPixmap((sx++) * 104, 0, art::siteSprite(site, glow, 100));
                sp.end();
                if (!dir.isEmpty()) sites.save(QDir(dir).filePath("0_sites.png"));
            }
            int si = 0;
            for (const core::SpellDef& sp : reg.spells()) {
                ip.drawPixmap(10 + (si % 17) * 70, 590 + (si / 17) * 70, art::spellIcon(sp.id, sp.school, 64));
                ++si;
            }
            int ai = 0;
            for (const core::ArtifactDef& a : reg.artifacts()) {
                const QPixmap& pm = art::artifactIcon(a.id, a.slot, 72);
                QVERIFY(!pm.isNull());
                ip.drawPixmap(10 + (ai % 15) * 80, 420 + (ai / 15) * 80, pm);
                ++ai;
            }
            ip.end();
            if (!dir.isEmpty()) icons.save(QDir(dir).filePath("0_icons.png"));
        }
        // The hero's gear: worn things around the figure, the backpack below (0_equipment.png).
        {
            EquipmentView gear(&reg);
            gear.setArtifacts({"crown_of_kings", "dragon_blade", "tower_shield", "winged_boots", "ring_of_vigor", "spyglass", "war_banner"},
                              {"iron_sword", "lucky_clover", "chain_mail"}, true);
            gear.resize(gear.sizeHint());
            QVERIFY(gear.sizeHint().height() > 400);
            if (!dir.isEmpty()) gear.grab().save(QDir(dir).filePath("0_equipment.png"));
            QSignalSpy wear(&gear, &EquipmentView::equipRequested);
            QTest::mouseClick(&gear, Qt::LeftButton, {}, QPoint(6 + 28, 430 + 28));  // the first backpack box
            QCOMPARE(wear.count(), 1);
            QCOMPARE(wear.first().first().toString(), QString("iron_sword"));
            gear.show();
            Toast* t = Toast::show(&gear, art::artifactIcon("winged_boots", "feet", 128), "<b>Found the Winged Boots!</b>");
            QVERIFY(t && t->isVisible());
        }
        // Every hero class on the map (0_heroes.png).
        {
            const QList<core::HeroClassDef> classes = reg.heroClasses().values();
            QImage heroes(int(classes.size()) * 100, 100, QImage::Format_ARGB32);
            heroes.fill(QColor("#5d8a3a"));
            QPainter hp(&heroes);
            for (int i = 0; i < classes.size(); ++i) hp.drawPixmap(i * 100, 0, art::heroSprite(QColor("#d62728"), false, 100, classes[i].id));
            hp.end();
            if (!dir.isEmpty()) heroes.save(QDir(dir).filePath("0_heroes.png"));
        }
        // The end screen with a made-up history (0_gameover.png).
        {
            GameView v;
            v.you = 1;
            v.winnerTeam = 2;
            v.gameOver = true;
            v.players = {ViewPlayer{1, "Ann", 1, "#d62728", "humans", false, true}, ViewPlayer{2, "AI 2", 2, "#1f77b4", "orcs", true, false}};
            for (int w = 1; w <= 9; ++w)
                v.history.append(QJsonObject{{"week", w}, {"players", QJsonObject{
                    {"1", QJsonObject{{"armyValue", 3000 * w - (w > 6 ? 9000 : 0)}, {"towns", w < 7 ? 1 + w / 3 : 0}, {"mines", w}, {"gold", 5000 + 700 * w}}},
                    {"2", QJsonObject{{"armyValue", 3800 * w}, {"towns", 1 + w / 2}, {"mines", w + 2}, {"gold", 400}}}}}});
            GameOverDialog end(v, "Game over. Your team has lost.");
            end.resize(760, 600);
            if (!dir.isEmpty()) end.grab().save(QDir(dir).filePath("0_gameover.png"));
        }
        // Towns: palisade / walls / citadel, small to big, for each race; and a town scene.
        QImage towns(3 * 4 * 110, 7 * 120 + 190, QImage::Format_ARGB32);
        towns.fill(QColor("#5d8a3a"));
        QPainter tp(&towns);
        int row = 0;
        for (const QString race : {"humans", "undead", "sylvan", "orcs", "dwarves", "ashborn", "tidefolk"}) {
            for (int walls = 0; walls <= 2; ++walls)
                for (int stage = 0; stage < 4; ++stage)
                    tp.drawPixmap((walls * 4 + stage) * 110, row * 120,
                                  art::townSprite(art::TownLook{QColor("#1f77b4"), race, walls, stage * 3 + (walls ? 1 : 0)}, 104));
            ++row;
        }
        using K = art::SceneBuilding::Kind;
        art::drawTownScene(tp, QRectF(0, 7 * 120 + 10, towns.width(), 170), art::TownLook{QColor("#d62728"), "humans", 2, 12},
                           {{K::Hall, 2, {}}, {K::Tavern, 0, {}}, {K::Dwelling, 1, "pikeman"}, {K::Dwelling, 2, "archer_elite"},
                            {K::MageGuild, 2, {}}, {K::Market, 0, {}}, {K::Granary, 0, {}}, {K::Shipyard, 0, {}}, {K::Walls, 0, {}},
                            {K::Walls, 0, {}}, {K::Walls, 0, {}}, {K::Dwelling, 0, "armoured_eagle"}});
        tp.end();
        if (!dir.isEmpty()) towns.save(QDir(dir).filePath("0_towns.png"));
        // Hero portraits of every class.
        {
            const QList<core::HeroClassDef> classes = reg.heroClasses().values();
            QImage faces(int(classes.size()) * 110, 110, QImage::Format_ARGB32);
            faces.fill(QColor("#2b2118"));
            QPainter fp(&faces);
            for (int i = 0; i < classes.size(); ++i)
                fp.drawPixmap(i * 110 + 5, 5, art::portrait(classes[i].id, i % 2 ? QColor("#1f77b4") : QColor("#d62728"), i, 100));
            fp.end();
            if (!dir.isEmpty()) faces.save(QDir(dir).filePath("0_portraits.png"));
        }
        // Battles on different ground.
        for (const QString ground : {"forest", "snow", "swamp", "sand"}) {
            ViewBattle vb;
            vb.present = true;
            vb.width = 15;
            vb.height = 11;
            vb.round = 1;
            vb.terrain = ground;
            vb.obstacles = {{5, 2}, {7, 5}, {9, 8}, {6, 9}, {10, 3}};
            ViewBattleUnit a;
            a.id = 1; a.side = 0; a.unit = "archer_elite"; a.count = 12; a.hex = {0, 5}; a.range = 7; a.shots = 10;
            ViewBattleUnit b;
            b.id = 2; b.side = 1; b.unit = "treant"; b.count = 4; b.hex = {14, 5};
            vb.units = {a, b};
            vb.active = 1;
            BattleView view(&reg);
            view.resize(1000, 620);
            view.setBattle(vb, QColor("#d62728"), QColor("#6e5a86"));
            view.show();
            QApplication::processEvents();
            if (!dir.isEmpty()) view.grab().save(QDir(dir).filePath(QStringLiteral("0_battle_%1.png").arg(ground)));
            if (ground == "forest") {
                // The next state: the archers walked, a treant fell. Mid-animation.
                ViewBattle next = vb;
                next.units[0].hex = {4, 5};
                next.units[1].count = 3;
                view.setBattle(next, QColor("#d62728"), QColor("#6e5a86"));
                QTest::qWait(150);
                if (!dir.isEmpty()) view.grab().save(QDir(dir).filePath("0_battle_anim.png"));
                QTest::qWait(1400);  // everything settles
            }
        }
        // A siege: a citadel's walls (one fallen, one cracked), towers, a catapult.
        {
            ViewBattle vb;
            vb.present = true;
            vb.width = 15;
            vb.height = 11;
            vb.round = 3;
            vb.wallLevel = 2;
            vb.wallMaxHp = 100;
            for (int y = 0; y < 11; ++y)
                if (y != 3) vb.walls.insert(core::Pos{11, y}, y == 5 ? 30 : y == 7 ? 60 : 100);
            vb.gate = {11, 5};
            vb.towers = {{12, 1}, {12, 9}, {12, 3}};
            ViewBattleUnit a;
            a.id = 1; a.side = 0; a.unit = "pikeman"; a.count = 30; a.hex = {9, 4};
            ViewBattleUnit c;
            c.id = 2; c.side = 0; c.unit = "catapult"; c.count = 1; c.hex = {0, 5};
            ViewBattleUnit d;
            d.id = 3; d.side = 1; d.unit = "archer_elite"; d.count = 15; d.hex = {13, 5}; d.range = 7; d.shots = 10;
            vb.units = {a, c, d};
            vb.active = 2;
            BattleView view(&reg);
            view.resize(1000, 620);
            view.setBattle(vb, QColor("#1f77b4"), QColor("#d62728"));
            view.show();
            QApplication::processEvents();
            if (!dir.isEmpty()) view.grab().save(QDir(dir).filePath("0_siege.png"));
        }
        // The spell runes of every school.
        {
            QImage runes(6 * 60, 60, QImage::Format_ARGB32);
            runes.fill(QColor("#2b2118"));
            QPainter rp(&runes);
            int i = 0;
            for (const char* school : {"air", "water", "earth", "nature", "dark", "other"}) rp.drawPixmap(i++ * 60 + 6, 6, schoolRune(school, 48));
            rp.end();
            if (!dir.isEmpty()) runes.save(QDir(dir).filePath("0_runes.png"));
        }
        // A lost battle as the result window shows it.
        const QJsonObject report{{"outcome", "defender"}, {"rounds", 4}, {"loserHeroKilled", true},
                                 {"attacker", QJsonObject{{"owner", 1}, {"losses", QJsonObject{{"archer", 12}, {"militia", 20}}}}},
                                 {"defender", QJsonObject{{"owner", 0}, {"losses", QJsonObject{{"bone_dragon", 1}}}}}};
        BattleResultDialog lost(&reg, report, 1);
        QCOMPARE(lost.outcome(), BattleResultDialog::Outcome::Defeat);
        lost.show();
        QApplication::processEvents();
        if (!dir.isEmpty()) lost.grab().save(QDir(dir).filePath("0_defeat.png"));
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
        // The host got a computer opponent by default (slot 2); Bob took the next free slot.
        QTRY_COMPARE_WITH_TIMEOUT(w.client().lobby().slotList[1].kind, net::SlotKind::Ai, 5000);
        QTRY_COMPARE_WITH_TIMEOUT(w.client().lobby().slotList[2].kind, net::SlotKind::Human, 5000);
        shot(&w, "2_lobby");

        // Ann hosts: she has no Ready box. Bob confirms through the protocol.
        QVERIFY(!w.lobbyPage()->findChild<QCheckBox*>("ready0"));
        bob.setReady(true);
        QTRY_VERIFY_WITH_TIMEOUT(w.client().lobby().slotList[2].ready, 5000);

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

        // Click a free tile next to the hero: the route is plotted; Enter walks it.
        const ViewHero hero = *game->view().hero(game->selectedHero());
        core::Pos target = hero.pos;
        for (int dy = -1; dy <= 1 && target == hero.pos; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                const core::Pos t{hero.pos.x + dx, hero.pos.y + dy};
                if (t == hero.pos || !game->view().known(t)) continue;
                const QString terrain = game->view().terrainPalette[game->view().tiles[t.y * game->view().width + t.x]];
                if (reg.terrainType(terrain)->walkable && !game->view().townCovering(t)) { target = t; break; }
            }
        }
        QVERIFY(target != hero.pos);
        game->mapView()->centerOn(hero.pos);
        QTest::mouseClick(game->mapView()->viewport(), Qt::LeftButton, {}, game->mapView()->tileRect(target).center());
        QCOMPARE(game->mapView()->route(), std::optional<core::Pos>(target));
        QVERIFY(game->mapView()->routePlan().found);
        QApplication::processEvents();
        QCOMPARE(game->view().hero(hero.id)->pos, hero.pos);  // a single click only plots
        game->mapView()->setFocus();
        QTest::keyClick(game->mapView(), Qt::Key_Return);
        QTRY_COMPARE_WITH_TIMEOUT(game->view().hero(hero.id)->pos, target, 5000);
        QVERIFY(!game->mapView()->route());  // arrived: the route is gone
        QVERIFY(game->view().hero(hero.id)->movementPoints < hero.movementPoints);
        QVERIFY(game->logText().contains("Move"));

        // Right-click the town: the dialog opens, Barracks can be built.
        const ViewTown town = *game->view().myTowns().first();
        game->mapView()->centerOn(town.pos);
        QTest::mouseClick(game->mapView()->viewport(), Qt::RightButton, {}, game->mapView()->tileRect(town.pos).center());
        QVERIFY(game->townDialog()->isVisible());
        // The town list at the bottom: one town, so nothing to step to.
        auto* choice = game->townDialog()->findChild<QComboBox*>("townChoice");
        QVERIFY(choice);
        QCOMPARE(choice->count(), 1);
        QVERIFY(!game->townDialog()->findChild<QPushButton*>("townNextButton")->isEnabled());
        QApplication::processEvents();
        shot(game->townDialog(), "4_town_buildings");
        auto* build = game->townDialog()->findChild<QPushButton*>("build_barracks");
        QVERIFY(build && build->isEnabled());
        QTest::mouseClick(build, Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(game->view().town(town.id)->buildings.contains("barracks"), 5000);
        // The only town has built today: "Build, then buy" is off until tomorrow.
        QTRY_VERIFY_WITH_TIMEOUT(!game->findChild<QPushButton*>("buildAndBuyButton")->isEnabled(), 3000);
        QTRY_VERIFY_WITH_TIMEOUT(game->townDialog()->findChild<QPushButton*>("recruit_militia") != nullptr, 5000);
        {
            auto* tabs = game->townDialog()->findChild<QTabWidget*>();
            for (int i = 0; i < tabs->count(); ++i)
                if (tabs->tabText(i) == "Recruit") tabs->setCurrentIndex(i);
        }
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
        QTest::mouseClick(game->findChild<QWidget*>("heroPanel"), Qt::LeftButton);
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
                free = free && !gs.townCovering(p);
                for (const core::Mine& m : gs.mines()) free = free && m.pos != p;
                for (const core::MapDwelling& d : gs.mapDwellings()) free = free && d.pos != p;
                if (free) spot = p;
            }
        QVERIFY(spot.x >= 0);
        gs.army(gs.addArmy(core::PlayerId(), spot))->addUnits("militia", 4, 6);
        game->mapView()->centerOn(spot);
        QTest::mouseDClick(game->mapView()->viewport(), Qt::LeftButton, {}, game->mapView()->tileRect(spot).center());
        // 4 militia would rather join; Ann fights them anyway.
        QTRY_VERIFY_WITH_TIMEOUT(game->findChild<QPushButton*>("joinFight") != nullptr, 5000);
        shot(game->findChild<QWidget*>("joinOfferDialog"), "6c_join_offer");
        QTest::mouseClick(game->findChild<QPushButton*>("joinFight"), Qt::LeftButton);
        QTRY_VERIFY2_WITH_TIMEOUT(game->view().battle.present, qPrintable(game->logText().right(400)), 5000);
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
        QTRY_VERIFY_WITH_TIMEOUT(game->findChild<QWidget*>("battleResult") != nullptr, 5000);
        auto* result = game->findChild<QWidget*>("battleResult");
        QVERIFY(result->findChild<QLabel*>("battleOutcome")->text().contains("Victory"));  // 4 militia stood no chance
        QApplication::processEvents();
        shot(result, "8b_battle_result");
        shot(&w, "8_after_battle");
    }
};

QTEST_MAIN(TstClientSmoke)
#include "tst_clientsmoke.moc"
