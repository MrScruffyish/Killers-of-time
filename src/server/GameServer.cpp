#include "server/GameServer.h"

#include <QJsonArray>

#include "core/Version.h"
#include "core/util/Log.h"
#include "net/Protocol.h"

namespace kot::server {

using net::Message;

GameServer::GameServer(QStringList races, QStringList aiProfiles, QObject* parent)
    : QObject(parent), m_races(races), m_aiProfiles(aiProfiles), m_lobby(races, aiProfiles) {
    connect(&m_server, &QTcpServer::newConnection, this, &GameServer::onNewConnection);
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
    welcome.body.insert("serverVersion", QString::fromLatin1(core::kGameVersion));
    welcome.body.insert("clientId", client.id);
    welcome.body.insert("races", QJsonArray::fromStringList(m_races));
    welcome.body.insert("aiProfiles", QJsonArray::fromStringList(m_aiProfiles));
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
    } else if (message.type == net::msg::startGame) {
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
            const core::PlayerId p = m_session->playerOfClient(c.id);
            if (!c.greeted || !p.isValid()) continue;
            Message started;
            started.type = net::msg::gameStarted;
            started.body.insert("you", m_session->youInfo(p));
            started.body.insert("view", m_session->viewFor(p));
            c.connection->send(started);
        }
    }
    emit gameStartRequested(m_lobby.state());
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

}  // namespace kot::server
