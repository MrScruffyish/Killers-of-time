#include "net/LobbyTypes.h"

#include <QJsonArray>

#include "core/data/JsonReader.h"

namespace kot::net {

using core::JsonReader;

const QStringList& defaultPlayerColors() {
    static const QStringList colors = {"#d62728", "#1f77b4", "#2ca02c", "#ff7f0e",
                                       "#9467bd", "#17becf", "#e377c2", "#8c564b"};
    return colors;
}

QString slotKindName(SlotKind kind) {
    switch (kind) {
        case SlotKind::Open: return QStringLiteral("open");
        case SlotKind::Human: return QStringLiteral("human");
        case SlotKind::Ai: return QStringLiteral("ai");
        case SlotKind::Closed: return QStringLiteral("closed");
        case SlotKind::HotSeat: return QStringLiteral("hotseat");
    }
    return {};
}

bool parseSlotKind(const QString& name, SlotKind* out) {
    for (SlotKind k : {SlotKind::Open, SlotKind::Human, SlotKind::Ai, SlotKind::Closed, SlotKind::HotSeat}) {
        if (slotKindName(k) == name) {
            *out = k;
            return true;
        }
    }
    return false;
}

QJsonObject SlotInfo::toJson() const {
    QJsonObject o;
    o.insert("index", index);
    o.insert("kind", slotKindName(kind));
    o.insert("clientId", clientId);
    o.insert("name", name);
    o.insert("race", race);
    o.insert("team", team);
    o.insert("color", color);
    o.insert("ready", ready);
    o.insert("aiProfile", aiProfile);
    o.insert("difficulty", difficulty);
    return o;
}

bool SlotInfo::fromJson(const QJsonObject& json, const QString& context, SlotInfo* out,
                        QStringList* errors) {
    JsonReader r(json, context, errors);
    SlotInfo s;
    s.index = r.requireInt("index", 0, kMaxSlots - 1);
    const QString kind = r.requireString("kind");
    if (!kind.isEmpty() && !parseSlotKind(kind, &s.kind))
        r.addError(QStringLiteral("unknown slot kind '%1'").arg(kind));
    s.clientId = r.optionalInt("clientId", 0, 0, 2000000000);
    s.name = r.optionalString("name");
    s.race = r.optionalString("race");
    s.team = r.optionalInt("team", 1, 1, kMaxTeams);
    s.color = r.optionalString("color");
    s.ready = r.optionalBool("ready", false);
    s.aiProfile = r.optionalString("aiProfile");
    s.difficulty = r.optionalString("difficulty");
    r.finish();
    if (!r.ok()) return false;
    *out = s;
    return true;
}

QJsonObject LobbyState::toJson() const {
    QJsonObject root;
    QJsonArray memberArray;
    for (const LobbyMember& m : members) {
        QJsonObject o;
        o.insert("clientId", m.clientId);
        o.insert("name", m.name);
        o.insert("isHost", m.isHost);
        memberArray.append(o);
    }
    root.insert("members", memberArray);
    QJsonArray slotArray;
    for (const SlotInfo& s : slotList) slotArray.append(s.toJson());
    root.insert("slots", slotArray);
    QJsonObject st;
    st.insert("mapWidth", settings.mapWidth);
    st.insert("mapHeight", settings.mapHeight);
    st.insert("seed", QString::number(settings.seed));  // 64-bit: sent as text
    st.insert("difficulty", settings.difficulty);
    st.insert("scenario", settings.scenario);
    st.insert("campaign", settings.campaign);
    st.insert("mapType", settings.mapType);
    st.insert("autosaves", settings.autosaves);
    st.insert("fogOfWar", settings.fogOfWar);
    root.insert("settings", st);
    root.insert("started", started);
    return root;
}

bool LobbyState::fromJson(const QJsonObject& json, LobbyState* out, QStringList* errors) {
    QStringList local;
    QStringList* errs = errors ? errors : &local;
    JsonReader top(json, QStringLiteral("lobby_state"), errs);
    const QJsonArray memberArray = top.requireArray("members");
    const QJsonArray slotArray = top.requireArray("slots");
    const QJsonObject settingsObject = top.requireObject("settings");
    const bool started = top.requireBool("started");
    top.finish();
    if (!top.ok()) return false;

    LobbyState s;
    s.started = started;
    for (qsizetype i = 0; i < memberArray.size(); ++i) {
        const QString ctx = QStringLiteral("lobby_state.members[%1]").arg(i);
        if (!memberArray[i].isObject()) {
            errs->append(ctx + QStringLiteral(": entry must be an object"));
            return false;
        }
        JsonReader r(memberArray[i].toObject(), ctx, errs);
        LobbyMember m;
        m.clientId = r.requireInt("clientId", 1, 2000000000);
        m.name = r.requireString("name");
        m.isHost = r.requireBool("isHost");
        r.finish();
        if (!r.ok()) return false;
        s.members.append(m);
    }
    for (qsizetype i = 0; i < slotArray.size(); ++i) {
        const QString ctx = QStringLiteral("lobby_state.slots[%1]").arg(i);
        if (!slotArray[i].isObject()) {
            errs->append(ctx + QStringLiteral(": entry must be an object"));
            return false;
        }
        SlotInfo slot;
        if (!SlotInfo::fromJson(slotArray[i].toObject(), ctx, &slot, errs)) return false;
        s.slotList.append(slot);
    }
    JsonReader sr(settingsObject, QStringLiteral("lobby_state.settings"), errs);
    s.settings.mapWidth = sr.requireInt("mapWidth", 16, 256);
    s.settings.mapHeight = sr.requireInt("mapHeight", 16, 256);
    const QString seedText = sr.requireString("seed");
    s.settings.difficulty = sr.optionalString("difficulty", QStringLiteral("normal"));
    s.settings.scenario = sr.optionalString("scenario");
    s.settings.campaign = sr.optionalString("campaign");
    s.settings.mapType = sr.optionalString("mapType", QStringLiteral("continent"));
    s.settings.autosaves = sr.optionalInt("autosaves", 5, 0, 100);
    s.settings.fogOfWar = sr.optionalBool("fogOfWar", true);
    sr.finish();
    bool ok = false;
    s.settings.seed = seedText.toULongLong(&ok);
    if (sr.ok() && !ok) {
        errs->append(QStringLiteral("lobby_state.settings: seed must be a number written as text"));
        return false;
    }
    if (!sr.ok()) return false;
    *out = s;
    return true;
}

}  // namespace kot::net
