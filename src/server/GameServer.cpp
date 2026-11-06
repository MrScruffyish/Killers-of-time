#include "server/GameServer.h"
#include "net/ActionTiming.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QRegularExpression>
#include <QSaveFile>
#include <algorithm>

#include "core/Version.h"
#include "core/rules/ScenarioGen.h"
#include "core/rules/Scenario.h"
#include "core/content/ContentRegistry.h"
#include "core/rules/Campaign.h"
#include "core/rules/Scenario.h"
#include "core/util/Log.h"
#include "net/Protocol.h"

namespace kot::server {

using net::Message;

GameServer::GameServer(QStringList races, QStringList aiProfiles, QObject* parent)
    : QObject(parent), m_races(races), m_aiProfiles(aiProfiles), m_lobby(races, aiProfiles) {
    connect(&m_server, &QTcpServer::newConnection, this, &GameServer::onNewConnection);
}

void GameServer::setContent(const core::ContentRegistry* content) {
    m_content = content;
    refreshContentLists();
}

QStringList GameServer::contentDirs(const QString& kind) const {
    QStringList dirs;
    if (m_content && !m_content->dataDir().isEmpty()) dirs << QDir(m_content->dataDir()).filePath(kind);
    if (!m_saveDir.isEmpty()) dirs << QDir(m_saveDir).filePath(kind);  // generated in the lobby
    return dirs;
}

void GameServer::refreshContentLists() {
    m_scenarios = QJsonArray();
    QStringList ids;
    if (m_content)
        for (const QString& dir : contentDirs("scenarios"))
            for (const core::ScenarioInfo& s : core::listScenarios(dir, *m_content)) {
                if (ids.contains(s.id)) continue;
                m_scenarios.append(QJsonObject{{"id", s.id}, {"name", s.name}, {"description", s.description},
                                               {"width", s.width}, {"height", s.height},
                                               {"races", QJsonArray::fromStringList(s.races)}});
                ids << s.id;
            }
    m_lobby.setScenarioIds(ids);
    QStringList campaigns;
    for (const QString& dir : contentDirs("campaigns"))
        for (const core::CampaignDef& c : core::listCampaigns(dir))
            if (!campaigns.contains(c.id)) campaigns << c.id;
    m_lobby.setCampaignIds(campaigns);
}

QJsonArray GameServer::campaignsJson() const {
    QJsonArray list;
    if (!m_content) return list;
    QList<core::CampaignDef> all;
    QStringList seen;
    for (const QString& dir : contentDirs("campaigns"))
        for (const core::CampaignDef& c : core::listCampaigns(dir))
            if (!seen.contains(c.id)) {
                seen << c.id;
                all << c;
            }
    for (const core::CampaignDef& c : all) {
        core::CampaignProgress p;
        if (!m_saveDir.isEmpty()) core::loadProgress(QDir(m_saveDir).filePath("campaign_" + c.id + ".json"), &p);
        QJsonArray chapters;
        for (const QString& s : c.scenarios) {
            QString name = s;
            for (const QJsonValue& v : m_scenarios)
                if (v.toObject()["id"].toString() == s) name = v.toObject()["name"].toString();
            chapters.append(name);
        }
        list.append(QJsonObject{{"id", c.id}, {"name", c.name}, {"description", c.description},
                                {"chapters", chapters}, {"chapterIds", QJsonArray::fromStringList(c.scenarios)},
                                {"next", p.next}});
    }
    return list;
}

bool GameServer::start(quint16 port) {
    if (!m_server.listen(QHostAddress::Any, port)) {
        qCCritical(lcKotServer) << "Cannot listen on port" << port << ":" << m_server.errorString();
        return false;
    }
    qCInfo(lcKotServer) << "Listening on port" << m_server.serverPort();
    return true;
}

void GameServer::onNewConnection() {
    while (m_server.hasPendingConnections()) {
        auto* connection = new net::Connection(m_server.nextPendingConnection(), this);
        Client client;
        client.id = m_nextClientId++;
        client.connection = connection;
        m_clients.insert(client.id, client);
        const int id = client.id;
        qCInfo(lcKotServer) << "Connection" << id << "from" << connection->peerDescription();

        connect(connection, &net::Connection::messageReceived, this,
                [this, id](const Message& m) { onMessage(id, m); });
        connect(connection, &net::Connection::protocolError, this, [this, id](const QString& why) {
            qCWarning(lcKotServer) << "Client" << id << "broke the protocol:" << why;
            if (m_clients.contains(id)) sendError(m_clients[id], "protocol_error", why, 0);
        });
        connect(connection, &net::Connection::disconnected, this, [this, id]() { removeClient(id); });
    }
}

void GameServer::removeClient(int clientId) {
    const auto it = m_clients.find(clientId);
    if (it == m_clients.end()) return;
    net::Connection* connection = it->connection;
    const bool wasMember = m_lobby.hasMember(clientId);
    m_clients.erase(it);
    connection->deleteLater();
    qCInfo(lcKotServer) << "Client" << clientId << "left - clients:" << m_clients.size();
    if (m_session && m_session->active()) {
        m_session->clientLeft(clientId);  // his player is played by the passive AI from now on
        m_lobby.removeMember(clientId);
        sendGameStates();
    } else if (wasMember) {
        m_lobby.removeMember(clientId);
        broadcastLobby();
    }
    emit clientCountChanged(clientCount());
}

void GameServer::sendError(Client& client, const QString& code, const QString& text, int replyTo) {
    Message m;
    m.type = net::msg::error;
    m.body.insert("code", code);
    m.body.insert("message", text);
    if (replyTo > 0) m.body.insert("replyTo", replyTo);
    client.connection->send(m);
}

void GameServer::broadcastLobby() {
    Message m;
    m.type = net::msg::lobbyState;
    m.body = m_lobby.state().toJson();
    for (Client& c : m_clients) {
        if (c.greeted) c.connection->send(m);
    }
}

void GameServer::onMessage(int clientId, const Message& message) {
    const auto it = m_clients.find(clientId);
    if (it == m_clients.end()) return;
    Client& client = it.value();
    if (!client.greeted) {
        if (message.type != net::msg::hello) {
            sendError(client, "expected_hello", "the first message must be hello", message.seq);
            client.connection->close();
            return;
        }
        handleHello(client, message);
        return;
    }
    handleLobbyMessage(client, message);
}

void GameServer::handleHello(Client& client, const Message& message) {
    const int protocol = message.body.value("protocol").toInt(-1);
    if (protocol != net::kProtocolVersion) {
        sendError(client, "protocol_mismatch",
                  QStringLiteral("this server speaks protocol %1, you sent %2")
                      .arg(net::kProtocolVersion).arg(protocol),
                  message.seq);
        client.connection->close();
        return;
    }
    // Game messages carry content ids as numbers: the client needs the same game data.
    if (m_content && message.body.value("content").toString() != m_content->checksum()) {
        sendError(client, "content_mismatch",
                  QStringLiteral("the server has other game data (content %1, yours %2): use the same game version and data")
                      .arg(m_content->checksum(), message.body.value("content").toString()),
                  message.seq);
        client.connection->close();
        return;
    }
    QString name = message.body.value("name").toString().trimmed();
    if (name.isEmpty()) name = QStringLiteral("Player %1").arg(client.id);
    name = name.left(24);
    client.greeted = true;
    m_lobby.addMember(client.id, name);

    Message welcome;
    welcome.type = net::msg::welcome;
    welcome.seq = message.seq;
    welcome.body.insert("protocol", net::kProtocolVersion);
    welcome.body.insert("serverVersion", QString::fromLatin1(core::versionText()));
    welcome.body.insert("clientId", client.id);
    welcome.body.insert("races", QJsonArray::fromStringList(m_races));
    welcome.body.insert("aiProfiles", QJsonArray::fromStringList(m_aiProfiles));
    welcome.body.insert("scenarios", m_scenarios);
    welcome.body.insert("campaigns", campaignsJson());
    // Coming back to a running game: the seats whose secret codes his client kept are
    // his again (games played at different times); else he chooses a free one.
    QList<core::PlayerId> own;
    // Only the hash of his key is kept (with his seats, in the game and its saves).
    const QString key = message.body.value("playerKey").toString();
    client.keyHash = key.isEmpty() ? QString()
                                   : QString::fromLatin1(QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha256).toHex());
    if (m_session && m_session->active()) {
        own = m_session->seatsFor(client.keyHash, true);
        if (own.isEmpty()) welcome.body.insert("freePlayers", freePlayersJson(client));
    }
    client.connection->send(welcome);
    broadcastLobby();
    emit clientCountChanged(clientCount());
    if (!own.isEmpty()) {
        QString error;
        for (const core::PlayerId p : own)
            if (m_session->joinPlayer(client.id, p, client.keyHash, &error)) qCInfo(lcKotServer) << name << "is back as player" << p.value();
        sendGameStarted(client);
        sendGameStates();
    }
}

