#include "net/LobbyClient.h"

#include <QJsonArray>
#include <QRandomGenerator>

#include "net/Protocol.h"

namespace kot::net {

LobbyClient::LobbyClient(QObject* parent) : QObject(parent) {
    qRegisterMetaType<kot::net::Message>();
}

bool LobbyClient::isHost() const {
    for (const LobbyMember& m : m_lobby.members) {
        if (m.clientId == m_clientId) return m.isHost;
    }
    return false;
}

QString LobbyClient::newPlayerKey() {
    QByteArray bytes(16, '\0');
    QRandomGenerator::system()->fillRange(reinterpret_cast<quint32*>(bytes.data()), 4);  // not guessable
    return QString::fromLatin1(bytes.toHex());
}

void LobbyClient::connectToServer(const QString& host, quint16 port, const QString& playerName) {
    disconnectFromServer();
    m_name = playerName;
    m_welcomed = false;
    m_clientId = 0;
    m_lobby = LobbyState();
    m_connection = new Connection(new QTcpSocket(), this);
    connect(m_connection, &Connection::connected, this, [this]() {
        Message hello;
        hello.type = msg::hello;
        hello.seq = m_nextSeq++;
        hello.body.insert("protocol", kProtocolVersion);
        hello.body.insert("name", m_name);
        hello.body.insert("content", m_contentChecksum);
        hello.body.insert("playerKey", m_playerKey);
        m_connection->send(hello);
    });
    connect(m_connection, &Connection::messageReceived, this, &LobbyClient::onMessage);
    connect(m_connection, &Connection::socketError, this, [this](const QString& why) {
        if (!m_welcomed) emit connectionFailed(why);
    });
    connect(m_connection, &Connection::protocolError, this,
            [this](const QString& why) { emit connectionFailed(why); });
    connect(m_connection, &Connection::disconnected, this, [this]() {
        const bool was = m_welcomed;
        m_welcomed = false;
        if (was) emit connectionFailed(QStringLiteral("the connection to the server was lost"));
        emit disconnected();
    });
    m_connection->connectToHost(host, port);
}

void LobbyClient::disconnectFromServer() {
    if (!m_connection) return;
    Connection* c = m_connection;
    m_connection = nullptr;
    m_welcomed = false;
    c->disconnect(this);  // no more signals from the old connection
    c->close();
    c->deleteLater();
}

int LobbyClient::send(const QString& type, const QJsonObject& body) {
    if (!m_connection || !m_welcomed) return 0;
    Message m;
    m.type = type;
    m.body = body;
    m.seq = m_nextSeq++;
    return m_connection->send(m) ? m.seq : 0;
}

int LobbyClient::claimSlot(int slot) { return send(msg::claimSlot, {{"slot", slot}}); }
int LobbyClient::releaseSlot() { return send(msg::releaseSlot); }
int LobbyClient::setSlot(int slot, const QJsonObject& patch) {
    QJsonObject body = patch;
    body.insert("slot", slot);
    return send(msg::setSlot, body);
}
int LobbyClient::setReady(bool ready) { return send(msg::setReady, {{"ready", ready}}); }
int LobbyClient::setSettings(const QJsonObject& patch) { return send(msg::setSettings, patch); }
int LobbyClient::startGame() { return send(msg::startGame); }
int LobbyClient::ping() { return send(msg::ping); }
int LobbyClient::sendCommand(const QString& type, const QJsonObject& body) { return send(type, body); }

void LobbyClient::onMessage(const Message& message) {
    if (message.type == msg::contentLists) {  // a map was made in the lobby
        m_scenarios = message.body.value("scenarios").toArray();
        m_campaigns = message.body.value("campaigns").toArray();
        emit lobbyChanged();
        return;
    }
    if (message.type == msg::welcome) {
        m_clientId = message.body.value("clientId").toInt();
        m_races.clear();
        for (const QJsonValue& v : message.body.value("races").toArray()) m_races << v.toString();
        m_aiProfiles.clear();
        for (const QJsonValue& v : message.body.value("aiProfiles").toArray()) m_aiProfiles << v.toString();
        m_welcomed = true;
        m_freePlayers = message.body.value("freePlayers").toArray();
        m_scenarios = message.body.value("scenarios").toArray();
        m_campaigns = message.body.value("campaigns").toArray();
        m_serverVersion = message.body.value("serverVersion").toString();
        emit connected();
        if (message.body.contains("freePlayers")) emit playerChoiceOffered();
    } else if (message.type == msg::lobbyState) {
        LobbyState state;
        QStringList errors;
        if (LobbyState::fromJson(message.body, &state, &errors)) {
            m_lobby = state;
            emit lobbyChanged();
        } else {
            emit connectionFailed(QStringLiteral("the server sent an invalid lobby: ") + errors.join("; "));
        }
    } else if (message.type == msg::error) {
        const QString code = message.body.value("code").toString();
        emit errorReceived(code, message.body.value("message").toString(),
                           message.body.value("replyTo").toInt(0));
        if (!m_welcomed) emit connectionFailed(message.body.value("message").toString());
    } else if (message.type == msg::gameStarted) {
        m_you = message.body.value("you").toObject();
        m_view = message.body.value("view").toObject();
        emit gameStarted();
        emit gameStateChanged();
    } else if (message.type == msg::gameState) {
        m_view = message.body;
        emit gameStateChanged();
    } else if (message.type == msg::commandResult) {
        emit commandResult(message.body.value("replyTo").toInt(0), message.body.value("info").toObject());
    } else if (message.type == msg::gameLoaded) {
        m_freePlayers = message.body.value("freePlayers").toArray();
        emit playerChoiceOffered();
    } else if (message.type == msg::gameStarting) {
        LobbyState state;
        if (LobbyState::fromJson(message.body, &state, nullptr)) m_lobby = state;
        emit gameStarting();
    }
}

}  // namespace kot::net
