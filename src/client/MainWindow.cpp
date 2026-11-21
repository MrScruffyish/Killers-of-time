#include "client/MainWindow.h"

#include <QCoreApplication>
#include <algorithm>
#include <QFileInfo>
#include <QGuiApplication>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonObject>
#include <QScreen>
#include <QStackedWidget>
#include <QTimer>

#include "client/Art.h"

#include <QDir>
#include "client/ConnectPage.h"
#include "client/GamePage.h"
#include "client/LoadDialog.h"
#include "client/LobbyPage.h"
#include "client/Settings.h"
#include <QSettings>

#include "core/Version.h"
#include "core/data/DataDir.h"
#include "net/GameToken.h"

namespace kot::client {

namespace {
// Setup is a comfortable window with room around it; the game itself can be bigger.
constexpr int kSetupWidth = 980;
constexpr int kSetupHeight = 660;
constexpr int kGameWidth = 1100;
constexpr int kGameHeight = 740;
}  // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("%1 %2").arg(QString::fromLatin1(core::kGameName), QString::fromLatin1(core::kGameVersion)));
    resize(kSetupWidth, kSetupHeight);

    m_pages = new QStackedWidget(this);
    setCentralWidget(m_pages);
    m_connect = new ConnectPage(this);
    m_lobby = new LobbyPage(&m_client, &m_content, this);
    m_pages->addWidget(m_connect);
    m_pages->addWidget(m_lobby);
    connect(m_pages, &QStackedWidget::currentChanged, this, [this]() { useWindowSize(m_pages->currentWidget() == m_game); });

    QString contentMessage;
    const QString dataDir = core::findDataDirectory();
    if (dataDir.isEmpty()) {
        contentMessage = tr("Cannot find the game data folder. Set KOT_DATA_DIR or start the game from the project folder.");
    } else {
        QStringList errors;
        m_contentLoaded = m_content.loadFromDirectory(dataDir, &errors);
        m_previousArtContent = art::content();
        art::setContent(&m_content);  // unit tiers, and the art overloads that take ids
        // Generated PNG art (kot_artexport) with a hand-made overlay that the export never touches.
        art::setArtDirectory(QDir(dataDir).filePath(QStringLiteral("art")),
                             QDir(dataDir).filePath(QStringLiteral("custom_art")));
        m_client.setContentChecksum(m_content.checksum());  // the server checks we have the same data
        if (!m_contentLoaded) contentMessage = tr("The game data is invalid:\n%1").arg(errors.join('\n'));
    }
    showConnect(contentMessage, !contentMessage.isEmpty());