void GameServer::handleLobbyMessage(Client& client, const Message& message) {
    const int id = client.id;
    LobbyResult result = LobbyResult::success();
    bool startRequested = false;

    if (message.type == net::msg::ping) {
        Message pong;
        pong.type = net::msg::pong;
        pong.seq = message.seq;
        client.connection->send(pong);
        return;
    } else if (message.type == net::msg::claimSlot) {
        result = m_lobby.claimSlot(id, message.body.value("slot").toInt(-1));
    } else if (message.type == net::msg::releaseSlot) {
        result = m_lobby.releaseSlot(id);
    } else if (message.type == net::msg::setSlot) {
        QJsonObject patch = message.body;
        const int slot = patch.take("slot").toInt(-1);
        result = m_lobby.editSlot(id, slot, patch);
    } else if (message.type == net::msg::setReady) {
        result = m_lobby.setReady(id, message.body.value("ready").toBool(false));
    } else if (message.type == net::msg::setSettings) {
        result = m_lobby.setSettings(id, message.body);
    } else if (handleSaveOrJoin(client, message)) {
        return;
    } else if (message.type == net::msg::startGame) {
        if (m_session && m_session->active())
            result = LobbyResult::failure("game_running", "a game is already running");
        else
            result = m_lobby.start(id);
        startRequested = result.ok;
    } else if (m_session && m_session->active()) {
        const qint64 started = net::timing::now();
        const CommandResult command = m_session->handle(id, message);
        if (!command.ok) {
            sendError(client, command.code, command.message, message.seq);
            return;
        }
        Message reply;
        reply.type = net::msg::commandResult;
        reply.body.insert("replyTo", message.seq);
        reply.body.insert("info", command.info);
        {
            const net::timing::Scope t("send");
            client.connection->send(reply);
        }
        sendGameStates();
        {
            const net::timing::Scope t("autosave");
            autosave();
        }
        net::timing::serverDone(message.type, net::timing::now() - started);
        return;
    } else {
        result = LobbyResult::failure("unknown_message", "unknown message type '" + message.type + "'");
    }

    if (!result.ok) {
        sendError(client, result.code, result.message, message.seq);
        return;
    }
    if (startRequested) {
        startGame();
        return;
    }
    broadcastLobby();
}

