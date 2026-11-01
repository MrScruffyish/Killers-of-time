#pragma once

#include <QObject>
#include <QStringList>

#include "net/Connection.h"
#include "net/LobbyTypes.h"

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

    // Each returns the sequence number of the request (an error reply names it).
    int claimSlot(int slot);
    int releaseSlot();
    int setSlot(int slot, const QJsonObject& patch);  // e.g. {"race": "humans", "team": 2}
    int setReady(bool ready);
    int setSettings(const QJsonObject& patch);
    int startGame();
    int ping();

signals:
    void connected();                     // handshake done
    void lobbyChanged();
    void errorReceived(const QString& code, const QString& message, int replyTo);
    void gameStarting();
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
};

}  // namespace kot::net