    connect(m_connect, &ConnectPage::joinRequested, this,
            [this](const QString& host, quint16 port, const QString& name, const QString& token, bool tls) {
                connectToServer(host, port, name, token, tls);
            });
    connect(m_connect, &ConnectPage::soloRequested, this, [this](quint16 port, const QString& name, bool tls) {
        hostLocalGame(port, name, tls, false, false, true);
    });
    connect(m_connect, &ConnectPage::hotseatRequested, this,
            [this](quint16 port, const QString& name, bool tls) {
                hostLocalGame(port, name, tls, true);
            });
    connect(m_connect, &ConnectPage::loadRequested, this,
            [this](quint16 port, const QString& name, bool tls) {
                hostLocalGame(port, name, tls, false, true);
            });
    connect(m_lobby, &LobbyPage::leaveRequested, this, [this]() {
        m_client.disconnectFromServer();
        showConnect(tr("You left the game."), false);
    });
    connect(&m_client, &net::LobbyClient::connected, this, [this]() {
        if (m_loadAfterConnect) {
            // Load game from the start screen: show the list here, without putting up
            // the game setup. Cancel goes back to this screen and leaves the server.
            if (!m_startLoadSeq) m_startLoadSeq = m_client.listSaves();
            return;
        }
        m_pages->setCurrentWidget(m_lobby);
    });
    connect(&m_client, &net::LobbyClient::commandResult, this, [this](int replyTo, const QJsonObject& info) {
        if (!m_startLoadSeq || replyTo != m_startLoadSeq) return;
        m_startLoadSeq = 0;
        const QJsonArray games = info["games"].toArray();
        if (games.isEmpty()) {
            showConnect(tr("There are no saved games on the server."), true);
            m_client.disconnectFromServer();
            return;
        }
        LoadDialog dialog(games, this);
        if (dialog.exec() == QDialog::Accepted && !dialog.chosen().isEmpty()) {
            m_loadAfterConnect = false;
            m_client.loadGame(dialog.chosen());
        } else {
            m_client.disconnectFromServer();
            showConnect(tr("Load cancelled."), false);
        }
    });
    connect(&m_client, &net::LobbyClient::gameStarted, this, [this]() {
        if (m_game) {
            m_pages->removeWidget(m_game);
            delete m_game;
            m_game = nullptr;
        }
        m_game = new GamePage(&m_client, &m_content, this);
        m_pages->addWidget(m_game);
        m_pages->setCurrentWidget(m_game);
    });
    connect(&m_client, &net::LobbyClient::gameAbandoned, this, [this]() {
        if (m_game) {
            m_pages->removeWidget(m_game);
            delete m_game;
            m_game = nullptr;
        }
        m_pages->setCurrentWidget(m_lobby);
    });
    connect(&m_client, &net::LobbyClient::playerChoiceOffered, this, [this]() {
        QStringList names;
        QList<int> ids;
        for (const QJsonValue& v : m_client.freePlayers()) {
            names << v.toObject()["name"].toString();
            ids << v.toObject()["id"].toInt();
        }
        if (names.isEmpty()) {
            m_lobby->showError(tr("A game is running and every player is taken."));
            return;
        }
        bool ok = false;
        const QString chosen = QInputDialog::getItem(this, tr("Join the game"), tr("A game is running. Play as:"), names, 0, false, &ok);
        if (ok) m_client.joinGame(ids[names.indexOf(chosen)]);
    });
    connect(&m_client, &net::LobbyClient::connectionFailed, this,
            [this](const QString& why) { showConnect(tr("Connection problem: %1").arg(why), true); });
}

MainWindow::~MainWindow() {
    // The art helpers take ids and look them up in this shared pointer. Leave them
    // pointing where they did before this window, not at our content after it is gone.
    if (art::content() == &m_content) art::setContent(m_previousArtContent);
    m_client.disconnectFromServer();
    if (m_serverProcess.state() != QProcess::NotRunning) {
        m_serverProcess.terminate();
        m_serverProcess.waitForFinished(1500);
    }
}

void MainWindow::showEvent(QShowEvent* event) {
    QMainWindow::showEvent(event);
    if (m_sizeRestored) return;
    m_sizeRestored = true;
    const QByteArray g = QSettings().value(QStringLiteral("geometry/setupWindow")).toByteArray();
    if (!g.isEmpty()) restoreGeometry(g);
    fitSetupWindow();
}

void MainWindow::fitSetupWindow() {
    // A saved geometry from a big display (or an old maximized window) must not
    // make the setup fill the screen.
    if (isMaximized() || isFullScreen() || isMinimized()) showNormal();
    if (width() > kSetupWidth || height() > kSetupHeight) {
        resize(std::min(width(), kSetupWidth), std::min(height(), kSetupHeight));
        QScreen* screen = QGuiApplication::screenAt(pos());
        if (!screen) screen = QGuiApplication::primaryScreen();
        if (screen) move(screen->geometry().center() - rect().center());
    }
}

void MainWindow::closeEvent(QCloseEvent* event) {
    QSettings().setValue(m_gameSize ? QStringLiteral("geometry/gameWindow") : QStringLiteral("geometry/setupWindow"), saveGeometry());
    QMainWindow::closeEvent(event);
}

