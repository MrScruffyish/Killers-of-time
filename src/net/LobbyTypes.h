#pragma once

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

namespace kot::net {

enum class SlotKind { Open, Human, Ai, Closed };

QString slotKindName(SlotKind kind);
bool parseSlotKind(const QString& name, SlotKind* out);

inline constexpr int kMaxSlots = 8;
inline constexpr int kMaxTeams = 8;

// The eight default player colours, in slot order.
const QStringList& defaultPlayerColors();

// One player position in a game being set up.
struct SlotInfo {
    int index = 0;
    SlotKind kind = SlotKind::Open;
    int clientId = 0;        // the human in this slot (kind Human)
    QString name;            // player name, or AI name
    QString race;
    int team = 1;            // 1..8; slots with the same team play together
    QString color;           // "#RRGGBB", unique among occupied slots
    bool ready = false;      // AI slots are always ready
    QString aiProfile;       // kind Ai
    QString difficulty;      // kind Ai: easy / normal / hard

    bool occupied() const { return kind == SlotKind::Human || kind == SlotKind::Ai; }
    QJsonObject toJson() const;
    static bool fromJson(const QJsonObject& json, const QString& context, SlotInfo* out,
                         QStringList* errors);
};

struct LobbyMember {
    int clientId = 0;
    QString name;
    bool isHost = false;
};

struct LobbySettings {
    int mapWidth = 64;
    int mapHeight = 64;
    quint64 seed = 1;
    QString difficulty = QStringLiteral("normal");  // easy, normal, hard, expert (balance.json)
    QString scenario;  // scenario id (data/scenarios), empty = a random map
    QString campaign;  // campaign id (data/campaigns): its next chapter is played
};

// Everything a client needs to show the game setup screen.
struct LobbyState {
    QList<LobbyMember> members;
    QList<SlotInfo> slotList;
    LobbySettings settings;
    bool started = false;

    QJsonObject toJson() const;
    static bool fromJson(const QJsonObject& json, LobbyState* out, QStringList* errors);
};

}  // namespace kot::net