void GameServer::startGame() {
    if (m_content) {
        auto session = std::make_unique<GameSession>(*m_content);
        session->setCampaignDir(m_saveDir);
        QString error;
        if (!session->start(m_lobby.state(), &error)) {
            m_lobby.cancelStart();
            const auto host = m_clients.find(m_lobby.hostId());
            if (host != m_clients.end()) sendError(host.value(), "cannot_start", error, 0);
            broadcastLobby();
            return;
        }
        m_session = std::move(session);
    }
    broadcastLobby();
    Message starting;
    starting.type = net::msg::gameStarting;
    starting.body = m_lobby.state().toJson();
    for (Client& c : m_clients) {
        if (c.greeted) c.connection->send(starting);
    }
    if (m_session) {
        for (Client& c : m_clients) {
            if (!c.greeted || !m_session->playerOfClient(c.id).isValid()) continue;
            m_session->bindSeats(c.id, c.keyHash);  // his seats are his from now on
            sendGameStarted(c);
        }
        m_lastAutosaveDay = m_session->state().day();  // day 1 is the start itself
        m_gameStamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
        saveCampaignStart();
    }
    emit gameStartRequested(m_lobby.state());
}

void GameServer::autosave() {
    if (!m_session || !m_session->active() || m_session->battleRunning() || m_saveDir.isEmpty()) return;
    const int day = m_session->state().day();
    const int keep = m_lobby.state().settings.autosaves;
    if (day <= m_lastAutosaveDay || keep <= 0) return;
    m_lastAutosaveDay = day;
    QString error;
    // The game's start time in the name: autosaves of different games never mix.
    if (m_gameStamp.isEmpty()) m_gameStamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    const QString name = QStringLiteral("autosave_%1_day%2").arg(m_gameStamp).arg(day, 4, 10, QLatin1Char('0'));
    const QString path = savePath(name, &error);
    if (path.isEmpty() || !writeSave(path, &error)) {
        qCWarning(lcKotServer) << "Autosave failed:" << error;
        return;
    }
    // Keep the `keep` most recently written autosaves (of any game); older ones go.
    QFileInfoList all = QDir(m_saveDir).entryInfoList({QStringLiteral("autosave_*.json")}, QDir::Files);
    std::sort(all.begin(), all.end(), [](const QFileInfo& a, const QFileInfo& b) {
        if (a.lastModified() != b.lastModified()) return a.lastModified() < b.lastModified();
        return a.fileName() < b.fileName();
    });
    while (all.size() > keep) QFile::remove(all.takeFirst().filePath());
}

