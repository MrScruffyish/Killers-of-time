#include "server/GameServer.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QRegularExpression>

#include "core/Version.h"
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
    m_scenarios = QJsonArray();
    QStringList ids;
    if (content && !content->dataDir().isEmpty()) {
        for (const core::ScenarioInfo& s : core::listScenarios(QDir(content->dataDir()).filePath("scenarios"), *content)) {
            m_scenarios.append(QJsonObject{{"id", s.id}, {"name", s.name}, {"description", s.description},
                                           {"width", s.width}, {"height", s.height},
                                           {"races", QJsonArray::fromStringList(s.races)}});
            ids << s.id;
        }
    }
    m_lobby.setScenarioIds(ids);
    QStringList campaigns;
    if (content && !content->dataDir().isEmpty())
        for (const core::CampaignDef& c : core::listCampaigns(QDir(content->dataDir()).filePath("campaigns"))) campaigns << c.id;
    m_lobby.setCampaignIds(campaigns);
}

QJsonArray GameServer::campaignsJson() const {
    QJsonArray list;
    if (!m_content || m_content->dataDir().isEmpty()) return list;
    for (const core::CampaignDef& c : core::listCampaigns(QDir(m_content->dataDir()).filePath("campaigns"))) {
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
    if (m_session && m_session->active()) welcome.body.insert("freePlayers", freePlayersJson());
    client.connection->send(welcome);
    broadcastLobby();
    emit clientCountChanged(clientCount());
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
        const CommandResult command = m_session->handle(id, message);
        if (!command.ok) {
            sendError(client, command.code, command.message, message.seq);
            return;
        }
        Message reply;
        reply.type = net::msg::commandResult;
        reply.body.insert("replyTo", message.seq);
        reply.body.insert("info", command.info);
        client.connection->send(reply);
        sendGameStates();
        autosave();
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
            if (c.greeted && m_session->playerOfClient(c.id).isValid()) sendGameStarted(c);
        }
        m_lastAutosaveDay = m_session->state().day();  // day 1 is the start itself
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
    const QString name = QStringLiteral("autosave_day%1").arg(day, 4, 10, QLatin1Char('0'));
    const QString path = savePath(name, &error);
    if (path.isEmpty() || !m_session->state().saveToFile(path, &error)) {
        qCWarning(lcKotServer) << "Autosave failed:" << error;
        return;
    }
    // Keep the newest `keep` autosaves (the names sort by day).
    QStringList all = QDir(m_saveDir).entryList({QStringLiteral("autosave_day*.json")}, QDir::Files, QDir::Name);
    while (all.size() > keep) QFile::remove(QDir(m_saveDir).filePath(all.takeFirst()));
}

void GameServer::saveCampaignStart() {
    if (!m_session || m_session->campaignId().isEmpty() || m_saveDir.isEmpty()) return;
    QString error;
    const QString name = QStringLiteral("campaign_%1_chapter%2_start").arg(m_session->campaignId()).arg(m_session->campaignChapter() + 1);
    const QString path = savePath(name.left(40), &error);
    if (path.isEmpty() || !m_session->state().saveToFile(path, &error)) qCWarning(lcKotServer) << "Campaign save failed:" << error;
}

void GameServer::sendGameStates() {
    if (!m_session) return;
    for (Client& c : m_clients) {
        const core::PlayerId p = m_session->playerOfClient(c.id);
        if (!c.greeted || !p.isValid()) continue;
        Message state;
        state.type = net::msg::gameState;
        state.body = m_session->viewFor(p);
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

QJsonArray GameServer::freePlayersJson() const {
    QJsonArray list;
    if (!m_session) return list;
    for (const core::PlayerId p : m_session->freePlayers()) {
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
        if (path.isEmpty() || !m_session->state().saveToFile(path, &error)) {
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
                static const char* status[] = {"ok", "converted", "incompatible"};
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
        core::GameState state;
        QStringList errors, notes;
        if (path.isEmpty() || !core::GameState::loadFromFile(path, *m_content, &state, &errors, &notes)) {
            sendError(client, "incompatible_save",
                      path.isEmpty() ? error : QStringLiteral("this saved game cannot be loaded: ") + errors.join("; "), message.seq);
            return true;
        }
        if (!notes.isEmpty()) qCInfo(lcKotServer) << "Converted" << path << notes;
        m_session = std::make_unique<GameSession>(*m_content);
        m_session->startFromState(std::move(state));
        m_lastAutosaveDay = m_session->state().day();
        qCInfo(lcKotServer) << "Loaded the game" << path;
        reply(client, message, {{"name", message.body.value("name").toString()}, {"converted", QJsonArray::fromStringList(notes)}});
        Message loaded;
        loaded.type = net::msg::gameLoaded;
        loaded.body.insert("freePlayers", freePlayersJson());
        for (Client& c : m_clients) {
            if (c.greeted) c.connection->send(loaded);
        }
        return true;
    }
    if (message.type == net::msg::joinGame) {
        if (!running) {
            sendError(client, "no_game", "no game is running", message.seq);
            return true;
        }
        const core::PlayerId player(static_cast<quint32>(message.body.value("player").toInt()));
        if (!m_session->joinPlayer(client.id, player, &error)) {
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
