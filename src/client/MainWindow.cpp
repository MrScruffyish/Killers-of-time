#include "client/MainWindow.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonObject>
#include <QStackedWidget>
#include <QTimer>

#include "client/Art.h"

#include <QDir>
#include "client/ConnectPage.h"
#include "client/GamePage.h"
#include "client/LobbyPage.h"
#include "client/Settings.h"
#include <QSettings>

#include "core/Version.h"
#include "core/data/DataDir.h"

namespace kot::client {

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("%1 %2").arg(QString::fromLatin1(core::kGameName), QString::fromLatin1(core::kGameVersion)));
    resize(1100, 740);

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
        art::setContent(&m_content);  // unit tiers, and the art overloads that take ids
        art::setArtDirectory(QDir(dataDir).filePath(QStringLiteral("art")));  // PNG art (kot_artexport); else drawn in code
        m_client.setContentChecksum(m_content.checksum());  // the server checks we have the same data
        if (!m_contentLoaded) contentMessage = tr("The game data is invalid:\n%1").arg(errors.join('\n'));
    }
    showConnect(contentMessage, !contentMessage.isEmpty());

    connect(m_connect, &ConnectPage::joinRequested, this,
            [this](const QString& host, quint16 port, const QString& name) { connectToServer(host, port, name); });
    connect(m_connect, &ConnectPage::hostRequested, this,
            [this](quint16 port, const QString& name) { hostLocalGame(port, name); });
    connect(m_lobby, &LobbyPage::leaveRequested, this, [this]() {
        m_client.disconnectFromServer();
        showConnect(tr("You left the game."), false);
    });
    connect(&m_client, &net::LobbyClient::connected, this, [this]() { m_pages->setCurrentWidget(m_lobby); });
    connect(&m_client, &net::LobbyClient::gameStarted, this, [this]() {
        if (m_game) {
            m_pages->removeWidget(m_game);
            delete m_game;
        }
        m_game = new GamePage(&m_client, &m_content, this);
        m_pages->addWidget(m_game);
        m_pages->setCurrentWidget(m_game);
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
    } else if (!game) {  // the setup: a normal window, not maximized
        showNormal();
        resize(1100, 740);
    }
}

void MainWindow::showConnect(const QString& message, bool error) {
    m_pages->setCurrentWidget(m_connect);
    m_connect->showMessage(message, error);
}

QString MainWindow::currentPage() const {
    QWidget* w = m_pages->currentWidget();
    if (w == m_connect) return QStringLiteral("connect");
    if (w == m_lobby) return QStringLiteral("lobby");
    return QStringLiteral("game");
}

void MainWindow::connectToServer(const QString& host, quint16 port, const QString& name) {
    if (!m_contentLoaded) {
        showConnect(tr("The game data could not be loaded, so the game cannot start."), true);
        return;
    }
    m_connect->showMessage(tr("Connecting to %1:%2 ...").arg(host).arg(port), false);
    // Our secret key for this server (made the first time): with it we get our seats back.
    const QString settingsKey = QStringLiteral("playerKeys/%1_%2").arg(host).arg(port);
    QString key = QSettings().value(settingsKey).toString();
    if (key.isEmpty()) {
        key = net::LobbyClient::newPlayerKey();
        QSettings().setValue(settingsKey, key);
    }
    m_client.setPlayerKey(key);
    m_client.connectToServer(host, port, name.isEmpty() ? tr("Player") : name);
}

void MainWindow::hostLocalGame(quint16 port, const QString& name) {
    const QString exe = QCoreApplication::applicationDirPath() + QStringLiteral("/kot_server");
    if (!QFileInfo::exists(exe)) {
        showConnect(tr("Cannot find kot_server next to this program (%1). Build it, or start a server yourself.").arg(exe), true);
        return;
    }
    if (m_serverProcess.state() == QProcess::NotRunning) {
        m_serverProcess.setProcessChannelMode(QProcess::ForwardedChannels);
        m_serverProcess.start(exe, {QStringLiteral("--port"), QString::number(port)});
        if (!m_serverProcess.waitForStarted(3000)) {
            showConnect(tr("Could not start the server: %1").arg(m_serverProcess.errorString()), true);
            return;
        }
    }
    m_connect->showMessage(tr("Starting a local server ..."), false);
    QTimer::singleShot(900, this, [this, port, name]() { connectToServer(QStringLiteral("127.0.0.1"), port, name); });
}

}  // namespace kot::client
