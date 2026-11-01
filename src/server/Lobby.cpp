#include "server/Lobby.h"

#include <QRegularExpression>
#include <algorithm>

namespace kot::server {

using net::LobbyMember;
using net::SlotInfo;
using net::SlotKind;

namespace {
const QStringList kPalette = {"#d62728", "#1f77b4", "#2ca02c", "#ff7f0e",
                              "#9467bd", "#17becf", "#e377c2", "#8c564b"};
const QStringList kDifficulties = {"easy", "normal", "hard"};

LobbyResult started() {
    return LobbyResult::failure("already_started", "the game has already started");
}
}  // namespace

const QStringList& Lobby::palette() { return kPalette; }

Lobby::Lobby(QStringList races, QStringList aiProfiles, int slotCount)
    : m_races(std::move(races)), m_aiProfiles(std::move(aiProfiles)) {
    if (m_races.isEmpty()) m_races << QStringLiteral("humans");
    if (m_aiProfiles.isEmpty()) m_aiProfiles << QStringLiteral("passive");
    slotCount = std::clamp(slotCount, 2, net::kMaxSlots);
    for (int i = 0; i < slotCount; ++i) m_state.slotList.append(freshSlot(i));
}

SlotInfo Lobby::freshSlot(int index) const {
    SlotInfo s;
    s.index = index;
    s.kind = SlotKind::Open;
    s.race = m_races.first();
    s.team = index + 1;
    s.color = kPalette[index % kPalette.size()];
    return s;
}

bool Lobby::colorTaken(const QString& color, int exceptSlot) const {
    for (const SlotInfo& s : m_state.slotList) {
        if (s.index != exceptSlot && s.occupied() && s.color.compare(color, Qt::CaseInsensitive) == 0)
            return true;
    }
    return false;
}

QString Lobby::unusedColor(int exceptSlot) const {
    for (const QString& c : kPalette) {
        if (!colorTaken(c, exceptSlot)) return c;
    }
    return kPalette.first();
}

LobbyMember* Lobby::member(int clientId) {
    for (LobbyMember& m : m_state.members) {
        if (m.clientId == clientId) return &m;
    }
    return nullptr;
}

bool Lobby::hasMember(int clientId) const {
    return const_cast<Lobby*>(this)->member(clientId) != nullptr;
}

int Lobby::hostId() const {
    for (const LobbyMember& m : m_state.members) {
        if (m.isHost) return m.clientId;
    }
    return 0;
}

int Lobby::slotOf(int clientId) const {
    for (const SlotInfo& s : m_state.slotList) {
        if (s.kind == SlotKind::Human && s.clientId == clientId) return s.index;
    }
    return -1;
}

void Lobby::vacate(int slotIndex) {
    m_state.slotList[slotIndex] = freshSlot(slotIndex);
    if (colorTaken(m_state.slotList[slotIndex].color, slotIndex))
        m_state.slotList[slotIndex].color = unusedColor(slotIndex);
}

LobbyResult Lobby::addMember(int clientId, const QString& name) {
    if (hasMember(clientId)) return LobbyResult::failure("already_joined", "already in the lobby");
    LobbyMember m;
    m.clientId = clientId;
    m.name = name;
    m.isHost = m_state.members.isEmpty();
    m_state.members.append(m);
    if (!m_state.started) {
        for (const SlotInfo& s : m_state.slotList) {
            if (s.kind == SlotKind::Open) {
                claimSlot(clientId, s.index);
                break;
            }
        }
    }
    return LobbyResult::success();
}

void Lobby::removeMember(int clientId) {
    const int slot = slotOf(clientId);
    if (slot >= 0) vacate(slot);
    const bool wasHost = (hostId() == clientId);
    m_state.members.removeIf([&](const LobbyMember& m) { return m.clientId == clientId; });
    if (wasHost && !m_state.members.isEmpty()) m_state.members.first().isHost = true;
}

LobbyResult Lobby::claimSlot(int clientId, int slot) {
    LobbyMember* m = member(clientId);
    if (!m) return LobbyResult::failure("unknown_client", "not in the lobby");
    if (m_state.started) return started();
    if (slot < 0 || slot >= m_state.slotList.size())
        return LobbyResult::failure("no_such_slot", "there is no such slot");
    SlotInfo& target = m_state.slotList[slot];
    if (target.kind == SlotKind::Closed) return LobbyResult::failure("slot_closed", "that slot is closed");
    if (target.kind != SlotKind::Open) return LobbyResult::failure("slot_taken", "that slot is taken");

    const int old = slotOf(clientId);
    if (old >= 0) vacate(old);
    target.kind = SlotKind::Human;
    target.clientId = clientId;
    target.name = m->name;
    target.ready = false;
    target.aiProfile.clear();
    target.difficulty.clear();
    if (colorTaken(target.color, slot)) target.color = unusedColor(slot);
    return LobbyResult::success();
}

LobbyResult Lobby::releaseSlot(int clientId) {
    if (!hasMember(clientId)) return LobbyResult::failure("unknown_client", "not in the lobby");
    if (m_state.started) return started();
    const int slot = slotOf(clientId);
    if (slot < 0) return LobbyResult::failure("no_slot", "you hold no slot");
    vacate(slot);
    return LobbyResult::success();
}

LobbyResult Lobby::editSlot(int actorId, int slot, const QJsonObject& patch) {
    LobbyMember* actor = member(actorId);
    if (!actor) return LobbyResult::failure("unknown_client", "not in the lobby");
    if (m_state.started) return started();
    if (slot < 0 || slot >= m_state.slotList.size())
        return LobbyResult::failure("no_such_slot", "there is no such slot");
    const bool host = actor->isHost;
    SlotInfo& current = m_state.slotList[slot];
    if (!host && !(current.kind == SlotKind::Human && current.clientId == actorId))
        return LobbyResult::failure("not_your_slot", "you can only change your own slot");

    SlotInfo s = current;  // work on a copy, apply only if everything is valid
    bool kicked = false;
    for (auto it = patch.begin(); it != patch.end(); ++it) {
        const QString key = it.key();
        const QJsonValue v = it.value();
        const bool hostOnly = (key == "kind" || key == "name" || key == "aiProfile" || key == "difficulty");
        if (hostOnly && !host) return LobbyResult::failure("not_host", "only the host can change '" + key + "'");
        const auto bad = [&](const QString& why) { return LobbyResult::failure("invalid_value", key + ": " + why); };

        if (key == "kind") {
            SlotKind kind;
            if (!v.isString() || !net::parseSlotKind(v.toString(), &kind) || kind == SlotKind::Human)
                return bad("must be open, ai or closed (players claim human slots themselves)");
            if (s.kind == SlotKind::Human) kicked = true;
            const bool wasAi = (s.kind == SlotKind::Ai);
            s.kind = kind;
            s.clientId = 0;
            s.ready = (kind == SlotKind::Ai);
            if (kind == SlotKind::Ai) {
                if (!wasAi) {
                    s.name = QStringLiteral("AI %1").arg(slot + 1);
                    s.aiProfile = m_aiProfiles.first();
                    s.difficulty = QStringLiteral("normal");
                }
            } else {
                s.name.clear();
                s.aiProfile.clear();
                s.difficulty.clear();
            }
        } else if (key == "race") {
            if (!v.isString() || !m_races.contains(v.toString()))
                return bad("unknown race (" + m_races.join(", ") + ")");
            s.race = v.toString();
        } else if (key == "team") {
            const int t = v.toInt(0);
            if (!v.isDouble() || t < 1 || t > net::kMaxTeams) return bad("must be a number from 1 to 8");
            s.team = t;
        } else if (key == "color") {
            static const QRegularExpression re("^#[0-9a-fA-F]{6}$");
            if (!v.isString() || !re.match(v.toString()).hasMatch()) return bad("must be a colour like #RRGGBB");
            s.color = v.toString().toLower();
        } else if (key == "name") {
            const QString n = v.toString().trimmed();
            if (!v.isString() || n.isEmpty() || n.size() > 24 || s.kind != SlotKind::Ai)
                return bad("only AI slots have a name set by the host (1 to 24 characters)");
            s.name = n;
        } else if (key == "aiProfile") {
            if (!v.isString() || !m_aiProfiles.contains(v.toString()) || s.kind != SlotKind::Ai)
                return bad("must be one of " + m_aiProfiles.join(", ") + " and the slot must be an AI");
            s.aiProfile = v.toString();
        } else if (key == "difficulty") {
            if (!v.isString() || !kDifficulties.contains(v.toString()) || s.kind != SlotKind::Ai)
                return bad("must be easy, normal or hard and the slot must be an AI");
            s.difficulty = v.toString();
        } else {
            return LobbyResult::failure("invalid_value", "unknown setting '" + key + "'");
        }
    }
    if (s.occupied() && colorTaken(s.color, slot))
        return LobbyResult::failure("color_taken", "that colour is already used by another player");

    if (s.kind == SlotKind::Human) s.ready = false;  // changed settings must be confirmed again
    current = s;
    (void)kicked;
    return LobbyResult::success();
}

LobbyResult Lobby::setReady(int clientId, bool ready) {
    if (!hasMember(clientId)) return LobbyResult::failure("unknown_client", "not in the lobby");
    if (m_state.started) return started();
    const int slot = slotOf(clientId);
    if (slot < 0) return LobbyResult::failure("no_slot", "claim a slot first");
    m_state.slotList[slot].ready = ready;
    return LobbyResult::success();
}

LobbyResult Lobby::setSettings(int actorId, const QJsonObject& patch) {
    LobbyMember* actor = member(actorId);
    if (!actor) return LobbyResult::failure("unknown_client", "not in the lobby");
    if (!actor->isHost) return LobbyResult::failure("not_host", "only the host can change the settings");
    if (m_state.started) return started();
    net::LobbySettings s = m_state.settings;
    for (auto it = patch.begin(); it != patch.end(); ++it) {
        const QString key = it.key();
        const QJsonValue v = it.value();
        if (key == "mapWidth" || key == "mapHeight") {
            const int n = v.toInt(0);
            if (!v.isDouble() || n < 16 || n > 256)
                return LobbyResult::failure("invalid_value", key + ": must be between 16 and 256");
            (key == "mapWidth" ? s.mapWidth : s.mapHeight) = n;
        } else if (key == "seed") {
            bool ok = false;
            const quint64 seed = v.isString() ? v.toString().toULongLong(&ok)
                                              : (v.isDouble() && v.toDouble() >= 0 ? (ok = true, static_cast<quint64>(v.toDouble())) : 0);
            if (!ok) return LobbyResult::failure("invalid_value", "seed: must be a number");
            s.seed = seed;
        } else {
            return LobbyResult::failure("invalid_value", "unknown setting '" + key + "'");
        }
    }
    m_state.settings = s;
    for (SlotInfo& slot : m_state.slotList) {
        if (slot.kind == SlotKind::Human) slot.ready = false;  // map changed: confirm again
    }
    return LobbyResult::success();
}

LobbyResult Lobby::checkStart(int actorId) const {
    const auto* actor = const_cast<Lobby*>(this)->member(actorId);
    if (!actor) return LobbyResult::failure("unknown_client", "not in the lobby");
    if (!actor->isHost) return LobbyResult::failure("not_host", "only the host can start the game");
    if (m_state.started) return started();
    int humans = 0, occupied = 0;
    QSet<int> teams;
    for (const SlotInfo& s : m_state.slotList) {
        if (!s.occupied()) continue;
        ++occupied;
        teams.insert(s.team);
        if (s.kind == SlotKind::Human) {
            ++humans;
            if (!s.ready)
                return LobbyResult::failure("not_ready", s.name + " is not ready");
        }
    }
    if (humans == 0) return LobbyResult::failure("cannot_start", "at least one human player is needed");
    if (occupied < 2) return LobbyResult::failure("cannot_start", "at least two players are needed");
    if (teams.size() < 2) return LobbyResult::failure("cannot_start", "there must be at least two teams");
    return LobbyResult::success();
}

LobbyResult Lobby::start(int actorId) {
    const LobbyResult check = checkStart(actorId);
    if (check.ok) m_state.started = true;
    return check;
}

}  // namespace kot::server
