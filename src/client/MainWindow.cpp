#include "client/MainWindow.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonObject>
#include <QStackedWidget>
#include <QTimer>

#include "client/ConnectPage.h"
#include "client/GamePage.h"
#include "client/LobbyPage.h"
#include "core/Version.h"
#include "core/data/DataDir.h"

namespace kot::client {

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("%1 %2").arg(QString::fromLatin1(core::kGameName), QString::fromLatin1(core::kGameVersion)));
    resize(1150, 760);

    m_pages = new QStackedWidget(this);
    setCentralWidget(m_pages);
    m_connect = new ConnectPage(this);
    m_lobby = new LobbyPage(&m_client, this);
    m_pages->addWidget(m_connect);
    m_pages->addWidget(m_lobby);

    QString contentMessage;
    const QString dataDir = core::findDataDirectory();
    if (dataDir.isEmpty()) {
        contentMessage = tr("Cannot find the game data folder. Set KOT_DATA_DIR or start the game from the project folder.");
    } else {
        QStringList errors;
        m_contentLoaded = m_content.loadFromDirectory(dataDir, &errors);
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
