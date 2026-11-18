#include <QTextBrowser>
#include <QCheckBox>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFrame>
#include <QGroupBox>
#include <QPixmap>
#include <QShortcut>
#include <QJsonObject>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QPainter>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>
#include <QPushButton>
#include <QTableWidget>
#include <QTabWidget>
#include <QComboBox>
#include <QScreen>
#include <QSignalSpy>
#include <QSpinBox>
#include <QtTest>

#include "TestContent.h"
#include <tuple>

#include "client/Art.h"
#include "client/ConnectPage.h"
#include "client/BattleResultDialog.h"
#include "client/BattleView.h"
#include "client/GamePage.h"
#include "client/HeroDialog.h"
#include "client/MainWindow.h"
#include "client/LoadDialog.h"
#include "client/LobbyPage.h"
#include "client/MapView.h"
#include "client/MiniMap.h"
#include "client/Panels.h"
#include "client/SaveGameDialog.h"
#include "client/Settings.h"
#include "client/SpellBook.h"
#include "client/TextLine.h"
#include "client/TownDialog.h"
#include "client/EquipmentView.h"
#include "client/Toast.h"
#include "client/GameOverDialog.h"
#include "client/MeetDialog.h"
#include "core/content/ContentRegistry.h"
#include "core/rules/MapSites.h"
#include "net/GameToken.h"
#include "net/LobbyClient.h"
#include "net/LobbyTypes.h"
#include "server/GameServer.h"

using namespace kot;
using namespace kot::client;

// Drives the real client windows against a real server (offscreen display):
// connect, lobby, start, select hero, click to move, open the town, build, end turn.
// How many pixels (of every second row and column) of what is on the screen in `w`
// differ from `w` painted afresh: widgets that repaint only what changed must not leave
// anything stale. -1: the screen cannot be read (then nothing is checked).
static int differsFromFullRepaint(QWidget* w, const char* name) {
    const QImage shown = w->screen()->grabWindow(w->window()->winId()).toImage();
    const QImage full = w->grab().toImage();
    if (shown.isNull()) return -1;
    const QPoint at = w->mapTo(w->window(), QPoint(0, 0));
    const QImage part = shown.copy(QRect(at * shown.devicePixelRatio(), full.size()));
    int differing = 0;
    for (int y = 0; y < full.height(); y += 2)
        for (int x = 0; x < full.width(); x += 2) {
            const QRgb a = part.pixel(x, y), b = full.pixel(x, y);
            if (std::abs(qRed(a) - qRed(b)) + std::abs(qGreen(a) - qGreen(b)) + std::abs(qBlue(a) - qBlue(b)) > 24) ++differing;
        }
    if (!qEnvironmentVariableIsEmpty("KOT_SCREENSHOT_DIR")) {
        part.save(QDir(qEnvironmentVariable("KOT_SCREENSHOT_DIR")).filePath(QStringLiteral("0_%1_shown.png").arg(name)));
        full.save(QDir(qEnvironmentVariable("KOT_SCREENSHOT_DIR")).filePath(QStringLiteral("0_%1_full.png").arg(name)));
    }
    return differing;
}

class TstClientSmoke : public QObject {
    Q_OBJECT
    core::ContentRegistry reg;

    static void shot(QWidget* w, const QString& name) {
        const QString dir = qEnvironmentVariable("KOT_SCREENSHOT_DIR");
        if (!dir.isEmpty()) w->grab().save(QDir(dir).filePath(name + ".png"));
    }

    // A save of a one-human game (played with `token`) in `dir`: what Load game on the
    // start screen finds. The server that made it is gone; only the save file is left.
    void saveOneHumanGame(const QString& dir, const QString& token, const QString& name) {
        server::GameServer saver({"humans"}, {"passive"});
        saver.setContent(&reg);
        saver.setSaveDir(dir);
        QVERIFY(saver.start(0));
        net::LobbyClient player;
        player.setContentChecksum(reg.checksum());
        player.setGameToken(token);
        player.connectToServer("127.0.0.1", saver.port(), "Ann");
        QTRY_VERIFY_WITH_TIMEOUT(player.isConnected() && !player.lobby().slotList.isEmpty(), 5000);
        player.setSlot(1, {{"kind", "ai"}});
        QTRY_COMPARE_WITH_TIMEOUT(player.lobby().slotList[1].kind, net::SlotKind::Ai, 5000);
        player.startGame();
        QTRY_VERIFY_WITH_TIMEOUT(!player.view().isEmpty(), 5000);
        player.saveGame(name);
        QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(QDir(dir).filePath(name + ".json")), 5000);
    }

    static ViewTown buildTown(int id, const QString& name, bool hasIt, const QString& why = QString()) {
        ViewTown t;
        t.id = id;
        t.name = name;
        t.race = core::RC("humans");
        t.owner = 1;
        t.own = t.mine = true;
        t.pos = {id, 5};
        if (hasIt) t.buildings = {core::B("barracks")};
        t.buildOptions = {{core::B("barracks"), why}};
        return t;
    }
    static int findBarracksRow(QTableWidget* table) {
        for (int row = 0; row < table->rowCount(); ++row)
            if (table->item(row, 0) && table->item(row, 0)->text() == QStringLiteral("Barracks")) return row;
        return -1;
    }
    // The all-towns button lives in the Build column of the Barracks row.
    static QPushButton* allTownsButton(QTableWidget* table) {
        const int row = findBarracksRow(table);
        if (row < 0 || !table->cellWidget(row, 2)) return nullptr;
        return table->cellWidget(row, 2)->findChild<QPushButton*>("buildEverywhere_barracks");
    }

