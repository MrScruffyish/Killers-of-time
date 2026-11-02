#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>

#include "net/LobbyTypes.h"

namespace kot::server {

struct LobbyResult {
    bool ok = false;
    QString code;     // machine readable: "not_host", "slot_taken", ...
    QString message;  // human readable

    static LobbyResult success() { LobbyResult r; r.ok = true; return r; }
    static LobbyResult failure(const QString& code, const QString& message) {
        LobbyResult r;
        r.code = code;
        r.message = message;
        return r;
    }
};

// The game setup before the game starts: members, slots (human / AI / open / closed),
// settings. Pure logic: the server calls it for every request and broadcasts state().
// Rules: the first member is the host and can configure everything; others can
// claim an open slot and change their own race, team and colour. Colours are unique
// among occupied slots. Any change to a human's slot clears his ready flag.
class Lobby {
public:
    Lobby(QStringList races, QStringList aiProfiles, int slotCount = 4);

    const net::LobbyState& state() const { return m_state; }
    bool hasMember(int clientId) const;
    int hostId() const;
    int slotOf(int clientId) const;  // -1 if the member holds no slot

    // The new member takes the first open slot, if there is one.
    LobbyResult addMember(int clientId, const QString& name);
    // Frees the member's slot; the next member becomes host if the host leaves.
    void removeMember(int clientId);

    LobbyResult claimSlot(int clientId, int slot);
    LobbyResult releaseSlot(int clientId);
    // patch keys: kind ("open"/"ai"/"closed", host only), race, team, color,
    // name, aiProfile, difficulty (host only, AI slots).
    LobbyResult editSlot(int actorId, int slot, const QJsonObject& patch);
    LobbyResult setReady(int clientId, bool ready);
    // patch keys (host only): mapWidth, mapHeight (16..256), seed.
    LobbyResult setSettings(int actorId, const QJsonObject& patch);

    // The host may start when: at least one human, two occupied slots on two
    // different teams, and every human is ready.
    LobbyResult checkStart(int actorId) const;
    LobbyResult start(int actorId);
    // Undo start() when the game could not be created.
    void cancelStart() { m_state.started = false; }

    static const QStringList& palette();

private:
    net::SlotInfo freshSlot(int index) const;
    QString unusedColor(int exceptSlot) const;
    bool colorTaken(const QString& color, int exceptSlot) const;
    net::LobbyMember* member(int clientId);
    void vacate(int slotIndex);

    QStringList m_races;
    QStringList m_aiProfiles;
    net::LobbyState m_state;
};

}  // namespace kot::server
