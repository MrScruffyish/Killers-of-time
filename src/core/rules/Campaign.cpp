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
    const QJsonObject legacy = o.value("legacy").toObject();
    d.legacy.kind = legacy.value("kind").toString();
    d.legacy.title = legacy.value("title").toString();
    d.legacy.intro = legacy.value("intro").toString();
    d.legacy.epilogue = legacy.value("epilogue").toString();
    for (const QJsonValue& v : legacy.value("chapters").toArray()) {
        const QJsonObject ch = v.toObject();
        LegacyChapter award;
        award.story = ch.value("story").toString();
        for (const QJsonValue& s : ch.value("spells").toArray()) award.spells << s.toString();
        for (const QJsonValue& b : ch.value("monuments").toArray()) award.monuments << b.toString();
        d.legacy.chapters << award;
    }
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
    for (const QJsonValue& v : o.value("artifacts").toArray()) p.artifacts << v.toString();
    for (const QJsonValue& v : o.value("spells").toArray()) p.spells << v.toString();
    for (const QJsonValue& v : o.value("monuments").toArray()) p.monuments << v.toString();
    for (const QJsonValue& v : o.value("records").toArray()) p.records << v.toInt();
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
    QJsonArray artifacts;
    for (const QString& a : progress.artifacts) artifacts.append(a);
    o.insert("artifacts", artifacts);
    QJsonArray spells;
    for (const QString& s : progress.spells) spells.append(s);
    o.insert("spells", spells);
    QJsonArray monuments;
    for (const QString& b : progress.monuments) monuments.append(b);
    o.insert("monuments", monuments);
    QJsonArray records;
    for (const int day : progress.records) records.append(day);
    o.insert("records", records);
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

int giveGoalArtifacts(GameState& state, const ContentRegistry& content, PlayerId player, const QStringList& keys) {
    QList<HeroId> mine;
    for (const Hero& h : state.heroes())
        if (h.owner == player) mine << h.id;
    if (mine.isEmpty()) return 0;
    std::sort(mine.begin(), mine.end());
    Hero* hero = state.hero(mine.first());
    if (!hero) return 0;
    int given = 0;
    for (const QString& key : keys) {
        const ArtifactId id = content.artifactId(key);
        if (!id.isValid()) continue;
        if (hero->artifacts.contains(id) || hero->backpack.contains(id)) continue;
        takeArtifact(*hero, content, id);  // worn in a free slot, or carried in the backpack
        ++given;
    }
    return given;
}

int applyLegacy(GameState& state, const ContentRegistry& content, PlayerId player, const CampaignProgress& progress) {
    int granted = 0;
    QList<HeroId> mine;
    for (const Hero& h : state.heroes())
        if (h.owner == player) mine << h.id;
    std::sort(mine.begin(), mine.end());
    Hero* hero = mine.isEmpty() ? nullptr : state.hero(mine.first());

    // Learned spells travel with the carried hero (first one), like goal artifacts.
    for (const QString& key : progress.spells) {
        const SpellId id = content.spellId(key);
        if (!id.isValid() || !hero || hero->spells.contains(id)) continue;
        hero->spells.append(id);
        ++granted;
    }

    // Monuments are permanently built in the player's capital (his first town). They are
    // free legacy gifts, not bought through the normal build rules.
    Town* capital = nullptr;
    for (Town& t : state.towns())
        if (t.owner == player) {
            capital = &t;
            break;
        }
    if (capital)
        for (const QString& key : progress.monuments) {
            const BuildingTypeId id = content.buildingId(key);
            if (!id.isValid() || capital->buildings.contains(id)) continue;
            capital->buildings.append(id);
            ++granted;
        }
    return granted;
}

}  // namespace kot::core