private slots:
    void initTestCase() {
        // Settings go to a throwaway folder, not the player's own config.
        static QTemporaryDir config;
        QSettings::setDefaultFormat(QSettings::NativeFormat);
        QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, config.path());
        QStringList errors;
        QVERIFY2(reg.loadFromDirectory(KOT_DATA_DIR, &errors), qPrintable(errors.join("\n")));
        useContent(reg);
        QSettings().setValue("game/endTurnReminder", false);  // no question box in the scripted play-through
        art::setContent(&reg);
        art::setArtDirectory(QDir(KOT_DATA_DIR).filePath("art"));
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

    // A saved full-screen geometry must not make the setup fill the display.
    void theSetupWindowStaysSmallWithWideMargins() {
        {
            QWidget big;
            big.resize(2000, 1400);
            big.show();
            QSettings().setValue(QStringLiteral("geometry/setupWindow"), big.saveGeometry());
        }
        MainWindow w;
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        QVERIFY2(w.width() <= 1000, qPrintable(QString::number(w.width())));
        QVERIFY2(w.height() <= 700, qPrintable(QString::number(w.height())));
        QVERIFY(!w.isMaximized());
        QVERIFY(!w.isFullScreen());
        QLayout* outer = w.lobbyPage()->layout();
        QVERIFY(outer);
        QVERIFY2(outer->contentsMargins().left() >= 20, "wide left margin");
        QVERIFY2(outer->contentsMargins().right() >= 20, "wide right margin");
        QVERIFY(w.lobbyPage()->findChild<QLayout*>("setupSettingsRow1"));
        QVERIFY(w.lobbyPage()->findChild<QLayout*>("setupSettingsRow2"));
        QVERIFY(w.lobbyPage()->findChild<QLayout*>("setupSettingsRow3"));
        QSettings().remove(QStringLiteral("geometry/setupWindow"));
    }

    // The setup offers the new map-content controls with sane defaults.
    void theLobbyLetsYouChooseHowMuchIsOnTheMap() {
        net::LobbyClient client;
        LobbyPage page(&client, &reg);
        QVERIFY(page.findChild<QLayout*>("setupSettingsRow3"));
        for (const char* name : {"neutralTownPercent", "mapDwellingPercent", "minePercent", "wanderingMonsterPercent"}) {
            auto* spin = page.findChild<QSpinBox*>(QString::fromLatin1(name));
            QVERIFY2(spin, name);
            QCOMPARE(spin->minimum(), 0);
            QCOMPARE(spin->maximum(), 400);
            QCOMPARE(spin->value(), 100);
        }
    }

    // Saving asks for a name in a roomy dialog, not the tiny stock one.
    void theSaveGameDialogIsRoomyAndSuggestsTheDay() {
        SaveGameDialog dialog(QStringLiteral("day7"));
        auto* name = dialog.findChild<QLineEdit*>(QStringLiteral("saveName"));
        QVERIFY(name);
        QCOMPARE(name->text(), QStringLiteral("day7"));
        QCOMPARE(dialog.name(), QStringLiteral("day7"));
        QVERIFY2(dialog.width() >= 480, qPrintable(QString::number(dialog.width())));
        QVERIFY2(dialog.height() >= 180, qPrintable(QString::number(dialog.height())));
        name->setText(QStringLiteral("  before the siege  "));
        QCOMPARE(dialog.name(), QStringLiteral("before the siege"));
    }

    // The start screen has the ways to play (solo, hot seat, network) and a saved game.
    void startScreenOffersTheWaysToPlayAndLoad() {
        ConnectPage page;
        QSignalSpy solo(&page, &ConnectPage::soloRequested);
        QSignalSpy hotseat(&page, &ConnectPage::hotseatRequested);
        QSignalSpy network(&page, &ConnectPage::joinRequested);
        QSignalSpy load(&page, &ConnectPage::loadRequested);
        auto* soloButton = page.findChild<QPushButton*>(QStringLiteral("soloButton"));
        auto* hotseatButton = page.findChild<QPushButton*>(QStringLiteral("hotseatButton"));
        auto* networkButton = page.findChild<QPushButton*>(QStringLiteral("networkButton"));
        auto* loadButton = page.findChild<QPushButton*>(QStringLiteral("loadGameButton"));
        QVERIFY(soloButton && hotseatButton && networkButton && loadButton);
        QCOMPARE(soloButton->text(), QStringLiteral("Solo game"));
        QCOMPARE(hotseatButton->text(), QStringLiteral("Hot seat"));
        QCOMPARE(networkButton->text(), QStringLiteral("Network game"));
        QCOMPARE(loadButton->text(), QStringLiteral("Load game"));

        soloButton->click();
        QCOMPARE(solo.count(), 1);
        QCOMPARE(hotseat.count(), 0);
        QCOMPARE(network.count(), 0);
        QCOMPARE(load.count(), 0);
        hotseatButton->click();
        QCOMPARE(hotseat.count(), 1);
        QCOMPARE(network.count(), 0);
        QCOMPARE(load.count(), 0);
        networkButton->click();  // shows the server fields; Connect sends them (see below)
        QCOMPARE(network.count(), 0);
        QCOMPARE(load.count(), 0);
        loadButton->click();
        QCOMPARE(load.count(), 1);
    }

    // Ctrl+L on the start screen takes the same load path as the Load game button.
    void ctrlLLoadsFromTheStartScreen() {
        ConnectPage page;
        page.show();
        QVERIFY(QTest::qWaitForWindowExposed(&page));
        page.activateWindow();
        QTRY_VERIFY_WITH_TIMEOUT(page.isActiveWindow(), 5000);
        auto* name = page.findChild<QLineEdit*>(QStringLiteral("nameEdit"));
        QVERIFY(name);
        name->setFocus();
        QTRY_VERIFY_WITH_TIMEOUT(name->hasFocus(), 5000);
        QSignalSpy load(&page, &ConnectPage::loadRequested);
        QTest::keyClick(name, Qt::Key_L, Qt::ControlModifier);
        QCOMPARE(load.count(), 1);
    }

    // The start screen is just the ways to play: the server fields appear only when
    // Network game is chosen, and Connect sends what was typed there.
    void startScreenShowsTheNetworkSettingsOnlyWhenAsked() {
        ConnectPage page;
        page.show();
        QVERIFY(QTest::qWaitForWindowExposed(&page));
        auto* networkBox = page.findChild<QGroupBox*>(QStringLiteral("networkBox"));
        auto* networkButton = page.findChild<QPushButton*>(QStringLiteral("networkButton"));
        auto* connectButton = page.findChild<QPushButton*>(QStringLiteral("connectButton"));
        auto* soloButton = page.findChild<QPushButton*>(QStringLiteral("soloButton"));
        auto* host = page.findChild<QLineEdit*>(QStringLiteral("hostEdit"));
        QVERIFY(networkBox && networkButton && connectButton && soloButton && host);
        QCOMPARE(connectButton->text(), QStringLiteral("Connect"));
        QVERIFY(!networkBox->isVisible());  // no network settings on the first screen

        QSignalSpy network(&page, &ConnectPage::joinRequested);
        QTest::mouseClick(networkButton, Qt::LeftButton);
        QVERIFY(networkBox->isVisible());
        QCOMPARE(network.count(), 0);  // choosing it shows the fields; it does not connect

        host->setText(QStringLiteral("192.0.2.10"));
        QTest::mouseClick(connectButton, Qt::LeftButton);
        QCOMPARE(network.count(), 1);
        QCOMPARE(network.first().at(0).toString(), QStringLiteral("192.0.2.10"));

        QTest::mouseClick(soloButton, Qt::LeftButton);  // another way to play: no server fields
        QVERIFY(!networkBox->isVisible());
    }

    // Load game on the start screen opens the saved games at once, not the game setup;
    // Cancel goes back to the first screen and leaves the server.
    void loadFromTheStartScreenCancelsBackToTheStartScreen() {
        QTemporaryDir saves;
        const QString token = net::newGameToken();
        saveOneHumanGame(saves.path(), token, "before-the-start");
        QVERIFY(QFile::exists(saves.filePath("before-the-start.json")));

        server::GameServer server({"humans"}, {"passive"});
        server.setContent(&reg);
        server.setSaveDir(saves.path());
        QVERIFY(server.start(0));

        MainWindow w;
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));

        // The list is modal: the timer runs in its event loop and looks at the window.
        bool listShown = false, setupShown = false;
        QTimer watcher;
        watcher.setInterval(20);
        connect(&watcher, &QTimer::timeout, &w, [&]() {
            auto* dialog = w.findChild<LoadDialog*>();
            if (!dialog || !dialog->isVisible()) return;
            listShown = true;
            setupShown = setupShown || w.currentPage() == QStringLiteral("lobby");
            watcher.stop();
            dialog->reject();  // the Cancel button
        });
        watcher.start();
        w.connectToServer(QStringLiteral("127.0.0.1"), server.port(), "Ann", token, false, false, true);
        QTRY_VERIFY_WITH_TIMEOUT(listShown, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(!w.client().isConnected(), 5000);

        QVERIFY2(!setupShown, "the game setup was shown behind the saved-game list");
        QCOMPARE(w.currentPage(), QStringLiteral("connect"));  // back on the first screen
    }

    // Choosing a save on the start screen plays it at once, without the game setup.
    void loadFromTheStartScreenStartsTheChosenSave() {
        QTemporaryDir saves;
        const QString token = net::newGameToken();
        saveOneHumanGame(saves.path(), token, "the-start");
        QVERIFY(QFile::exists(saves.filePath("the-start.json")));

        server::GameServer server({"humans"}, {"passive"});
        server.setContent(&reg);
        server.setSaveDir(saves.path());
        QVERIFY(server.start(0));

        MainWindow w;
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));

        bool setupShown = false;
        QTimer watcher;
        watcher.setInterval(20);
        connect(&watcher, &QTimer::timeout, &w, [&]() {
            auto* dialog = w.findChild<LoadDialog*>();
            if (!dialog || !dialog->isVisible()) return;
            setupShown = setupShown || w.currentPage() == QStringLiteral("lobby");
            auto* load = dialog->findChild<QPushButton*>(QStringLiteral("loadChosenButton"));
            if (load && load->isEnabled()) {
                watcher.stop();
                QTest::mouseClick(load, Qt::LeftButton);
            }
        });
        watcher.start();
        w.connectToServer(QStringLiteral("127.0.0.1"), server.port(), "Ann", token, false, false, true);
        QTRY_COMPARE_WITH_TIMEOUT(w.currentPage(), QStringLiteral("game"), 8000);

        QVERIFY2(!setupShown, "the game setup was shown before the saved-game list");
        QVERIFY(w.gamePage());
        QCOMPARE(w.gamePage()->view().day, 1);
    }

    // Item 16 changes only the start-screen way in: the lobby's own Load game button
    // still lists the saves and a Cancel keeps the lobby (and the connection).
    void theLobbysLoadButtonKeepsTheLobbyWhenCancelled() {
        QTemporaryDir saves;
        const QString token = net::newGameToken();
        saveOneHumanGame(saves.path(), token, "from-the-lobby");
        QVERIFY(QFile::exists(saves.filePath("from-the-lobby.json")));

        server::GameServer server({"humans"}, {"passive"});
        server.setContent(&reg);
        server.setSaveDir(saves.path());
        QVERIFY(server.start(0));

        MainWindow w;
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        w.connectToServer(QStringLiteral("127.0.0.1"), server.port(), "Ann", token);
        QTRY_COMPARE_WITH_TIMEOUT(w.currentPage(), QStringLiteral("lobby"), 5000);

        bool listShown = false;
        QTimer watcher;
        watcher.setInterval(20);
        connect(&watcher, &QTimer::timeout, &w, [&]() {
            auto* dialog = w.findChild<LoadDialog*>();
            if (!dialog || !dialog->isVisible()) return;
            listShown = true;
            watcher.stop();
            dialog->reject();  // the Cancel button
        });
        watcher.start();
        auto* load = w.lobbyPage()->findChild<QPushButton*>(QStringLiteral("loadButton"));
        QVERIFY(load && load->isEnabled());
        QTest::mouseClick(load, Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(listShown, 5000);
        QTest::qWait(100);  // the cancel settles

        QCOMPARE(w.currentPage(), QStringLiteral("lobby"));
        QVERIFY(w.client().isConnected());
    }

    // Ctrl+L in the lobby opens the saved-game list, the same as the Load game button.
    void ctrlLLoadsFromTheLobby() {
        QTemporaryDir saves;
        const QString token = net::newGameToken();
        saveOneHumanGame(saves.path(), token, "from-the-lobby-key");
        QVERIFY(QFile::exists(saves.filePath("from-the-lobby-key.json")));

        server::GameServer server({"humans"}, {"passive"});
        server.setContent(&reg);
        server.setSaveDir(saves.path());
        QVERIFY(server.start(0));

        MainWindow w;
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        w.connectToServer(QStringLiteral("127.0.0.1"), server.port(), "Ann", token);
        QTRY_COMPARE_WITH_TIMEOUT(w.currentPage(), QStringLiteral("lobby"), 5000);

        bool listShown = false;
        QTimer watcher;
        watcher.setInterval(20);
        connect(&watcher, &QTimer::timeout, &w, [&]() {
            auto* dialog = w.findChild<LoadDialog*>();
            if (!dialog || !dialog->isVisible()) return;
            listShown = true;
            watcher.stop();
            dialog->reject();
        });
        watcher.start();
        w.activateWindow();
        QTRY_VERIFY_WITH_TIMEOUT(w.isActiveWindow(), 5000);
        auto* seed = w.lobbyPage()->findChild<QLineEdit*>(QStringLiteral("mapSeed"));
        QVERIFY(seed);
        seed->setFocus();
        QTRY_VERIFY_WITH_TIMEOUT(seed->hasFocus(), 5000);
        QTest::keyClick(seed, Qt::Key_L, Qt::ControlModifier);
        QTRY_VERIFY_WITH_TIMEOUT(listShown, 5000);
        QCOMPARE(w.currentPage(), QStringLiteral("lobby"));
        QVERIFY(w.client().isConnected());
    }

    // Closing a main window must not leave the art helpers pointing at its content:
    // the next screen would dereference a destroyed registry.
    void closingTheMainWindowRestoresArtContent() {
        art::setContent(&reg);
        {
            MainWindow w;
            QCOMPARE(art::content(), &w.content());
        }
        QCOMPARE(art::content(), &reg);
        QCOMPARE(art::raceKey(core::RC("humans")), QStringLiteral("humans"));
    }

    // A shipyard is a special buy: it comes after every other building in the list.
    void theShipyardComesLastInTheBuildList() {
        GameView v;
        v.you = v.currentPlayer = 1;
        v.players = {ViewPlayer{1, "Ann", 1, "#d62728", core::RC("humans"), false, false}};
        ViewTown town;
        town.id = 7;
        town.name = "Haven";
        town.race = core::RC("humans");
        town.owner = 1;
        town.own = town.mine = true;
        town.pos = {5, 5};
        town.buildings = {core::B("village_hall")};
        // The server lists the shipyard first; the dialog must move it to the end.
        town.buildOptions = {{core::B("shipyard"), QString()},
                             {core::B("barracks"), QString()},
                             {core::B("marketplace"), QString()}};
        v.towns = {town};
        TownDialog dialog(&reg);
        dialog.showTown(v, town.id);
        QTRY_VERIFY_WITH_TIMEOUT(
            dialog.findChild<QTableWidget*>(QStringLiteral("buildingTable")) != nullptr, 2000);
        auto* table = dialog.findChild<QTableWidget*>(QStringLiteral("buildingTable"));
        QStringList names;
        for (int row = 0; row < table->rowCount(); ++row) names << table->item(row, 0)->text();
        QVERIFY2(names.contains(QStringLiteral("Shipyard")), qPrintable(names.join(", ")));
        QCOMPARE(names.last(), QStringLiteral("Shipyard"));
        dialog.hide();
    }

    // The build list says plainly whether a row is new or replaces something built.
    void theBuildListSaysNewOrUpgrade() {
        GameView v;
        v.you = v.currentPlayer = 1;
        v.players = {ViewPlayer{1, "Ann", 1, "#d62728", core::RC("humans"), false, false}};
        ViewTown town;
        town.id = 7;
        town.name = "Haven";
        town.race = core::RC("humans");
        town.owner = 1;
        town.own = town.mine = true;
        town.pos = {5, 5};
        town.buildings = {core::B("village_hall")};
        town.buildOptions = {{core::B("barracks"), QString()},
                             {core::B("town_hall"), QString()},
                             {core::B("marketplace"), QString()}};
        v.towns = {town};
        TownDialog dialog(&reg);
        dialog.showTown(v, town.id);
        QTRY_VERIFY_WITH_TIMEOUT(dialog.findChild<QTableWidget*>("buildingTable") != nullptr, 2000);
        auto* table = dialog.findChild<QTableWidget*>("buildingTable");
        QCOMPARE(table->horizontalHeaderItem(1)->text(), QStringLiteral("New / upgrade"));
        QStringList kinds;
        for (int row = 0; row < table->rowCount(); ++row)
            kinds << QStringLiteral("%1: %2").arg(table->item(row, 0)->text(), table->item(row, 1)->text());
        QVERIFY2(kinds.contains(QStringLiteral("Barracks: New")), qPrintable(kinds.join(", ")));
        QVERIFY2(kinds.contains(QStringLiteral("Town Hall: Upgrade of Village Hall")), qPrintable(kinds.join(", ")));
        dialog.hide();
    }

    // "All towns" builds that one building in the towns that can take it now.
    void theAllTownsButtonSkipsTownsThatHaveItOrCannotBuild() {
        GameView v;
        v.you = v.currentPlayer = 1;
        v.players = {ViewPlayer{1, "Ann", 1, "#d62728", core::RC("humans"), false, false}};
        v.towns = {buildTown(1, "Has it", true), buildTown(2, "Can build", false),
                   buildTown(3, "Built today", false, QStringLiteral("built today")),
                   buildTown(4, "Can build too", false)};
        TownDialog dialog(&reg);
        QSignalSpy commands(&dialog, &TownDialog::commandRequested);
        dialog.showTown(v, 2);
        QTRY_VERIFY_WITH_TIMEOUT(dialog.findChild<QTableWidget*>("buildingTable") != nullptr, 2000);
        auto* table = dialog.findChild<QTableWidget*>("buildingTable");
        auto* all = allTownsButton(table);
        QVERIFY(all);
        QVERIFY2(all->isVisible(), "another town can take the building too");
        QVERIFY(all->isEnabled());
        all->click();
        QCOMPARE(commands.count(), 2);  // only the towns without it that can build it
        QList<int> towns;
        for (const QList<QVariant>& args : commands) {
            QCOMPARE(args[0].toString(), QStringLiteral("build"));
            const QJsonObject body = args[1].toJsonObject();
            QCOMPARE(body["building"].toInt(), core::B("barracks").index());
            towns << body["town"].toInt();
        }
        QCOMPARE(towns, QList<int>({2, 4}));
        dialog.hide();
    }

    // With one town there is nothing to spread around: the shortcut is not shown.
    void allTownsIsHiddenWhenThereIsOnlyOneTown() {
        GameView v;
        v.you = v.currentPlayer = 1;
        v.players = {ViewPlayer{1, "Ann", 1, "#d62728", core::RC("humans"), false, false}};
        v.towns = {buildTown(2, "Only town", false)};
        TownDialog dialog(&reg);
        dialog.showTown(v, 2);
        QTRY_VERIFY_WITH_TIMEOUT(dialog.findChild<QTableWidget*>("buildingTable") != nullptr, 2000);
        auto* table = dialog.findChild<QTableWidget*>("buildingTable");
        auto* all = allTownsButton(table);
        QVERIFY(all);
        QVERIFY(!all->isVisible());
        auto* build = table->cellWidget(findBarracksRow(table), 2)->findChild<QPushButton*>("build_barracks");
        QVERIFY(build);
        QVERIFY(build->isVisible());  // the ordinary Build button stays
        dialog.hide();
    }

    // If this is the only town that can still put the building up, the shortcut
    // would send just the one command the plain Build button already sends.
    void allTownsIsHiddenWhenOnlyThisTownCanBuild() {
        GameView v;
        v.you = v.currentPlayer = 1;
        v.players = {ViewPlayer{1, "Ann", 1, "#d62728", core::RC("humans"), false, false}};
        v.towns = {buildTown(1, "Has it", true), buildTown(2, "Can build", false),
                   buildTown(3, "Built today", false, QStringLiteral("built today"))};
        TownDialog dialog(&reg);
        dialog.showTown(v, 2);
        QTRY_VERIFY_WITH_TIMEOUT(dialog.findChild<QTableWidget*>("buildingTable") != nullptr, 2000);
        auto* all = allTownsButton(dialog.findChild<QTableWidget*>("buildingTable"));
        QVERIFY(all);
        QVERIFY(!all->isVisible());
        dialog.hide();
    }

    // The shortcut follows the view: once the other town has the building, only
    // this one is left and the button goes away without reopening the dialog.
    void allTownsFollowsTheViewWhenTownsChange() {
        GameView v;
        v.you = v.currentPlayer = 1;
        v.players = {ViewPlayer{1, "Ann", 1, "#d62728", core::RC("humans"), false, false}};
        v.towns = {buildTown(2, "Can build", false), buildTown(4, "Can build too", false)};
        TownDialog dialog(&reg);
        dialog.showTown(v, 2);
        QTRY_VERIFY_WITH_TIMEOUT(dialog.findChild<QTableWidget*>("buildingTable") != nullptr, 2000);
        QVERIFY(allTownsButton(dialog.findChild<QTableWidget*>("buildingTable"))->isVisible());

        v.towns[1] = buildTown(4, "Built it", true);
        dialog.refresh(v);
        QTRY_VERIFY_WITH_TIMEOUT(allTownsButton(dialog.findChild<QTableWidget*>("buildingTable")) != nullptr, 2000);
        QVERIFY(!allTownsButton(dialog.findChild<QTableWidget*>("buildingTable"))->isVisible());
        dialog.hide();
    }

    // A small hand-made map with a lake, an island and a river, all in sight: the
    // coasts, features and edges (saved as 0_coast.png).
    void coastGallery() {
        GameView v;
        v.width = 16;
        v.height = 12;
        const QStringList palette = {"grass", "sea", "sand", "forest", "hills", "mountains", "river", "dirt"};  // digits below
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
            for (const QChar c : row) v.tiles.append(reg.terrainId(palette[c.digitValue()]).index());
        v.visible = QByteArray(v.width * v.height, '\1');
        // Monster groups of every size, along the bottom left.
        int armyId = 900;
        for (const auto& [x, n, unit] : {std::tuple{0, 3, "wolf"}, std::tuple{1, 7, "skeleton"}, std::tuple{2, 15, "orc_warrior"},
                                         std::tuple{3, 30, "pikeman"}, std::tuple{4, 80, "imp"}}) {
            ViewArmy a;
            a.id = armyId++;
            a.pos = {x, 11 - (x % 2)};
            ViewStack st;
            st.unit = reg.unitId(unit);
            st.count = n;
            a.stacks = {st};
            if (n > 20) {
                ViewStack second;
                second.unit = reg.unitId("archer");
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
            const core::TerrainDef* t = reg.terrainByKey(kinds[k]);
            QVERIFY(t);
            for (int variant = 0; variant < 6; ++variant) {
                dp.drawPixmap(variant * 72, k * 72, art::terrainTile(kinds[k], 72, variant % 4));
                const QPixmap& pm = art::decoration(kinds[k], variant, 72);
                if (!pm.isNull()) dp.drawPixmap(variant * 72, k * 72, pm);
            }
        }
        dp.end();
        if (!dir.isEmpty()) decor.save(QDir(dir).filePath("0_decor.png"));
    }

    void artGallery() {
        const QList<core::UnitDef> units = reg.units();
        const int cols = 6, cell = 110;
        QImage sheet(cols * cell + 640, ((units.size() + cols - 1) / cols + 1) * cell, QImage::Format_ARGB32);
        sheet.fill(QColor("#2b2118"));
        QPainter p(&sheet);
        for (int i = 0; i < units.size(); ++i) {
            const QPointF c((i % cols) * cell + cell / 2.0, (i / cols) * cell + cell / 2.0 - 8);
            art::drawUnitToken(p, c, 40, i % 2 ? QColor("#6e5a86") : QColor("#d62728"), units[i].name, 25, i % 4, false, false,
                               units[i].key, units[i].rank);
            p.setPen(Qt::white);
            p.drawText(QRectF(c.x() - cell / 2.0, c.y() + 42, cell, 16), Qt::AlignCenter, units[i].name);
        }
        const int y = ((units.size() + cols - 1) / cols) * cell + 10;
        int x = 10;
        for (const QPixmap& pm : {art::townSprite(QColor("#d62728"), 96), art::heroSprite(QColor("#1f77b4"), false, 64, reg.heroClassId("knight")),
                                  art::mineSprite("gold", QColor("#999999"), 64),
                                  art::mineSprite("wood", QColor("#d62728"), 64),
                                  art::mineSprite("stone", QColor("#999999"), 64),
                                  art::mineSprite("gems", QColor("#1f77b4"), 64),
                                  art::dwellingSprite(QColor("#999999"), reg.unitId("unicorn"), 64),
                                  art::chestSprite("wood", 64), art::artifactSprite(64)}) {
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
            const QStringList buttons = {"build", "buy", "wagon", "end", "spell", "defend", "auto", "retreat",
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
            for (const char* r : {"gold", "wood", "stone", "gems", "sulphur"}) {
                ip.drawPixmap(mx, 196, art::mineSprite(r, QColor("#d62728"), 128));
                mx += 140;
            }
            ip.drawPixmap(mx, 196, art::mineSprite("gems", QColor("#d62728"), 64));
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
                ip.drawPixmap(10 + (si % 17) * 70, 590 + (si / 17) * 70, art::spellIcon(sp.key, 64));
                ++si;
            }
            int ai = 0;
            for (const core::ArtifactDef& a : reg.artifacts()) {
                const QPixmap& pm = art::artifactIcon(a.key, 72);
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
            gear.setArtifacts({reg.artifactId("crown_of_kings"), reg.artifactId("dragon_blade"), reg.artifactId("tower_shield"), reg.artifactId("winged_boots"), reg.artifactId("ring_of_vigor"), reg.artifactId("spyglass"), reg.artifactId("war_banner")},
                              {reg.artifactId("iron_sword"), reg.artifactId("lucky_clover"), reg.artifactId("chain_mail")}, true);
            gear.resize(gear.sizeHint());
            QVERIFY(gear.sizeHint().height() > 400);
            if (!dir.isEmpty()) gear.grab().save(QDir(dir).filePath("0_equipment.png"));
            QSignalSpy wear(&gear, &EquipmentView::equipRequested);
            QTest::mouseClick(&gear, Qt::LeftButton, {}, QPoint(6 + 28, 430 + 28));  // the first backpack box
            QCOMPARE(wear.count(), 1);
            QCOMPARE(wear.first().first().value<core::ArtifactId>(), reg.artifactId("iron_sword"));
            gear.show();
            Toast* t = Toast::show(&gear, art::artifactIcon("winged_boots", 128), "<b>Found the Winged Boots!</b>");
            QVERIFY(t && t->isVisible());
        }
        // Every hero class on the map (0_heroes.png).
        {
            const QList<core::HeroClassDef> classes = reg.heroClasses();
            QImage heroes(int(classes.size()) * 100, 100, QImage::Format_ARGB32);
            heroes.fill(QColor("#5d8a3a"));
            QPainter hp(&heroes);
            for (int i = 0; i < classes.size(); ++i) hp.drawPixmap(i * 100, 0, art::heroSprite(QColor("#d62728"), false, 100, classes[i].id));
            hp.end();
            if (!dir.isEmpty()) heroes.save(QDir(dir).filePath("0_heroes.png"));
        }
        // Two heroes meet (0_meet.png): a stack and an artifact go across.
        {
            GameView v;
            v.you = v.currentPlayer = 1;
            v.players = {ViewPlayer{1, "Ann", 1, "#d62728", core::RC("humans"), false, true}};
            ViewHero a;
            a.id = 10; a.name = "Arthur"; a.classId = reg.heroClassId("knight"); a.owner = 1; a.level = 4; a.pos = {5, 5}; a.army = 20; a.own = true;
            a.artifacts = {reg.artifactId("iron_sword")}; a.backpack = {reg.artifactId("ring_of_vigor")};
            ViewHero b = a;
            b.id = 11; b.name = "Elaine"; b.classId = reg.heroClassId("wizard"); b.pos = {6, 5}; b.army = 21; b.artifacts.clear(); b.backpack.clear();
            v.heroes = {a, b};
            v.armies = {ViewArmy{20, 1, 10, {5, 5}, {ViewStack{reg.unitId("pikeman"), 12, 120, 10, 1}, ViewStack{reg.unitId("archer"), 8, 80, 10, 0}}},
                        ViewArmy{21, 1, 11, {6, 5}, {ViewStack{reg.unitId("militia"), 20, 120, 6, 0}}}};
            MeetDialog meet(&reg, nullptr);
            QSignalSpy sent(&meet, &MeetDialog::commandRequested);
            meet.showMeeting(v, 10, 11);
            QVERIFY(meet.isVisible());
            QApplication::processEvents();
            if (!dir.isEmpty()) meet.grab().save(QDir(dir).filePath("0_meet.png"));
            meet.findChild<QPushButton*>("meetGive_20_1")->click();
            QCOMPARE(sent.size(), 1);
            QCOMPARE(sent[0][0].toString(), QString("move_units"));
            QCOMPARE(sent[0][1].toJsonObject().value("to").toInt(), 21);
            meet.findChild<QPushButton*>("meetArtifact_10_ring_of_vigor")->click();
            QCOMPARE(sent[1][0].toString(), QString("give_artifact"));
            // Apart: the window closes.
            v.heroes[1].pos = {9, 5};
            meet.refresh(v);
            QVERIFY(!meet.isVisible());
        }
        // The end screen with a made-up history (0_gameover.png).
        {
            GameView v;
            v.you = 1;
            v.winnerTeam = 2;
            v.gameOver = true;
            v.players = {ViewPlayer{1, "Ann", 1, "#d62728", core::RC("humans"), false, true}, ViewPlayer{2, "AI 2", 2, "#1f77b4", core::RC("orcs"), true, false}};
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
        {  // the town screen of every race (0_scenes.png)
            const QStringList races = {"humans", "undead", "sylvan", "orcs", "dwarves", "ashborn", "tidefolk"};
            QImage scenes(900, int(races.size()) * 150, QImage::Format_ARGB32);
            QPainter sp(&scenes);
            for (int i = 0; i < races.size(); ++i)
                art::drawTownScene(sp, QRectF(0, i * 150, 900, 146), art::TownLook{QColor("#d62728"), races[i], 1, 9},
                                   {{K::Hall, 1, {}}, {K::Tavern, 0, {}}, {K::Dwelling, 1, {}}, {K::MageGuild, 1, {}}, {K::Market, 0, {}}, {K::Walls, 0, {}}});
            sp.end();
            if (!dir.isEmpty()) scenes.save(QDir(dir).filePath("0_scenes.png"));
        }
        // Hero portraits of every class.
        {
            const QList<core::HeroClassDef> classes = reg.heroClasses();
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
            vb.terrain = reg.terrainId(ground).index();
            vb.obstacles = {{5, 2}, {7, 5}, {9, 8}, {6, 9}, {10, 3}};
            ViewBattleUnit a;
            a.id = 1; a.side = 0; a.unit = reg.unitId("archer_elite"); a.count = 12; a.hex = {0, 5}; a.range = 7; a.shots = 10;
            ViewBattleUnit b;
            b.id = 2; b.side = 1; b.unit = reg.unitId("treant"); b.count = 4; b.hex = {14, 5};
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
            a.id = 1; a.side = 0; a.unit = reg.unitId("pikeman"); a.count = 30; a.hex = {9, 4};
            ViewBattleUnit c;
            c.id = 2; c.side = 0; c.unit = reg.unitId("catapult"); c.count = 1; c.hex = {0, 5};
            ViewBattleUnit d;
            d.id = 3; d.side = 1; d.unit = reg.unitId("archer_elite"); d.count = 15; d.hex = {13, 5}; d.range = 7; d.shots = 10;
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

    // The week bar fills the width and moves with the day.
    void theWeekBarShowsTheDaysOfTheWeek() {
        WeekBar bar;
        bar.resize(900, 30);
        bar.setDay(1, "Take the castle");
        QCOMPARE(bar.day(), 1);
        QCOMPARE(bar.width(), 900);
        QVERIFY(bar.toolTip().contains("Week 1"));
        const QImage first = bar.grab().toImage();
        bar.setDay(8, "Take the castle");  // a new week starts
        QCOMPARE(bar.day(), 8);
        QVERIFY(bar.toolTip().contains("Week 2"));
        const QImage second = bar.grab().toImage();
        QVERIFY(first != second);
        bar.setDay(14, "Take the castle");  // the last day of the week
        QVERIFY(bar.toolTip().contains("new units tomorrow"));
    }

    // The hero's battle spells reach any stack and say which side they hit.
    void battleSpellsSayTheirSideAndReachAnywhere() {
        const core::SpellDef* enemySpell = nullptr;
        const core::SpellDef* allySpell = nullptr;
        for (const core::SpellDef& s : reg.spells()) {
            if (s.scope != core::SpellDef::Scope::Battle) continue;
            if (!enemySpell && s.target == core::SpellDef::Target::Enemy) enemySpell = &s;
            if (!allySpell && s.target == core::SpellDef::Target::Ally) allySpell = &s;
        }
        QVERIFY(enemySpell && allySpell);

        ViewBattle vb;
        vb.present = true;
        vb.width = 15;
        vb.height = 11;
        vb.round = 1;
        vb.yourSide = 0;
        ViewBattleUnit mine;
        mine.id = 1;
        mine.side = 0;
        mine.unit = reg.unitId("pikeman");
        mine.count = 10;
        mine.hex = {1, 5};
        ViewBattleUnit friendUnit = mine;
        friendUnit.id = 3;
        friendUnit.hex = {2, 5};
        ViewBattleUnit foe;
        foe.id = 2;
        foe.side = 1;
        foe.unit = reg.unitId("treant");
        foe.count = 4;
        foe.hex = {14, 5};
        vb.units = {mine, friendUnit, foe};
        vb.active = mine.id;
        vb.hasHero = true;
        vb.heroSpellPoints = 10;
        vb.spells = {{enemySpell->id, true, enemySpell->cost, true}, {allySpell->id, false, allySpell->cost, true}};

        BattleView view(&reg);
        view.resize(1000, 620);
        view.setBattle(vb, QColor("#d62728"), QColor("#6e5a86"));
        view.show();
        QApplication::processEvents();
        QVERIFY(view.findChild<QPushButton*>("castSpellButton")->toolTip().contains("any stack"));
        auto* info = static_cast<TextLine*>(view.findChild<QWidget*>("battleInfo"));
        QVERIFY(info);

        // The hero's enemy spell: the far enemy is a target, our own stack is not.
        view.aimSpell(enemySpell->id, false);
        QVERIFY(view.isAimingSpell());
        QVERIFY(view.spellTargetsEnemy());
        QVERIFY(view.spellAimsAt(foe.hex));
        QVERIFY(!view.spellAimsAt(friendUnit.hex));
        QVERIFY(info->text().contains("any distance"));
        QVERIFY(info->text().contains("enemy stack"));

        // The hero's friendly spell: the other way around.
        view.aimSpell(allySpell->id, false);
        QVERIFY(!view.spellTargetsEnemy());
        QVERIFY(view.spellAimsAt(friendUnit.hex));
        QVERIFY(!view.spellAimsAt(foe.hex));
        QVERIFY(info->text().contains("any distance"));
        QVERIFY(info->text().contains("your stacks"));

        view.aimSpell(core::SpellId(), false);
        QVERIFY(!view.isAimingSpell());
        QVERIFY(!view.spellAimsAt(foe.hex));
        QVERIFY(SpellBook::targetText(*enemySpell).contains("any distance"));
        QVERIFY(SpellBook::targetText(*allySpell).contains("any distance"));
    }

    // The acting stack's own spells aim the same way, at any distance.
    void theStackSpellsAlsoReachAnywhere() {
        const core::SpellDef* enemySpell = nullptr;
        for (const core::SpellDef& s : reg.spells())
            if (s.scope == core::SpellDef::Scope::Battle && s.target == core::SpellDef::Target::Enemy) {
                enemySpell = &s;
                break;
            }
        QVERIFY(enemySpell);

        ViewBattle vb;
        vb.present = true;
        vb.width = 15;
        vb.height = 11;
        vb.round = 1;
        vb.yourSide = 0;
        ViewBattleUnit mine;
        mine.id = 1;
        mine.side = 0;
        mine.unit = reg.unitId("pikeman");
        mine.count = 10;
        mine.hex = {1, 5};
        ViewBattleUnit foe;
        foe.id = 2;
        foe.side = 1;
        foe.unit = reg.unitId("treant");
        foe.count = 4;
        foe.hex = {14, 5};
        vb.units = {mine, foe};
        vb.active = mine.id;
        vb.unitSpells = {{enemySpell->id, true, enemySpell->cost, true}};

        BattleView view(&reg);
        view.resize(1000, 620);
        view.setBattle(vb, QColor("#d62728"), QColor("#6e5a86"));
        view.show();
        QApplication::processEvents();
        QVERIFY(view.findChild<QPushButton*>("unitCastButton")->toolTip().contains("any stack"));
        auto* info = static_cast<TextLine*>(view.findChild<QWidget*>("battleInfo"));
        QVERIFY(info);

        view.aimSpell(enemySpell->id, true);
        QVERIFY(view.isAimingSpell());
        QVERIFY(view.spellTargetsEnemy());
        QVERIFY(view.spellAimsAt(foe.hex));
        QVERIFY(!view.spellAimsAt(mine.hex));
        QVERIFY(info->text().contains("any distance"));
        QVERIFY(info->text().contains("enemy stack"));
    }

    // The spell book is an open book with a replaceable PNG illustration per spell.
    void spellBookIsAnIllustratedOpenBook() {
        const core::SpellDef* spell = nullptr;
        for (const core::SpellDef& s : reg.spells())
            if (s.scope == core::SpellDef::Scope::Battle) { spell = &s; break; }
        QVERIFY(spell);
        QVERIFY2(art::hasPicture(art::path::spell(spell->key)), "the illustration must be a PNG in the art directory");

        SpellBook book(&reg, {{spell->id, true, {}}}, QStringLiteral("A spell book"), nullptr);
        QVERIFY(book.findChild<QFrame*>("spellBookLeftPage"));
        QVERIFY(book.findChild<QFrame*>("spellBookRightPage"));
        QVERIFY(book.findChild<QFrame*>("spellBookSpine"));
        auto* card = book.findChild<QPushButton*>(QStringLiteral("spell_%1").arg(spell->key));
        QVERIFY(card && card->isEnabled());
        auto* illustration = book.findChild<QLabel*>(QStringLiteral("spellIllustration_%1").arg(spell->key));
        QVERIFY(illustration);
        // Qt 6 returns the pixmap by value (Qt 5 returned a pointer).
        const QPixmap pic = illustration->pixmap();
        QVERIFY(!pic.isNull());
        QCOMPARE(pic.size(), QSize(112, 112));
        QVERIFY(book.findChild<QPushButton*>("spellBookClose"));
    }

    // Spells are laid out across the two pages, in data order.
    void spellBookSplitsSpellsAcrossItsPages() {
        QList<SpellBook::Entry> entries;
        for (const core::SpellDef& s : reg.spells()) {
            if (s.scope != core::SpellDef::Scope::Battle) continue;
            entries.append({s.id, true, {}});
            if (entries.size() == 3) break;
        }
        QCOMPARE(entries.size(), 3);
        SpellBook book(&reg, entries, QStringLiteral("Three spells"), nullptr);
        auto* left = book.findChild<QFrame*>("spellBookLeftPage");
        auto* right = book.findChild<QFrame*>("spellBookRightPage");
        QVERIFY(left && right);
        QCOMPARE(left->findChildren<QPushButton*>().size(), 2);
        QCOMPARE(right->findChildren<QPushButton*>().size(), 1);
    }

    // S opens the hero's spell book, X the acting stack's, from the battle map.
    void spellShortcutsOpenTheTwoBooks() {
        const core::SpellDef* heroSpell = nullptr;
        const core::SpellDef* unitSpell = nullptr;
        for (const core::SpellDef& s : reg.spells()) {
            if (s.scope != core::SpellDef::Scope::Battle || s.target != core::SpellDef::Target::Enemy) continue;
            if (!heroSpell) heroSpell = &s;
            else if (!unitSpell && s.key != heroSpell->key) unitSpell = &s;
        }
        QVERIFY(heroSpell && unitSpell);

        ViewBattle vb;
        vb.present = true;
        vb.width = 15;
        vb.height = 11;
        vb.round = 1;
        vb.yourSide = 0;
        ViewBattleUnit mine;
        mine.id = 1;
        mine.side = 0;
        mine.unit = reg.unitId("pikeman");
        mine.count = 10;
        mine.hex = {1, 5};
        ViewBattleUnit foe;
        foe.id = 2;
        foe.side = 1;
        foe.unit = reg.unitId("treant");
        foe.count = 4;
        foe.hex = {14, 5};
        vb.units = {mine, foe};
        vb.active = mine.id;
        vb.hasHero = true;
        vb.heroSpellPoints = 20;
        vb.spells = {{heroSpell->id, true, heroSpell->cost, true}};
        vb.unitSpells = {{unitSpell->id, true, unitSpell->cost, true}};

        BattleView view(&reg);
        view.resize(1000, 620);
        view.setBattle(vb, QColor("#d62728"), QColor("#6e5a86"));
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        view.activateWindow();
        QTRY_VERIFY_WITH_TIMEOUT(view.isActiveWindow(), 5000);

        auto* castButton = view.findChild<QPushButton*>("castSpellButton");
        auto* unitButton = view.findChild<QPushButton*>("unitCastButton");
        QVERIFY(castButton->isVisible() && castButton->isEnabled());
        QVERIFY(unitButton->isVisible() && unitButton->isEnabled());
        QVERIFY(view.findChild<QShortcut*>("heroSpellShortcut"));
        QVERIFY(view.findChild<QShortcut*>("unitSpellShortcut"));

        // The modal book waits in exec(); a timer watches for it and picks its
        // first enabled spell card so the key press can finish and aim the spell.
        const auto pressAndPick = [&](Qt::Key key, const QString& expectedTitle) {
            QString seenTitle;
            bool shown = false;
            QTimer watcher;
            watcher.setInterval(20);
            connect(&watcher, &QTimer::timeout, &view, [&]() {
                auto* book = view.findChild<QDialog*>("spellBook");
                if (!book || !book->isVisible()) return;
                seenTitle = book->windowTitle();
                shown = true;
                watcher.stop();
                for (QPushButton* card : book->findChildren<QPushButton*>())
                    if (card->objectName().startsWith(QStringLiteral("spell_")) && card->isEnabled()) {
                        card->click();
                        return;
                    }
            });
            watcher.start();
            // A modal book takes the active window with it. Wait for the battle
            // view to get it back before pressing the next shortcut.
            view.activateWindow();
            QTRY_VERIFY_WITH_TIMEOUT(view.isActiveWindow(), 5000);
            view.setFocus();
            QTRY_VERIFY_WITH_TIMEOUT(view.hasFocus(), 5000);
            QTest::keyClick(&view, key);
            QTRY_VERIFY_WITH_TIMEOUT(shown, 5000);
            QVERIFY2(seenTitle.contains(expectedTitle), qPrintable(seenTitle));
        };

        pressAndPick(Qt::Key_S, QStringLiteral("hero"));
        QVERIFY(view.isAimingSpell());
        view.aimSpell(core::SpellId(), false);

        pressAndPick(Qt::Key_X, QStringLiteral("stack"));
        QVERIFY(view.isAimingSpell());
    }

    // The final events animate, so the result dialog can wait for the last blow.
    void theBattleFieldShowsEventsAndReportsWhenDone() {
        ViewBattle vb;
        vb.present = true;
        vb.width = 15;
        vb.height = 11;
        vb.round = 1;
        vb.yourSide = 0;
        ViewBattleUnit mine;
        mine.id = 1;
        mine.side = 0;
        mine.unit = reg.unitId("pikeman");
        mine.count = 10;
        mine.hex = {1, 5};
        ViewBattleUnit foe;
        foe.id = 2;
        foe.side = 1;
        foe.unit = reg.unitId("treant");
        foe.count = 4;
        foe.hex = {14, 5};
        vb.units = {mine, foe};
        vb.active = mine.id;

        BattleView view(&reg);
        view.resize(1000, 620);
        view.setBattle(vb, QColor("#d62728"), QColor("#6e5a86"));
        view.show();
        QApplication::processEvents();
        QVERIFY(!view.isAnimating());

        // The kind of event the server sends with the battle-ending action.
        const QJsonArray events{{QJsonObject{{"type", QStringLiteral("attack")}, {"unit", 1},
                                             {"target", 2}, {"x", 14}, {"y", 5}}}};
        view.playEvents(events);
        QVERIFY(view.isAnimating());
        QTest::qWait(1500);  // an attack is 0.4 s; the field settles when the queue empties
        QVERIFY(!view.isAnimating());
    }

    // The attack cursor's sword turns to show which way the blow lands.
    void attackSwordTurnsWithTheDirectionOfTheBlow() {
        using art::Pointer;
        const QPointF o(0, 0);
        // The sword points up-right at rest, so +45° makes it follow the blow.
        QCOMPARE(BattleView::attackPointerDegrees(o, QPointF(10, 0)), 45);   // strike right
        QCOMPARE(BattleView::attackPointerDegrees(o, QPointF(0, 10)), 135);  // strike down
        QCOMPARE(BattleView::attackPointerDegrees(o, QPointF(-10, 0)), 225); // left
        QCOMPARE(BattleView::attackPointerDegrees(o, QPointF(0, -10)), 315); // up
        // Every direction snaps to a step and stays in range.
        for (int dx = -10; dx <= 10; ++dx)
            for (int dy = -10; dy <= 10; ++dy) {
                const int deg = BattleView::attackPointerDegrees(o, QPointF(dx, dy));
                QVERIFY(deg >= 0 && deg < 360);
                QCOMPARE(deg % art::kPointerAngleStep, 0);
            }
        // The turned art differs by direction and is padded so the blade fits.
        const QPixmap right = art::pointerImage(Pointer::MoveAttack, BattleView::attackPointerDegrees(o, QPointF(10, 0)));
        const QPixmap left = art::pointerImage(Pointer::MoveAttack, BattleView::attackPointerDegrees(o, QPointF(-10, 0)));
        QVERIFY(!right.isNull() && !left.isNull());
        QCOMPARE(right.size(), left.size());
        QVERIFY(right.width() > art::kPointerSize);
        QVERIFY(!(right.toImage() == left.toImage()));
    }

    // Hovering an unvisited hermit's hut names the place but not the secret wish.
    void unvisitedHermitHidesHisWish() {
        GameView v;
        v.width = 4;
        v.height = 4;
        v.tiles = QList<int>(v.width * v.height, reg.terrainId(QStringLiteral("grass")).index());
        ViewObject hut;
        hut.id = 1;
        hut.pos = {1, 1};
        hut.site = QStringLiteral("quest_hut");
        // Before a visit the server sends no quest and no giftFor hint at all.
        v.objects = {hut};

        MapView map(&reg);
        map.resize(4 * 48 + 20, 4 * 48 + 20);
        map.setTileSize(48);
        map.setView(v);
        const QString text = map.describeTile({1, 1});
        QVERIFY2(text.contains(QStringLiteral("Hermit's Hut")), qPrintable(text));
        QVERIFY2(text.contains(core::siteDescription(QStringLiteral("quest_hut"))), qPrintable(text));
        QVERIFY2(!text.contains(QStringLiteral("something for this hero")), qPrintable(text));
        QVERIFY2(!text.contains(QStringLiteral("nothing more for this hero")), qPrintable(text));
        QVERIFY2(!text.contains(QStringLiteral("Bring")), qPrintable(text));
    }

    // A quest whose wish was granted is marked as spent, on the map and in its text.
    void finishedQuestIsMarkedAsSpent() {
        GameView v;
        v.width = 4;
        v.height = 4;
        v.tiles = QList<int>(v.width * v.height, reg.terrainId(QStringLiteral("grass")).index());
        ViewObject hut;
        hut.id = 1;
        hut.pos = {1, 1};
        hut.site = QStringLiteral("quest_hut");
        hut.quest = QStringLiteral("Bring 100 gold: 500 experience.");
        v.objects = {hut};

        MapView map(&reg);
        map.resize(4 * 48 + 20, 4 * 48 + 20);
        map.setTileSize(48);
        map.setView(v);
        QVERIFY(!map.describeTile({1, 1}).contains(QStringLiteral("completed")));

        hut.questDone = true;
        v.objects = {hut};
        map.setView(v);
        const QString done = map.describeTile({1, 1});
        QVERIFY2(done.contains(QString(QChar(0x2713))), qPrintable(done));
        QVERIFY2(done.contains(QStringLiteral("completed")), qPrintable(done));
        // A spent hut looks different from a fresh one, so the map marks it too.
        QVERIFY(art::siteSprite(QStringLiteral("quest_hut"), false, 48).toImage() !=
                art::siteSpriteDone(QStringLiteral("quest_hut"), 48).toImage());
    }

    // New games do not start with fog; with fog on, unseen land is plainly shaded.
    void fogIsOffByDefaultAndUnseenLandIsDark() {
        QVERIFY(!net::LobbySettings().fogOfWar);
        QVERIFY(MapView::kUnseenShadeAlpha >= 180);

        GameView v;
        v.width = 4;
        v.height = 1;
        const int grass = reg.terrainId(QStringLiteral("grass")).index();
        v.tiles = QList<int>({grass, grass, grass, grass});
        v.visible = QByteArray(2, char(1)) + QByteArray(2, char(0));  // last two: known but out of sight

        MapView map(&reg);
        map.resize(4 * 64 + 20, 64 + 20);
        map.setTileSize(64);
        map.setView(v);
        map.show();
        QTest::qWait(50);

        const QImage img = map.viewport()->grab().toImage();
        const auto brightness = [&](int x) {
            long long sum = 0;
            int n = 0;
            const QRect r = map.tileRect({x, 0}).adjusted(8, 8, -8, -8);
            for (int yy = r.top(); yy < r.bottom(); ++yy)
                for (int xx = r.left(); xx < r.right(); ++xx) {
                    if (xx < 0 || yy < 0 || xx >= img.width() || yy >= img.height()) continue;
                    const QRgb p = img.pixel(xx, yy);
                    sum += (qRed(p) + qGreen(p) + qBlue(p)) / 3;
                    ++n;
                }
            return n ? double(sum) / n : -1.0;
        };
        const double seen = brightness(0), unseen = brightness(2);
        QVERIFY2(seen > 0, qPrintable(QStringLiteral("seen %1 unseen %2").arg(seen).arg(unseen)));
        QVERIFY2(unseen >= 0 && unseen < seen * 0.45,
                 qPrintable(QStringLiteral("seen %1 unseen %2").arg(seen).arg(unseen)));
    }

    // The game-over screen explains a loss from the weekly numbers, and calls out
    // hoarding an army instead of spending it ("too careful").
    void gameOverAnalysisExplainsTheLossAndTooCarefulPlay() {
        const auto week = [](int w, int myArmy, int myGold, int myTowns, int myMines, int myHeroes,
                             int foeArmy, int foeGold, int foeTowns, int foeMines, int foeHeroes) {
            return QJsonObject{{"week", w}, {"players", QJsonObject{
                {"1", QJsonObject{{"armyValue", myArmy}, {"gold", myGold}, {"towns", myTowns},
                                  {"mines", myMines}, {"heroes", myHeroes}}},
                {"2", QJsonObject{{"armyValue", foeArmy}, {"gold", foeGold}, {"towns", foeTowns},
                                  {"mines", foeMines}, {"heroes", foeHeroes}}}}}};
        };
        GameView v;
        v.you = 1;
        v.gameOver = true;
        v.players = {ViewPlayer{1, "Ann", 1, "#d62728", core::RC("humans"), false, false},
                     ViewPlayer{2, "Boss", 2, "#1f77b4", core::RC("orcs"), true, false}};

        // No history to go on: no notes.
        QVERIFY(GameOverDialog::analysis(v).isEmpty());

        // A lopsided loss where the player sat on a pile of gold with a small army.
        v.winnerTeam = 2;
        v.history = QJsonArray{week(1, 3000, 1000, 1, 1, 1, 3000, 500, 1, 1, 1),
                               week(9, 4000, 30000, 1, 2, 1, 40000, 500, 5, 6, 3)};
        const QStringList notes = GameOverDialog::analysis(v);
        QVERIFY2(!notes.isEmpty(), "a lopsided loss should produce notes");
        const QString all = notes.join(QStringLiteral(" "));
        QVERIFY2(all.contains(QStringLiteral("too careful")), qPrintable(all));
        QVERIFY2(all.contains(QStringLiteral("force")), qPrintable(all));
        QVERIFY2(all.contains(QStringLiteral("towns")), qPrintable(all));
        QVERIFY2(all.contains(QStringLiteral("mines")), qPrintable(all));

        // The notes are actually shown in the dialog, in a word-wrapped label.
        GameOverDialog end(v, QStringLiteral("Game over. Your team has lost."));
        auto* label = end.findChild<QLabel*>(QStringLiteral("gameOverAnalysis"));
        QVERIFY(label);
        QVERIFY2(label->text().contains(QStringLiteral("too careful")), qPrintable(label->text()));
        QVERIFY(label->wordWrap());

        // A close loss: only the "came down to the battles" note.
        v.history = QJsonArray{week(1, 3000, 500, 1, 1, 1, 3000, 500, 1, 1, 1),
                               week(9, 20000, 500, 3, 3, 2, 21000, 500, 3, 3, 2)};
        const QStringList close = GameOverDialog::analysis(v);
        QCOMPARE(close.size(), 1);
        QVERIFY2(close.first().contains(QStringLiteral("battles")), qPrintable(close.first()));

        // A win says the player finished ahead.
        v.winnerTeam = 1;
        const QStringList won = GameOverDialog::analysis(v);
        QVERIFY2(!won.isEmpty() && won.first().contains(QStringLiteral("finished ahead")), qPrintable(won.join(QStringLiteral(" "))));
    }

    // Hot seat: the host's second slot is a human at this computer, not an AI.
    void hotSeatStartsWithTwoHumansHere() {
        server::GameServer server({"humans"}, {"passive"});
        server.setContent(&reg);
        QVERIFY(server.start(0));

        MainWindow w;
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        w.connectToServer("127.0.0.1", server.port(), "Ann", {}, false, true);
        QTRY_COMPARE_WITH_TIMEOUT(w.currentPage(), QString("lobby"), 5000);
        QTRY_COMPARE_WITH_TIMEOUT(w.client().lobby().slotList[1].kind, net::SlotKind::HotSeat,
                                  5000);
        QCOMPARE(w.client().lobby().slotList[1].name, QStringLiteral("Player 2"));
        QCOMPARE(w.client().lobby().slotList[1].clientId, w.client().clientId());
        QVERIFY(w.client().lobby().slotList[1].ready);
        QCOMPARE(w.client().lobby().slotList[2].kind, net::SlotKind::Open);
    }

    // Enemy turns come in one view: say whose mine or town changed hands.
    void enemyTakeoversAreReported() {
        GameView before, after;
        before.you = after.you = 1;
        after.players = {ViewPlayer{1, QStringLiteral("Ann"), 1, QStringLiteral("#d62728"), core::RC("humans"), false, false},
                         ViewPlayer{2, QStringLiteral("Bob"), 2, QStringLiteral("#1f77b4"), core::RC("humans"), false, false}};
        ViewMine mine;
        mine.id = 5;
        mine.owner = 1;
        mine.pos = {3, 3};
        before.mines = {mine};
        ViewMine taken = mine;
        taken.owner = 2;
        after.mines = {taken};
        ViewTown town;
        town.id = 7;
        town.name = QStringLiteral("Home");
        town.owner = 2;
        town.pos = {1, 1};
        before.towns = {town};
        ViewTown recovered = town;
        recovered.owner = 1;
        after.towns = {recovered};

        const QStringList changes = GamePage::ownershipChanges(before, after);
        QVERIFY2(changes.contains(QStringLiteral("Bob took your mine.")), qPrintable(changes.join(", ")));
        QVERIFY2(changes.contains(QStringLiteral("You took a town from Bob.")), qPrintable(changes.join(", ")));
        // The first view and an unchanged view say nothing.
        GameView empty;
        QVERIFY(GamePage::ownershipChanges(empty, after).isEmpty());
        QVERIFY(GamePage::ownershipChanges(before, before).isEmpty());
    }

    void playThroughTheClient() {
        QTemporaryDir saves;  // Ctrl+S below needs somewhere to write the save
        server::GameServer server({"humans"}, {"passive"});
        server.setContent(&reg);
        server.setSaveDir(saves.path());
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
        bob.setContentChecksum(reg.checksum());
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
        auto* week = game->findChild<QWidget*>("weekBar");
        QVERIFY(week);
        QCOMPARE(week->property("day").toInt(), game->view().day);
        QVERIFY(week->width() > game->width() / 2);  // it fills the line
        // The dwelling button explains what "home" means.
        auto* sendUnits = game->findChild<QPushButton*>("sendUnitsButton");
        QVERIFY(sendUnits);
        QVERIFY(sendUnits->toolTip().contains("nearest town"));
        QVERIFY(sendUnits->toolTip().contains("buy them for gold"));
        QApplication::processEvents();
        shot(&w, "3_game");

        // Click a free tile next to the hero: the route is plotted; Enter walks it.
        const ViewHero hero = *game->view().hero(game->selectedHero());
        core::Pos target = hero.pos;
        for (int dy = -1; dy <= 1 && target == hero.pos; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                const core::Pos t{hero.pos.x + dx, hero.pos.y + dy};
                if (t == hero.pos || !game->view().known(t)) continue;
                const core::TerrainId terrain(game->view().tiles[t.y * game->view().width + t.x]);
                if (reg.terrain(terrain).walkable && !game->view().townCovering(t)) { target = t; break; }
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
        QVERIFY(!game->logText().contains("arrived"));  // routine results are not written down
        {
            // The map repaints only what changed (MapView::updateTiles): after the walk and
            // some hovering, what is on the screen must equal a full repaint.
            QTest::qWait(1600);  // the walk animation
            for (const QPoint p : {QPoint(100, 100), QPoint(300, 200), QPoint(150, 250)}) {
                QTest::mouseMove(game->mapView()->viewport(), p);
                QTest::qWait(30);
            }
            const int differing = differsFromFullRepaint(game->mapView()->viewport(), "map");
            QVERIFY2(differing < 40, qPrintable(QStringLiteral("%1 pixels differ from a full repaint").arg(differing)));
        }

        // Right-click the town: the dialog opens, Barracks can be built.
        const ViewTown town = *game->view().myTowns().first();
        // B: the next town that can build opens on its build screen, no map stop first.
        w.activateWindow();
        QTRY_VERIFY_WITH_TIMEOUT(w.isActiveWindow(), 5000);
        game->mapView()->setFocus();
        QTRY_VERIFY_WITH_TIMEOUT(game->mapView()->hasFocus(), 5000);
        QTest::keyClick(game->mapView(), Qt::Key_B);
        QTRY_VERIFY_WITH_TIMEOUT(game->townDialog()->isVisible(), 5000);
        QCOMPARE(game->townDialog()->townId(), town.id);
        {
            auto* tabs = game->townDialog()->findChild<QTabWidget*>();
            QVERIFY(tabs);
            QCOMPARE(tabs->currentIndex(), 0);
            QCOMPARE(tabs->tabText(0), QString("Buildings"));
        }
        game->townDialog()->hide();
        w.activateWindow();
        QTRY_VERIFY_WITH_TIMEOUT(w.isActiveWindow(), 5000);
        // And by mouse: the dialog opens, Barracks can be built.
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
        QTRY_VERIFY_WITH_TIMEOUT(game->view().town(town.id)->buildings.contains(kot::core::B("barracks")), 5000);
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
        // Hiding the active dialog leaves no active window (no window manager hands it
        // back), and setFocus() only works in an active window: the game page's
        // shortcuts (WidgetWithChildrenShortcut) need one.
        w.activateWindow();
        QTRY_VERIFY_WITH_TIMEOUT(w.isActiveWindow(), 5000);

        // B again: the town has built today, so a note says so and nothing opens.
        game->mapView()->setFocus();
        QTRY_VERIFY_WITH_TIMEOUT(game->mapView()->hasFocus(), 5000);
        QTest::keyClick(game->mapView(), Qt::Key_B);
        const auto nothingToBuildToast = [game]() {
            for (Toast* t : game->findChildren<Toast*>())
                for (QLabel* l : t->findChildren<QLabel*>())
                    if (l->text().contains("No town can build")) return true;
            return false;
        };
        QTRY_VERIFY_WITH_TIMEOUT(nothingToBuildToast(), 3000);
        QVERIFY(!game->townDialog()->isVisible());

        // End the turn: E ends it and a note says so at once (no extra confirmation).
        auto* endTurn = game->findChild<QPushButton*>("endTurnButton");
        QVERIFY(endTurn && endTurn->isEnabled());
        const auto turnEndedToast = [game]() {
            for (Toast* t : game->findChildren<Toast*>())
                for (QLabel* l : t->findChildren<QLabel*>())
                    if (l->text().contains("Turn ended")) return true;
            return false;
        };
        game->mapView()->setFocus();
        QTRY_VERIFY_WITH_TIMEOUT(game->mapView()->hasFocus(), 5000);
        QTest::keyClick(game->mapView(), Qt::Key_E);
        QTRY_VERIFY_WITH_TIMEOUT(turnEndedToast(), 1200);  // at once, not after the server replies
        QTRY_VERIFY_WITH_TIMEOUT(!game->view().isMyTurn(), 5000);
        QVERIFY(!endTurn->isEnabled());
        bob.sendCommand("end_turn");
        QTRY_VERIFY_WITH_TIMEOUT(game->view().isMyTurn() && game->view().day == 2, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(endTurn->isEnabled(), 5000);
        QVERIFY(game->logText().contains("Day 2"));
        QTRY_VERIFY_WITH_TIMEOUT(!turnEndedToast(), 3000);  // short: it does not linger
        QTRY_COMPARE_WITH_TIMEOUT(week->property("day").toInt(), 2, 5000);
        QApplication::processEvents();
        shot(&w, "6_game_day2");

        // The help page from the Game menu.
        game->findChild<QAction*>("helpAction")->trigger();
        auto* help = game->findChild<QDialog*>("helpDialog");
        QVERIFY(help && help->isVisible());
        QVERIFY(help->findChild<QTextBrowser*>("helpText")->toPlainText().contains("zone of control"));
        QApplication::processEvents();
        shot(help, "6a_help");
        QVERIFY(help->findChild<QTextBrowser*>("helpText")->toPlainText().contains("Ctrl+S"));
        help->hide();

        // Ctrl+S saves through the same dialog and path as the Game menu action.
        w.activateWindow();
        QTRY_VERIFY_WITH_TIMEOUT(w.isActiveWindow(), 5000);
        game->mapView()->setFocus();
        QTRY_VERIFY_WITH_TIMEOUT(game->mapView()->hasFocus(), 5000);
        bool saveDialogShown = false;
        QTimer saveWatcher;
        saveWatcher.setInterval(20);
        connect(&saveWatcher, &QTimer::timeout, &w, [&]() {
            auto* dialog = w.findChild<SaveGameDialog*>();
            if (!dialog || !dialog->isVisible()) return;
            saveDialogShown = true;
            saveWatcher.stop();
            auto* name = dialog->findChild<QLineEdit*>(QStringLiteral("saveName"));
            if (!name) return;
            name->setText(QStringLiteral("day2_shortcut"));
            dialog->accept();
        });
        saveWatcher.start();
        QTest::keyClick(game->mapView(), Qt::Key_S, Qt::ControlModifier);
        QTRY_VERIFY_WITH_TIMEOUT(saveDialogShown, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(game->logText().contains("saved as \"day2_shortcut\""), 5000);
        QVERIFY(game->findChild<QAction*>("saveAction")->text().contains("Ctrl+S"));

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
        QTRY_VERIFY_WITH_TIMEOUT(game->logText().contains("Buy all units: "), 5000);

        // A battle: monsters appear next to Ann's hero, she clicks them.
        core::GameState& gs = server.session()->state();
        core::Hero* annHero = gs.hero(core::HeroId(quint32(hero.id)));
        QVERIFY(annHero);
        // One XP short of the next level: any XP from the win will level her, so
        // the result/level-up dialog order can be checked when the battle ends.
        annHero->experience = static_cast<int>(reg.xpForNextLevel(annHero->level)) - 1;
        const core::Pos at = gs.army(annHero->army)->pos;
        core::Pos spot{-1, -1};
        for (int dy = -1; dy <= 1 && spot.x < 0; ++dy)
            for (int dx = -1; dx <= 1 && spot.x < 0; ++dx) {
                const core::Pos p{at.x + dx, at.y + dy};
                bool free = (dx || dy) && gs.map().inBounds(p) && reg.terrain(gs.map().terrainAt(p)).walkable;
                for (const core::Army& a : gs.armies()) free = free && a.pos != p;
                free = free && !gs.townCovering(p);
                for (const core::Mine& m : gs.mines()) free = free && m.pos != p;
                for (const core::MapDwelling& d : gs.mapDwellings()) free = free && d.pos != p;
                if (free) spot = p;
            }
        QVERIFY(spot.x >= 0);
        gs.army(gs.addArmy(core::PlayerId(), spot))->addUnits(reg.unitId("militia"), 4, 6);
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
        if (game->view().battle.present) {  // the field repaints only what moves: the screen must equal a full repaint
            QTest::qWait(1500);
            const int differing = differsFromFullRepaint(game->battleView()->field(), "battle");
            QVERIFY2(differing < 40, qPrintable(QStringLiteral("%1 battle pixels differ from a full repaint").arg(differing)));
        }

        // Let the computer finish it.
        if (game->view().battle.present) {
            QTRY_VERIFY_WITH_TIMEOUT(game->view().battle.myMove(), 5000);
            auto* autoButton = game->battleView()->findChild<QPushButton*>("autoBattleButton");
            QTest::mouseClick(autoButton, Qt::LeftButton);
        }
        QTRY_VERIFY_WITH_TIMEOUT(!game->view().battle.present, 5000);
        // The result comes up on the map, with the battle field already put away,
        // and no level-up window is allowed to cover it.
        QTRY_VERIFY_WITH_TIMEOUT(game->findChild<QWidget*>("battleResult") != nullptr, 5000);
        QVERIFY(game->mapView()->isVisible());
        QVERIFY(!game->battleView()->isVisible());
        auto* result = game->findChild<QWidget*>("battleResult");
        QVERIFY(result->findChild<QLabel*>("battleOutcome")->text().contains("Victory"));  // 4 militia stood no chance
        QVERIFY2(!game->findChild<QWidget*>("levelUpDialog"),
                 "the level-up must wait until the battle result is dismissed");
        QApplication::processEvents();
        shot(result, "8b_battle_result");
        shot(&w, "8_after_battle");

        // Dismissing the result is what lets the level-up appear.
        QTest::mouseClick(result->findChild<QPushButton*>("battleResultOk"), Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(game->findChild<QWidget*>("levelUpDialog") != nullptr, 5000);
        shot(game->findChild<QWidget*>("levelUpDialog"), "8c_level_up");
        QTest::mouseClick(game->findChild<QPushButton*>("levelUpChooseNow"), Qt::LeftButton);
    }
};

QTEST_MAIN(TstClientSmoke)
#include "tst_clientsmoke.moc"
