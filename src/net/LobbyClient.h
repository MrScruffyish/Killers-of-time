#pragma once

#include <QJsonArray>
#include <QObject>
#include <QStringList>

#include "net/Connection.h"
#include "net/LobbyTypes.h"
#include "net/Protocol.h"

namespace kot::net {

// Client side of the lobby: connects, says hello, keeps the latest lobby state and
// offers one function per request. Results arrive as signals.
class LobbyClient : public QObject {
    Q_OBJECT
public:
    explicit LobbyClient(QObject* parent = nullptr);

    void connectToServer(const QString& host, quint16 port, const QString& playerName);
    void disconnectFromServer();
    bool isConnected() const { return m_welcomed; }

    int clientId() const { return m_clientId; }
    bool isHost() const;
    const LobbyState& lobby() const { return m_lobby; }
    const QStringList& races() const { return m_races; }
    const QStringList& aiProfiles() const { return m_aiProfiles; }
    // Scenarios the server offers: [{id, name, description, width, height, races}].
    const QJsonArray& scenarios() const { return m_scenarios; }
    // Campaigns: [{id, name, description, chapters (names), chapterIds, next}].
    const QJsonArray& campaigns() const { return m_campaigns; }

    // Each returns the sequence number of the request (an error reply names it).
    int claimSlot(int slot);
    int releaseSlot();
    int setSlot(int slot, const QJsonObject& patch);  // e.g. {"race": "humans", "team": 2}
    int setReady(bool ready);
    int setSettings(const QJsonObject& patch);
    int startGame();
    int ping();
    // Any game command, e.g. sendCommand(msg::moveHero, {{"hero", 5}, {"x", 3}, {"y", 4}}).
    int sendCommand(const QString& type, const QJsonObject& body = {});
    // Saved games and taking over a player of a running game (results: commandResult / errorReceived).
    int saveGame(const QString& name) { return send(msg::saveGame, {{"name", name}}); }
    int listSaves() { return send(msg::listSaves); }
    int loadGame(const QString& name) { return send(msg::loadGame, {{"name", name}}); }
    int joinGame(int playerId) { return send(msg::joinGame, {{"player", playerId}}); }
    // Players of a running game nobody plays now: [{id, name}] (from welcome or game_loaded).
    const QJsonArray& freePlayers() const { return m_freePlayers; }
    // The latest view the server sent (empty before the game starts) and who we are.
    const QJsonObject& view() const { return m_view; }
    const QJsonObject& you() const { return m_you; }

signals:
    void connected();                     // handshake done
    void lobbyChanged();
    void errorReceived(const QString& code, const QString& message, int replyTo);
    void gameStarting();
    void gameStarted();                                  // first view received
    void gameStateChanged();                             // a new view received
    void commandResult(int replyTo, const QJsonObject& info);
    // A game is running (we joined late) or was loaded: pick one of freePlayers() with joinGame().
    void playerChoiceOffered();
    void connectionFailed(const QString& reason);  // could not connect or lost the server
    void disconnected();

private:
    int send(const QString& type, const QJsonObject& body = {});
    void onMessage(const Message& message);

    Connection* m_connection = nullptr;
    QString m_name;
    bool m_welcomed = false;
    int m_clientId = 0;
    int m_nextSeq = 1;
    LobbyState m_lobby;
    QStringList m_races;
    QStringList m_aiProfiles;
    QJsonObject m_view;
    QJsonObject m_you;
    QJsonArray m_freePlayers;
    QJsonArray m_scenarios;
    QJsonArray m_campaigns;
};

}  // namespace kot::net
