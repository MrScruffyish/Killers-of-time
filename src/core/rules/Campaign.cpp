#include "core/rules/Campaign.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <algorithm>

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
    d.carryArmy = carry.value("army").toBool(false);
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
    QJsonObject o;
    o.insert("name", h->name);
    o.insert("class", content.heroClass(h->classId)->key);
    o.insert("level", h->level);
    o.insert("experience", h->experience);
    QJsonObject traits;
    for (const auto [t, v] : h->traits) traits.insert(content.trait(t)->key, v);  // keys
    o.insert("traits", traits);
    {
        QJsonArray spells;  // keys (campaign progress outlives data changes better)
        for (const SpellId s : h->spells) spells.append(content.spell(s)->key);
        o.insert("spells", spells);
    }
    for (const char* field : {"artifacts", "backpack"}) {
        QJsonArray list;  // keys
        for (const ArtifactId a : (QLatin1String(field) == QLatin1String("artifacts") ? h->artifacts : h->backpack)) list.append(content.artifact(a)->key);
        o.insert(QLatin1String(field), list);
    }
    o.insert("pendingTraitPicks", h->pendingTraitPicks);
    QJsonArray stacks;
    if (const Army* a = state.army(h->army))
        for (const UnitStack& s : a->stacks)
            stacks.append(QJsonObject{{"unit", content.unit(s.unitTypeId())->key}, {"count", s.count()}, {"experience", s.experience()}});
    o.insert("army", stacks);
    return o;
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
                       const QList<QJsonObject>& carried, bool withArmy) {
    QList<HeroId> mine;
    for (const Hero& h : state.heroes())
        if (h.owner == player) mine << h.id;
    std::sort(mine.begin(), mine.end());
    if (mine.isEmpty()) return 0;
    const Pos base = state.army(state.hero(mine.first())->army)->pos;
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
        h->name = c.value("name").toString(h->name);
        h->classId = cls;
        h->level = std::max(1, c.value("level").toInt(1));
        h->experience = c.value("experience").toInt();
        h->pendingTraitPicks = c.value("pendingTraitPicks").toInt();
        h->traits = TraitValues();
        const QJsonObject traits = c.value("traits").toObject();
        for (auto it = traits.begin(); it != traits.end(); ++it)
            if (const TraitId t = content.traitId(it.key()); t.isValid()) h->traits[t] = it.value().toInt();
        h->spells.clear();
        for (const QJsonValue& v : c.value("spells").toArray())
            if (content.spellId(v.toString()).isValid()) h->spells << content.spellId(v.toString());
        h->artifacts.clear();
        for (const QJsonValue& v : c.value("artifacts").toArray())
            if (content.artifactId(v.toString()).isValid()) h->artifacts << content.artifactId(v.toString());
        h->backpack.clear();
        for (const QJsonValue& v : c.value("backpack").toArray())
            if (content.artifactId(v.toString()).isValid()) h->backpack << content.artifactId(v.toString());
        h->spellPoints = maxSpellPoints(*h, content);
        if (withArmy) {
            Army* a = state.army(h->army);
            a->stacks.clear();
            for (const QJsonValue& v : c.value("army").toArray()) {
                const QJsonObject s = v.toObject();
                const UnitDef* u = content.unitByKey(s.value("unit").toString());
                if (!u || !a->addUnits(u->id, s.value("count").toInt(), u->maxHp)) continue;
                a->stacks.last().setExperience(s.value("experience").toInt());
            }
        }
        ++applied;
    }
    refreshHeroMovement(state, content, player);
    return applied;
}

}  // namespace kot::core
