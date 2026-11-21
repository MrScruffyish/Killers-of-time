#pragma once

#include <QHash>
#include <QJsonArray>
#include <QObject>
#include <QSet>
#include <QSslConfiguration>
#include <QStringList>
#include <QTcpServer>

#include "net/Connection.h"
#include <memory>

#include "server/GameSession.h"
#include "server/Lobby.h"

class QSslServer;

namespace kot::core { class ContentRegistry; }

namespace kot::server {

// Accepts clients, performs the handshake and runs the lobby.
// Game play (commands, turns, filtered state) is added in the next slice.
class GameServer : public QObject {
    Q_OBJECT
public:
    GameServer(QStringList races, QStringList aiProfiles, QObject* parent = nullptr);
    ~GameServer() override;

    // Port 0 picks any free port (used by the tests); port() tells which.
    bool start(quint16 port);
    quint16 port() const;
    int clientCount() const { return static_cast<int>(m_clients.size()); }
    const Lobby& lobby() const { return m_lobby; }
    // Encrypts all connections with TLS, using this certificate and key (PEM files).
    // Call before start(); false (and why) when they cannot be read.
    bool setTls(const QString& certPath, const QString& keyPath, QString* error = nullptr);
    bool tlsEnabled() const { return m_tls; }
    // Only clients whose game token is one of `tokens` may connect (their SHA-256 is
    // kept, like the seats'). Empty: anyone may connect. Call before start(); a token
    // may be typed with spaces and dashes, like on the start screen. False (and why)
    // when one of them is not a game token.
    bool setAllowedGameTokens(const QStringList& tokens, QString* error = nullptr);
    bool hasAllowedGameTokens() const { return !m_allowedTokenHashes.isEmpty(); }
    // With game content set, starting the game from the lobby creates a real game.
    // Without it (some tests) the server only runs the lobby.
    // Also finds the scenarios in the data folder.
    void setContent(const core::ContentRegistry* content);
    const GameSession* session() const { return m_session.get(); }
    GameSession* session() { return m_session.get(); }  // tests set up situations
    // Folder for saved games (created when needed).
    void setSaveDir(const QString& dir) {
        m_saveDir = dir;
        refreshContentLists();
    }
    const QString& saveDir() const { return m_saveDir; }
    // Most space the save folder may use (all its files); 0 = no limit. A save that
    // would go over it first removes the oldest saved games (writeSave).
    void setSaveLimitBytes(qint64 bytes) { m_saveLimitBytes = bytes; }
    // Loads the newest saved game (not campaign progress) and waits for the players
    // (after a server restart: --resume). False when there is none that loads.
    bool resumeNewestSave(QString* error);

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
        QString tokenHash;  // SHA-256 of the game token from his hello: his seats are bound to it
    };

    void onMessage(int clientId, const net::Message& message);
    void handleHello(Client& client, const net::Message& message);
    void handleLobbyMessage(Client& client, const net::Message& message);
    void removeClient(int clientId);
    void sendError(Client& client, const QString& code, const QString& text, int replyTo);
    void broadcastLobby();
    void sendContentLists();  // refreshed scenario/campaign lists (with campaign progress)
    void startGame();
    // The host left the running game: drop it and put everyone back in a fresh setup.
    void abandonGame(int replyTo);
    void sendGameStates();
    // Saved games, joining a running game. Return true if the message was one of these.
    bool handleSaveOrJoin(Client& client, const net::Message& message);
    void reply(Client& client, const net::Message& request, const QJsonObject& info);
    // The seats this client may choose (nobody has played them yet).
    QJsonArray freePlayersJson(const Client& client) const;
    QString savePath(const QString& name, QString* error) const;
    void sendGameStarted(Client& client);

    QTcpServer m_server;
    std::unique_ptr<QSslServer> m_sslServer;  // when TLS is on, this one listens
    QSslConfiguration m_tlsConfig;
    bool m_tls = false;
    QSet<QString> m_allowedTokenHashes;  // empty: anyone may connect
    QStringList m_races;
    QStringList m_aiProfiles;
    Lobby m_lobby;
    QHash<int, Client> m_clients;
    const core::ContentRegistry* m_content = nullptr;
    std::unique_ptr<GameSession> m_session;
    int m_nextClientId = 1;
    QString m_saveDir;
    qint64 m_saveLimitBytes = 0;
    // Writes the running game to `path` within the save limit: the oldest saved games
    // (not campaign progress, not `path` itself) are removed until it fits.
    bool writeSave(const QString& path, QString* error);
    // Runs a saved game; connected clients whose codes fit get their seats back, the
    // others are offered the free ones.
    bool loadSaveFile(const QString& path, QString* error);
    // Player count of a scenario, and of a campaign's current chapter (0 = unknown).
    int scenarioSeats(const QString& id) const;
    int campaignSeats(const QString& id) const;
    int m_lastAutosaveDay = 0;
    QString m_gameStamp;  // when this game was started or loaded: part of its autosave names
    // A save at the start of every new day, the oldest beyond the lobby's
    // `autosaves` removed; skipped (and tried again) while a battle runs.
    void autosave();
    void saveCampaignStart();
    QJsonArray m_scenarios;  // for the welcome message
    QJsonArray campaignsJson() const;
    // Scenarios and campaigns: the game's own and the generated ones (saves/scenarios, saves/campaigns).
    void refreshContentLists();
    QStringList contentDirs(const QString& kind) const;
    void generateContent(Client& client, const net::Message& message);  // with each campaign's progress (saves folder)
};

}  // namespace kot::server