void GameServer::saveCampaignStart() {
    if (!m_session || m_session->campaignId().isEmpty() || m_saveDir.isEmpty()) return;
    QString error;
    const QString name = QStringLiteral("campaign_%1_chapter%2_start").arg(m_session->campaignId()).arg(m_session->campaignChapter() + 1);
    const QString path = savePath(name.left(40), &error);
    if (path.isEmpty() || !writeSave(path, &error)) qCWarning(lcKotServer) << "Campaign save failed:" << error;
}

bool GameServer::loadSaveFile(const QString& path, QString* error) {
    core::GameState state;
    QStringList errors;
    if (!m_content || !core::GameState::loadFromFile(path, *m_content, &state, &errors)) {
        if (error) *error = QStringLiteral("this saved game cannot be loaded: ") + errors.join("; ");
        return false;
    }
    m_session = std::make_unique<GameSession>(*m_content);
    m_session->startFromState(std::move(state));
    m_lastAutosaveDay = m_session->state().day();
    m_gameStamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    qCInfo(lcKotServer) << "Loaded the game" << path;
    // Who has a code for a seat gets it back; the others choose a free one.
    QList<int> back;
    for (Client& c : m_clients) {
        if (!c.greeted) continue;
        QString why;
        for (const core::PlayerId p : m_session->seatsFor(c.keyHash, true))
            if (m_session->joinPlayer(c.id, p, c.keyHash, &why)) back << c.id;
    }
    for (Client& c : m_clients) {
        if (!c.greeted) continue;
        if (back.contains(c.id)) {
            sendGameStarted(c);
            continue;
        }
        Message loaded;
        loaded.type = net::msg::gameLoaded;
        loaded.body.insert("freePlayers", freePlayersJson(c));
        c.connection->send(loaded);
    }
    if (!back.isEmpty()) sendGameStates();
    return true;
}

