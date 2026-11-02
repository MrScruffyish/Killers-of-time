#pragma once

#include <QHash>
#include <QJsonArray>
#include <QObject>
#include <QStringList>
#include <QTcpServer>

#include "net/Connection.h"
#include <memory>

#include "server/GameSession.h"
#include "server/Lobby.h"

namespace kot::core { class ContentRegistry; }

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
    // With game content set, starting the game from the lobby creates a real game.
    // Without it (some tests) the server only runs the lobby.
    // Also finds the scenarios in the data folder.
    void setContent(const core::ContentRegistry* content);
    const GameSession* session() const { return m_session.get(); }
    GameSession* session() { return m_session.get(); }  // tests set up situations
    // Folder for saved games (created when needed).
    void setSaveDir(const QString& dir) { m_saveDir = dir; }
    const QString& saveDir() const { return m_saveDir; }

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
    void startGame();
    void sendGameStates();
    // Saved games, joining a running game. Return true if the message was one of these.
    bool handleSaveOrJoin(Client& client, const net::Message& message);
    void reply(Client& client, const net::Message& request, const QJsonObject& info);
    QJsonArray freePlayersJson() const;
    QString savePath(const QString& name, QString* error) const;
    void sendGameStarted(Client& client);

    QTcpServer m_server;
    QStringList m_races;
    QStringList m_aiProfiles;
    Lobby m_lobby;
    QHash<int, Client> m_clients;
    const core::ContentRegistry* m_content = nullptr;
    std::unique_ptr<GameSession> m_session;
    int m_nextClientId = 1;
    QString m_saveDir;
    QJsonArray m_scenarios;  // for the welcome message
};

}  // namespace kot::server