void MainWindow::useWindowSize(bool game) {
    if (game == m_gameSize || !m_sizeRestored) {
        m_gameSize = game;
        return;
    }
    QSettings().setValue(m_gameSize ? QStringLiteral("geometry/gameWindow") : QStringLiteral("geometry/setupWindow"), saveGeometry());
    m_gameSize = game;
    QByteArray g = QSettings().value(game ? QStringLiteral("geometry/gameWindow") : QStringLiteral("geometry/setupWindow")).toByteArray();
    if (game && g.isEmpty()) g = QSettings().value(QStringLiteral("geometry/mainWindow")).toByteArray();  // from older versions
    if (!g.isEmpty()) {
        restoreGeometry(g);
        if (!game) fitSetupWindow();
    } else if (!game) {  // the setup: a normal window, not maximized
        fitSetupWindow();
        resize(kSetupWidth, kSetupHeight);
    } else {
        resize(kGameWidth, kGameHeight);
    }
}

void MainWindow::showConnect(const QString& message, bool error) {
    m_loadAfterConnect = false;  // back on the start screen: nothing to open in the lobby
    m_startLoadSeq = 0;
    m_pages->setCurrentWidget(m_connect);
    m_connect->showMessage(message, error);
}

QString MainWindow::currentPage() const {
    QWidget* w = m_pages->currentWidget();
    if (w == m_connect) return QStringLiteral("connect");
    if (w == m_lobby) return QStringLiteral("lobby");
    return QStringLiteral("game");
}

void MainWindow::connectToServer(const QString& host, quint16 port, const QString& name, const QString& gameToken,
                                 bool tls, bool hotSeat, bool loadSavedGame, bool allowCampaign) {
    m_loadAfterConnect = loadSavedGame;
    m_lobby->setHotSeatSetup(hotSeat);
    m_lobby->setCampaignAllowed(allowCampaign);
    if (!m_contentLoaded) {
        showConnect(tr("The game data could not be loaded, so the game cannot start."), true);
        return;
    }
    m_connect->showMessage(tr("Connecting to %1:%2 ...").arg(host).arg(port), false);
    // Our game token for this server (made the first time, or typed in to get the seats
    // played on another computer): with it the server gives our seats back.
    const QString settingsKey = QStringLiteral("gameTokens/%1_%2").arg(host).arg(port);
    QString token = net::normalizeGameToken(gameToken);
    if (!token.isEmpty()) {
        if (!net::looksLikeGameToken(token)) {
            showConnect(tr("That does not look like a game token: it is 64 hex digits (letters and digits 0-9a-f)."), true);
            return;
        }
        QSettings().setValue(settingsKey, token);  // this computer plays with it from now on
    } else {
        token = QSettings().value(settingsKey).toString();
        if (token.isEmpty()) {
            token = net::newGameToken();
            QSettings().setValue(settingsKey, token);
        }
    }
    m_client.setGameToken(token);
    m_client.connectToServer(host, port, name.isEmpty() ? tr("Player") : name, tls);
}

void MainWindow::hostLocalGame(quint16 port, const QString& name, bool tls, bool hotSeat,
                               bool loadSavedGame, bool allowCampaign) {
    const QString exe = QCoreApplication::applicationDirPath() + QStringLiteral("/kot_server");
    if (!QFileInfo::exists(exe)) {
        showConnect(tr("Cannot find kot_server next to this program (%1). Build it, or start a server yourself.").arg(exe), true);
        return;
    }
    if (m_serverProcess.state() == QProcess::NotRunning) {
        m_serverProcess.setProcessChannelMode(QProcess::ForwardedChannels);
        QStringList args{QStringLiteral("--port"), QString::number(port)};
        if (tls) args << QStringLiteral("--tls");
        m_serverProcess.start(exe, args);
        if (!m_serverProcess.waitForStarted(3000)) {
            showConnect(tr("Could not start the server: %1").arg(m_serverProcess.errorString()), true);
            return;
        }
    }
    m_connect->showMessage(tr("Starting a local server ..."), false);
    QTimer::singleShot(900, this, [this, port, name, tls, hotSeat, loadSavedGame, allowCampaign]() {
        connectToServer(QStringLiteral("127.0.0.1"), port, name, {}, tls, hotSeat, loadSavedGame, allowCampaign);
    });
}

}  // namespace kot::client