bool GameServer::resumeNewestSave(QString* error) {
    if (m_saveDir.isEmpty()) {
        if (error) *error = QStringLiteral("no save folder");
        return false;
    }
    for (const QFileInfo& f : QDir(m_saveDir).entryInfoList({QStringLiteral("*.json")}, QDir::Files, QDir::Time)) {
        if (f.completeBaseName().startsWith("campaign_") && !f.completeBaseName().contains("_chapter")) continue;  // progress
        QString why;
        if (loadSaveFile(f.filePath(), &why)) return true;
        qCWarning(lcKotServer) << "Cannot resume" << f.fileName() << why;
    }
    if (error) *error = QStringLiteral("no saved game here loads");
    return false;
}

bool GameServer::writeSave(const QString& path, QString* error) {
    const QByteArray bytes = m_session->state().saveBytes(*m_content);
    if (m_saveLimitBytes > 0) {
        if (bytes.size() > m_saveLimitBytes) {
            if (error) *error = QStringLiteral("the save (%1 KB) is bigger than the server's whole save space (%2 KB)")
                                    .arg(bytes.size() / 1024).arg(m_saveLimitBytes / 1024);
            return false;
        }
        // Everything in the save folder counts (generated maps too); only saved games
        // are removed, oldest first.
        qint64 used = 0;
        QFileInfoList games;
        for (QDirIterator it(m_saveDir, QDir::Files, QDirIterator::Subdirectories); it.hasNext();) {
            const QFileInfo f(it.next());
            if (f.absoluteFilePath() == QFileInfo(path).absoluteFilePath()) continue;  // replaced by the new one
            used += f.size();
            const bool progress = f.completeBaseName().startsWith("campaign_") && !f.completeBaseName().contains("_chapter");
            if (f.absolutePath() == QDir(m_saveDir).absolutePath() && f.suffix() == QLatin1String("json") && !progress) games << f;
        }
        std::sort(games.begin(), games.end(), [](const QFileInfo& a, const QFileInfo& b) {
            if (a.lastModified() != b.lastModified()) return a.lastModified() < b.lastModified();
            return a.fileName() < b.fileName();
        });
        while (used + bytes.size() > m_saveLimitBytes && !games.isEmpty()) {
            const QFileInfo oldest = games.takeFirst();
            if (QFile::remove(oldest.filePath())) {
                used -= oldest.size();
                qCInfo(lcKotServer) << "Save space full: removed the oldest save" << oldest.fileName();
            }
        }
        if (used + bytes.size() > m_saveLimitBytes) {
            if (error) *error = QStringLiteral("the server's save space is full (%1 of %2 KB)").arg(used / 1024).arg(m_saveLimitBytes / 1024);
            return false;
        }
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        if (error) *error = file.errorString();
        return false;
    }
    return true;
}

void GameServer::sendGameStates() {
    if (!m_session) return;
    for (Client& c : m_clients) {
        const core::PlayerId p = m_session->playerOfClient(c.id);
        if (!c.greeted || !p.isValid()) continue;
        Message state;
        state.type = net::msg::gameState;
        {
            const net::timing::Scope t("view");
            state.body = m_session->viewFor(p);
        }
        const net::timing::Scope t("send");
        c.connection->send(state);
    }
}

void GameServer::sendGameStarted(Client& c) {
    const core::PlayerId p = m_session->playerOfClient(c.id);
    Message started;
    started.type = net::msg::gameStarted;
    started.body.insert("you", m_session->youInfo(p));
    started.body.insert("view", m_session->viewFor(p));
    c.connection->send(started);
}

void GameServer::reply(Client& client, const Message& request, const QJsonObject& info) {
    Message m;
    m.type = net::msg::commandResult;
    m.body.insert("replyTo", request.seq);
    m.body.insert("info", info);
    client.connection->send(m);
}

QJsonArray GameServer::freePlayersJson(const Client& client) const {
    QJsonArray list;
    if (!m_session) return list;
    for (const core::PlayerId p : m_session->seatsFor(client.keyHash, false)) {
        const core::Player* player = m_session->state().player(p);
        list.append(QJsonObject{{"id", static_cast<int>(p.value())}, {"name", player ? player->name : QString()}});
    }
    return list;
}

