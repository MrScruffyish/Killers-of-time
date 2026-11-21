#include "core/rules/Campaign.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <algorithm>
#include <limits>

#include "core/content/ContentRegistry.h"
#include "core/rules/HeroRules.h"
#include "core/rules/Movement.h"

namespace kot::core {

namespace {
QJsonObject readObject(const QString& path, QString* why) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (why) *why = QStringLiteral("cannot open");
        return {};
    }
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &pe);
    if (!doc.isObject() && why) *why = pe.errorString();
    return doc.object();
}
}  // namespace

bool loadCampaign(const QString& path, CampaignDef* out, QStringList* errors) {
    QString why;
    const QJsonObject o = readObject(path, &why);
    CampaignDef d;
    d.id = o.value("id").toString();
    d.name = o.value("name").toString(d.id);
    d.description = o.value("description").toString();
    for (const QJsonValue& v : o.value("scenarios").toArray()) d.scenarios << v.toString();
    const QJsonObject carry = o.value("carryOver").toObject();
    d.carryHeroes = std::clamp(carry.value("heroes").toInt(1), 0, 8);
    d.maxLevel = std::clamp(carry.value("maxLevel").toInt(0), 0, 1000);  // 0 = no cap
    if (d.id.isEmpty() || d.scenarios.isEmpty()) {
        if (errors) *errors << QStringLiteral("%1: a campaign needs an id and scenarios%2").arg(path, why.isEmpty() ? "" : " (" + why + ")");
        return false;
    }
    *out = d;
    return true;
}

QList<CampaignDef> listCampaigns(const QString& dir) {
    QList<CampaignDef> list;
    for (const QFileInfo& fi : QDir(dir).entryInfoList({"*.json"}, QDir::Files, QDir::Name)) {
        CampaignDef d;
        if (loadCampaign(fi.filePath(), &d, nullptr)) list << d;
    }
    return list;
}

bool loadProgress(const QString& path, CampaignProgress* out) {
    if (!QFile::exists(path)) return false;
    const QJsonObject o = readObject(path, nullptr);
    CampaignProgress p;
    p.campaign = o.value("campaign").toString();
    p.next = std::max(0, o.value("next").toInt());
    for (const QJsonValue& v : o.value("heroes").toArray()) p.heroes << v.toObject();
    *out = p;
    return true;
}

bool saveProgress(const QString& path, const CampaignProgress& progress, QString* error) {
    QJsonObject o;
    o.insert("campaign", progress.campaign);
    o.insert("next", progress.next);
    QJsonArray heroes;
    for (const QJsonObject& h : progress.heroes) heroes.append(h);
    o.insert("heroes", heroes);
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        if (error) *error = f.errorString();
        return false;
    }
    f.write(QJsonDocument(o).toJson());
    return f.commit();
}

QJsonObject heroSnapshot(const GameState& state, const ContentRegistry& content, HeroId id) {
    const Hero* h = state.hero(id);
    if (!h) return {};
    // Only the identity and the level outlive a chapter. Older progress files may carry
    // more fields; applyCarriedHeroes() ignores them.
    return {{"name", h->name}, {"class", content.heroClass(h->classId)->key}, {"level", h->level}};
}

QList<HeroId> bestHeroes(const GameState& state, PlayerId player, int count) {
    QList<const Hero*> heroes;
    for (const Hero& h : state.heroes())
        if (h.owner == player) heroes << &h;
    std::stable_sort(heroes.begin(), heroes.end(), [](const Hero* a, const Hero* b) {
        return a->level != b->level ? a->level > b->level : a->experience > b->experience;
    });
    QList<HeroId> ids;
    for (int i = 0; i < heroes.size() && i < count; ++i) ids << heroes[i]->id;
    return ids;
}

int applyCarriedHeroes(GameState& state, const ContentRegistry& content, PlayerId player,
                       const QList<QJsonObject>& carried, int maxLevel) {
    QList<HeroId> mine;
    for (const Hero& h : state.heroes())
        if (h.owner == player) mine << h.id;
    std::sort(mine.begin(), mine.end());
    if (mine.isEmpty()) return 0;
    const Pos base = state.army(state.hero(mine.first())->army)->pos;
    const int cap = maxLevel > 0 ? maxLevel : std::numeric_limits<int>::max();
    int applied = 0;
    for (int i = 0; i < carried.size(); ++i) {
        const QJsonObject& c = carried[i];
        const HeroClassId cls = content.heroClassId(c.value("class").toString());  // keys in campaign progress
        if (!cls.isValid()) continue;
        HeroId id = i < mine.size() ? mine[i] : HeroId();
        if (!id.isValid()) {
            id = createHero(state, content, player, c.value("name").toString(), cls, base);
            if (!id.isValid()) continue;
        }
        Hero* h = state.hero(id);
        const int level = std::clamp(c.value("level").toInt(1), 1, cap);
        h->name = c.value("name").toString(h->name);
        h->classId = cls;
        h->level = level;
        h->experience = 0;
        h->pendingTraitPicks = 0;
        // The new chapter keeps its own army, spells, artifacts and backpack. Traits
        // follow the class and the level, as for a hero who grew to it normally.
        const HeroClassDef* def = content.heroClass(cls);
        h->traits = def ? def->baseTraits : TraitValues();
        if (def)
            for (int lv = 2; lv <= level; ++lv)
                for (const auto [t, v] : def->autoGrowth) h->traits[t] += v;
        h->spellPoints = maxSpellPoints(*h, content);
        ++applied;
    }
    refreshHeroMovement(state, content, player);
    return applied;
}

}  // namespace kot::core
