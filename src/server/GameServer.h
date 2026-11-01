#pragma once

#include <QHash>
#include <QObject>
#include <QStringList>
#include <QTcpServer>

#include "net/Connection.h"
#include "server/Lobby.h"

namespace kot::server {

// Accepts clients, performs the handshake and runs the lobby.
// Game play (commands, turns, filtered state) is added in the next slice.
class GameServer : public QObject {
    Q_OBJECT
public:
    GameServer(QStringList races, QStringList aiProfiles, QObject* parent = nullptr);

    // Port 0 picks any free port (used by the tests); port() tells which.
    bool start(quint16 port);
    quint16 port() const { return m_server.serverPort(); }
    int clientCount() const { return static_cast<int>(m_clients.size()); }
    const Lobby& lobby() const { return m_lobby; }

signals:
    void clientCountChanged(int count);
    // The host started the game and the lobby is valid.
    void gameStartRequested(const kot::net::LobbyState& lobby);

private slots:
    void onNewConnection();

private:
    struct Client {
        int id = 0;
        net::Connection* connection = nullptr;
        bool greeted = false;
    };

    void onMessage(int clientId, const net::Message& message);
    void handleHello(Client& client, const net::Message& message);
    void handleLobbyMessage(Client& client, const net::Message& message);
    void removeClient(int clientId);
    void sendError(Client& client, const QString& code, const QString& text, int replyTo);
    void broadcastLobby();

    QTcpServer m_server;
    QStringList m_races;
    QStringList m_aiProfiles;
    Lobby m_lobby;
    QHash<int, Client> m_clients;
    int m_nextClientId = 1;
};

}  // namespace kot::server
