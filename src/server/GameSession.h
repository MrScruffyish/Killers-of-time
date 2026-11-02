#pragma once

#include <QHash>
#include <QJsonObject>
#include <QSet>
#include <QString>

#include "core/model/GameState.h"
#include "net/LobbyTypes.h"
#include "net/Message.h"

namespace kot::core { class ContentRegistry; }

namespace kot::server {

struct CommandResult {
    bool ok = false;
    QString code;      // on failure: "not_your_turn", "bad_request", "failed", ...
    QString message;
    QJsonObject info;  // on success: details for the sender (what happened)
};

// One running game on the server. Owns the GameState, checks every command of every
// player (right turn, own things), applies it with the rules in core/rules, and
// builds each player's filtered view. AI players and players who left are played
// by a passive AI that just ends the turn.
class GameSession {
public:
    explicit GameSession(const core::ContentRegistry& content);

    // Creates the game from the finished lobby. On failure `error` says why.
    bool start(const net::LobbyState& lobby, QString* error);
    bool active() const { return m_active; }

    core::GameState& state() { return m_state; }
    const core::GameState& state() const { return m_state; }
    core::PlayerId playerOfClient(int clientId) const { return m_clientPlayers.value(clientId); }
    QList<int> clients() const { return m_clientPlayers.keys(); }

    CommandResult handle(int clientId, const net::Message& command);
    // A human left: his player is now played by the passive AI (the game goes on).
    void clientLeft(int clientId);

    QJsonObject viewFor(core::PlayerId player) const;
    QJsonObject youInfo(core::PlayerId player) const;

private:
    CommandResult dispatch(core::PlayerId player, const net::Message& command);
    void playAutoTurns();
    bool isAuto(core::PlayerId p) const { return m_autoPlayers.contains(p); }

    const core::ContentRegistry& m_content;
    core::GameState m_state;
    bool m_active = false;
    QHash<int, core::PlayerId> m_clientPlayers;  // connected humans
    QSet<core::PlayerId> m_autoPlayers;          // AI and abandoned players
};

}  // namespace kot::server