QString GameServer::savePath(const QString& name, QString* error) const {
    static const QRegularExpression valid(QStringLiteral("^[A-Za-z0-9_-]{1,40}$"));
    if (!valid.match(name).hasMatch()) {
        *error = QStringLiteral("a save name has 1-40 letters, digits, '_' or '-'");
        return {};
    }
    if (m_saveDir.isEmpty() || !QDir().mkpath(m_saveDir)) {
        *error = QStringLiteral("the server has no folder for saved games");
        return {};
    }
    return QDir(m_saveDir).filePath(name + QStringLiteral(".json"));
}

void GameServer::generateContent(Client& client, const Message& message) {
    const QJsonObject b = message.body;
    QString error;
    const QString kind = b.value("kind").toString("scenario");
    const QString id = b.value("id").toString();
    if (!m_content || m_saveDir.isEmpty()) {
        sendError(client, "failed", "this server cannot make maps", message.seq);
        return;
    }
    if (!core::validGeneratedId(id)) {
        sendError(client, "failed", "a name has 1-40 letters, digits, '_' or '-'", message.seq);
        return;
    }
    const QString scenarioDir = QDir(m_saveDir).filePath("scenarios"), campaignDir = QDir(m_saveDir).filePath("campaigns");
    if (kind == "campaign") {
        core::CampaignGenParams p;
        p.id = id;
        p.name = b.value("name").toString(id);
        p.chapters = b.value("chapters").toInt(3);
        p.playerRace = b.value("race").toString("random");
        p.startSize = b.value("size").toInt(48);
        p.seed = static_cast<quint64>(b.value("seed").toDouble(1));
        p.carryHeroes = b.value("carryHeroes").toInt(1);
        p.carryArmy = b.value("carryArmy").toBool(true);
        p.islands = b.value("islands").toBool(false);
        if (!core::generateCampaign(*m_content, p, scenarioDir, campaignDir, &error)) {
            sendError(client, "failed", error, message.seq);
            return;
        }
    } else {
        core::ScenarioGenParams p;
        p.id = id;
        p.width = b.value("width").toInt(64);
        p.height = b.value("height").toInt(p.width);
        p.seed = static_cast<quint64>(b.value("seed").toDouble(1));
        for (const QJsonValue& r : b.value("races").toArray()) p.races << r.toString();
        p.islands = b.value("islands").toBool(false);
        p.seaLevel = b.value("seaLevel").toDouble(p.seaLevel);
        p.mountains = b.value("mountains").toDouble(p.mountains);
        p.rivers = b.value("rivers").toInt(-1);
        p.roads = b.value("roads").toBool(true);
        p.monsterPercent = b.value("monsterPercent").toInt(100);
        p.victory = b.value("victory").toString("conquest");
        p.victoryAmount = b.value("victoryAmount").toInt(0);
        core::GameState gs;
        if (!core::generateScenario(*m_content, p, &gs, &error) || !QDir().mkpath(scenarioDir) ||
            !core::saveScenario(QDir(scenarioDir).filePath(id + ".json"), id, b.value("name").toString(id),
                                b.value("description").toString(QStringLiteral("A random map, %1x%2, %3 players.")
                                                                    .arg(p.width).arg(p.height).arg(p.races.size())),
                                gs, *m_content, &error)) {
            sendError(client, "failed", error.isEmpty() ? QStringLiteral("cannot write the scenario") : error, message.seq);
            return;
        }
    }
    refreshContentLists();
    reply(client, message, {{"id", id}, {"kind", kind}});
    Message lists;
    lists.type = net::msg::contentLists;
    lists.body.insert("scenarios", m_scenarios);
    lists.body.insert("campaigns", campaignsJson());
    for (Client& c : m_clients)
        if (c.greeted) c.connection->send(lists);
}

bool GameServer::handleSaveOrJoin(Client& client, const Message& message) {
    const bool running = m_session && m_session->active();
    QString error;
    if (message.type == net::msg::saveGame) {
        if (!running || !m_session->playerOfClient(client.id).isValid()) {
            sendError(client, "no_game", "you are not playing a game", message.seq);
            return true;
        }
        if (m_session->battleRunning()) {
            sendError(client, "battle_in_progress", "finish the battle before saving", message.seq);
            return true;
        }
        const QString name = message.body.value("name").toString();
        const QString path = savePath(name, &error);
        // Any player may save (with players coming at different times the host may be
        // away; the save space limit keeps the disk safe), but only the host may replace
        // an existing save.
        if (!path.isEmpty() && QFile::exists(path) && m_lobby.hostId() != client.id) {
            sendError(client, "save_exists", QStringLiteral("there is already a save called '%1'; only the host can replace it").arg(name), message.seq);
            return true;
        }
        if (path.isEmpty() || !writeSave(path, &error)) {
            sendError(client, "failed", error, message.seq);
            return true;
        }
        qCInfo(lcKotServer) << "Saved the game to" << path;
        reply(client, message, {{"name", name}});
        return true;
    }
    if (message.type == net::msg::listSaves) {
        // Newest first, with what the load list shows and whether each one loads.
        QStringList names;
        QJsonArray games;
        if (!m_saveDir.isEmpty()) {
            for (const QFileInfo& f : QDir(m_saveDir).entryInfoList({"*.json"}, QDir::Files, QDir::Time)) {
                // campaign_<id>.json holds campaign progress, not a game.
                if (f.completeBaseName().startsWith("campaign_") && !f.completeBaseName().contains("_chapter")) continue;
                names << f.completeBaseName();
                if (!m_content) continue;
                const core::SaveInfo s = core::inspectSave(f.filePath(), *m_content);
                static const char* status[] = {"ok", "incompatible"};
                games.append(QJsonObject{{"name", s.name}, {"gameVersion", s.gameVersion}, {"savedAt", s.savedAt},
                                         {"formatVersion", s.formatVersion}, {"day", s.day},
                                         {"players", QJsonArray::fromStringList(s.players)},
                                         {"mapWidth", s.mapWidth}, {"mapHeight", s.mapHeight},
                                         {"status", status[int(s.status)]},
                                         {"problems", QJsonArray::fromStringList(s.problems)}});
            }
        }
        reply(client, message, {{"saves", QJsonArray::fromStringList(names)}, {"games", games}});
        return true;
    }
    if (message.type == net::msg::generateMap) {
        if (running) {
            sendError(client, "game_running", "a game is already running", message.seq);
            return true;
        }
        if (m_lobby.hostId() != client.id) {
            sendError(client, "not_host", "only the host can make maps", message.seq);
            return true;
        }
        generateContent(client, message);
        return true;
    }
    if (message.type == net::msg::loadGame) {
        if (running) {
            sendError(client, "game_running", "a game is already running", message.seq);
            return true;
        }
        if (m_lobby.hostId() != client.id) {
            sendError(client, "not_host", "only the host can load a game", message.seq);
            return true;
        }
        if (!m_content) {
            sendError(client, "failed", "this server cannot run games", message.seq);
            return true;
        }
        const QString path = savePath(message.body.value("name").toString(), &error);
        if (path.isEmpty() || !loadSaveFile(path, &error)) {
            sendError(client, "incompatible_save", error, message.seq);
            return true;
        }
        reply(client, message, {{"name", message.body.value("name").toString()}});
        return true;
    }
    if (message.type == net::msg::joinGame) {
        if (!running) {
            sendError(client, "no_game", "no game is running", message.seq);
            return true;
        }
        const core::PlayerId player(static_cast<quint32>(message.body.value("player").toInt()));
        if (!m_session->joinPlayer(client.id, player, client.keyHash, &error)) {
            sendError(client, "failed", error, message.seq);
            return true;
        }
        reply(client, message, {{"player", static_cast<int>(player.value())}});
        sendGameStarted(client);
        sendGameStates();
        return true;
    }
    return false;
}

}  // namespace kot::server
