// Save/load of GameState as JSON. See docs/save-format.md.
#include <QFile>
#include <QFileInfo>
#include <QDateTime>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSet>
#include <algorithm>
#include <climits>

#include "core/rules/MapSites.h"
#include "core/content/ContentRegistry.h"
#include "core/Version.h"
#include "core/data/JsonReader.h"
#include "core/model/GameState.h"
#include "core/content/IdJson.h"

namespace kot::core {

namespace {

constexpr int kMaxId = INT_MAX;

// Content ids in a save: numbers (positions in the content lists) or the text keys of
// the data files (GameState::IdStyle). One place for every kind of id.
struct Ids {
    const ContentRegistry& content;
    bool text;

    QJsonValue resource(ResourceId r) const { return text ? QJsonValue(content.resource(r).key) : QJsonValue(r.index()); }
    QJsonValue amounts(const ResourceAmounts& a) const {
        if (!text) return amountsToJson(a);
        QJsonObject o;
        for (const auto [r, amount] : a) o.insert(content.resource(r).key, amount);
        return o;
    }
    ResourceId readResource(JsonReader& r, const QString& key) const {
        if (text) {
            const QString k = r.requireId(key);
            const ResourceId id = content.resourceId(k);
            if (!k.isEmpty() && !id.isValid()) r.addError(QStringLiteral("unknown resource '%1'").arg(k));
            return id;
        }
        const int i = r.requireInt(key, 0, ResourceAmounts::kMax - 1);
        if (i >= content.resources().size()) r.addError(QStringLiteral("unknown resource %1").arg(i));
        return ResourceId(i);
    }
    QJsonValue unit(UnitTypeId u) const { return text ? QJsonValue(content.unit(u)->key) : QJsonValue(u.index()); }
    // Reads a unit id or key; unknown ones are errors.
    UnitTypeId readUnit(JsonReader& r, const QString& key) const {
        if (text) {
            const QString k = r.requireId(key);
            const UnitTypeId id = content.unitId(k);
            if (!k.isEmpty() && !id.isValid()) r.addError(QStringLiteral("unknown unit '%1'").arg(k));
            return id;
        }
        const UnitTypeId id(r.requireInt(key, 0, 32767));
        if (r.ok() && !content.unit(id)) r.addError(QStringLiteral("unknown unit %1").arg(id.index()));
        return id;
    }
    // unit -> count, as an object keyed by the unit's key or id number.
    QJsonObject unitCounts(const QMap<UnitTypeId, int>& m) const {
        QJsonObject o;
        for (auto it = m.cbegin(); it != m.cend(); ++it)
            o.insert(text ? content.unit(it.key())->key : QString::number(it.key().index()), it.value());
        return o;
    }
    QMap<UnitTypeId, int> readUnitCounts(JsonReader& r, const QString& key, int min, int max) const {
        QMap<UnitTypeId, int> out;
        const QMap<QString, int> m = r.optionalIntMap(key, min, max);
        for (auto it = m.cbegin(); it != m.cend(); ++it) {
            bool number = false;
            const UnitTypeId id = text ? content.unitId(it.key()) : UnitTypeId(it.key().toInt(&number));
            if (content.unit(id) && (text || number)) out.insert(id, it.value());
            else r.addError(QStringLiteral("'%1': unknown unit '%2'").arg(key, it.key()));
        }
        return out;
    }
    QJsonArray spells(const QList<SpellId>& list) const {
        QJsonArray a;
        for (const SpellId sp : list) a.append(text ? QJsonValue(content.spell(sp)->key) : QJsonValue(sp.index()));
        return a;
    }
    QList<SpellId> readSpells(JsonReader& r, const QString& key) const {
        QList<SpellId> out;
        if (!r.has(key)) return out;
        for (const QJsonValue& v : r.requireArray(key)) {
            const SpellId sp = text ? content.spellId(v.toString()) : SpellId(v.isDouble() ? v.toInt() : -1);
            if (content.spell(sp)) out << sp;
            else r.addError(QStringLiteral("'%1': unknown spell %2").arg(key, v.isDouble() ? QString::number(v.toInt()) : "'" + v.toString() + "'"));
        }
        return out;
    }
    QJsonValue artifact(ArtifactId a) const { return text ? QJsonValue(content.artifact(a)->key) : QJsonValue(a.index()); }
    QJsonArray artifacts(const QList<ArtifactId>& list) const {
        QJsonArray out;
        for (const ArtifactId a : list) out.append(artifact(a));
        return out;
    }
    ArtifactId readArtifact(const QJsonValue& v) const {
        const ArtifactId a = text ? content.artifactId(v.toString()) : ArtifactId(v.isDouble() ? v.toInt() : -1);
        return content.artifact(a) ? a : ArtifactId();
    }
    ArtifactId readArtifactField(JsonReader& r, const QString& key) const {
        const ArtifactId a = text ? content.artifactId(r.requireId(key)) : ArtifactId(r.requireInt(key, 0, 32767));
        return content.artifact(a) ? a : ArtifactId();
    }
    QList<ArtifactId> readArtifacts(JsonReader& r, const QString& key) const {
        QList<ArtifactId> out;
        if (!r.has(key)) return out;
        for (const QJsonValue& v : r.requireArray(key)) {
            const ArtifactId a = readArtifact(v);
            if (a.isValid()) out << a;
            else r.addError(QStringLiteral("'%1': unknown artifact %2").arg(key, v.isDouble() ? QString::number(v.toInt()) : "'" + v.toString() + "'"));
        }
        return out;
    }
    QJsonValue race(RaceId id) const { return text ? QJsonValue(content.race(id).key) : QJsonValue(id.index()); }
    RaceId readRace(JsonReader& r, const QString& key) const {
        const RaceId id = text ? content.raceId(r.requireId(key)) : RaceId(r.requireInt(key, 0, 32767));
        if (r.ok() && (!id.isValid() || id.index() >= content.races().size())) r.addError(QStringLiteral("unknown race"));
        return id;
    }
    QJsonValue heroClass(HeroClassId c) const { return text ? QJsonValue(content.heroClass(c)->key) : QJsonValue(c.index()); }
    HeroClassId readHeroClass(JsonReader& r, const QString& key) const {
        const HeroClassId c = text ? content.heroClassId(r.requireId(key)) : HeroClassId(r.requireInt(key, 0, 32767));
        if (r.ok() && !content.heroClass(c)) r.addError(QStringLiteral("unknown hero class"));
        return c;
    }
    QJsonValue heroTemplate(HeroTemplateId t) const { return text ? QJsonValue(content.heroTemplate(t)->key) : QJsonValue(t.index()); }
    HeroTemplateId readHeroTemplate(const QJsonValue& v) const {
        const HeroTemplateId t = text ? content.heroTemplateId(v.toString()) : HeroTemplateId(v.isDouble() ? v.toInt() : -1);
        return content.heroTemplate(t) ? t : HeroTemplateId();
    }
    QJsonValue traits(const TraitValues& values) const {
        if (text) {
            QJsonObject o;
            for (const auto [t, v] : values) o.insert(content.trait(t)->key, v);
            return o;
        }
        QJsonArray a;
        int last = -1;
        for (const auto [t, v] : values) last = t.index();
        for (int i = 0; i <= last; ++i) a.append(values.value(TraitId(i)));
        return a;
    }
    TraitValues readTraits(JsonReader& r, const QString& key) const {
        TraitValues out;
        if (!r.has(key)) return out;
        if (text) {
            const QMap<QString, int> m = r.optionalIntMap(key, -1000000, 1000000);
            for (auto it = m.cbegin(); it != m.cend(); ++it) {
                const TraitId t = content.traitId(it.key());
                if (t.isValid()) out[t] = it.value();
                else r.addError(QStringLiteral("unknown trait '%1'").arg(it.key()));
            }
            return out;
        }
        const QJsonArray a = r.requireArray(key);
        if (a.size() > content.traits().size()) r.addError(QStringLiteral("'%1' has more values than there are traits").arg(key));
        for (int i = 0; i < a.size() && i < content.traits().size(); ++i) out[TraitId(i)] = a[i].toInt();
        return out;
    }
    QJsonArray buildings(const QList<BuildingTypeId>& list) const {
        QJsonArray a;
        for (const BuildingTypeId b : list) a.append(text ? QJsonValue(content.building(b)->key) : QJsonValue(b.index()));
        return a;
    }
    QList<BuildingTypeId> readBuildings(JsonReader& r, const QString& key) const {
        QList<BuildingTypeId> out;
        if (!r.has(key)) return out;
        for (const QJsonValue& v : r.requireArray(key)) {
            const BuildingTypeId b = text ? content.buildingId(v.toString()) : BuildingTypeId(v.isDouble() ? v.toInt() : -1);
            if (content.building(b)) out << b;
            else r.addError(QStringLiteral("'%1': unknown building %2").arg(key, v.isDouble() ? QString::number(v.toInt()) : "'" + v.toString() + "'"));
        }
        return out;
    }
    // building -> count, as an object keyed by the building's key or id number.
    QJsonObject buildingCounts(const QMap<BuildingTypeId, int>& m) const {
        QJsonObject o;
        for (auto it = m.cbegin(); it != m.cend(); ++it)
            o.insert(text ? content.building(it.key())->key : QString::number(it.key().index()), it.value());
        return o;
    }
    QMap<BuildingTypeId, int> readBuildingCounts(JsonReader& r, const QString& key, int min, int max) const {
        QMap<BuildingTypeId, int> out;
        const QMap<QString, int> m = r.optionalIntMap(key, min, max);
        for (auto it = m.cbegin(); it != m.cend(); ++it) {
            bool number = false;
            const BuildingTypeId id = text ? content.buildingId(it.key()) : BuildingTypeId(it.key().toInt(&number));
            if (content.building(id) && (text || number)) out.insert(id, it.value());
            else r.addError(QStringLiteral("'%1' uses unknown building '%2'").arg(key, it.key()));
        }
        return out;
    }
    ResourceAmounts readAmounts(JsonReader& r, const QString& key, int min, int max) const {
        if (!r.has(key)) return {};
        ResourceAmounts out;
        if (text) {
            const QMap<QString, int> m = r.optionalIntMap(key, min, max);
            for (auto it = m.cbegin(); it != m.cend(); ++it) {
                const ResourceId id = content.resourceId(it.key());
                if (id.isValid()) out[id] = it.value();
                else r.addError(QStringLiteral("'%1': unknown resource '%2'").arg(key, it.key()));
            }
            return out;
        }
        QString error;
        out = core::amountsFromJson(r.requireArray(key), static_cast<int>(content.resources().size()), &error);
        if (!error.isEmpty()) r.addError(QStringLiteral("'%1': %2").arg(key, error));
        for (const auto [res, amount] : out)
            if (amount < min || amount > max) r.addError(QStringLiteral("'%1'[%2] must be %3..%4").arg(key).arg(res.index()).arg(min).arg(max));
        return out;
    }
};

void putOptionalId(QJsonObject& o, const QString& key, quint32 id) {
    if (id != 0) o.insert(key, static_cast<int>(id));
}

}  // namespace

QJsonObject GameState::toJson(const ContentRegistry& content, IdStyle style) const {
    const Ids ids{content, style == IdStyle::Text};
    QJsonObject root;
    root.insert("formatVersion", kSaveFormatVersion);
    if (ids.text) root.insert("contentIds", QStringLiteral("text"));
    else root.insert("contentChecksum", content.checksum());
    root.insert("day", m_day);
    root.insert("difficulty", m_difficulty);
    if (!m_fogOfWar) root.insert("fogOfWar", false);
    if (m_defeatRule != QLatin1String("week")) root.insert("defeat", m_defeatRule);

    if (!m_hiddenMap) root.insert("hiddenMap", false);
    if (!intro.isEmpty()) root.insert("intro", intro);
    if (!storyEvents.isEmpty()) {
        QJsonArray events;
        for (const StoryEvent& e : storyEvents) {
            QJsonObject o{{"day", e.day}, {"text", e.text}};
            if (!e.gift.isEmpty()) o.insert("gift", ids.amounts(e.gift));
            if (e.monster.isValid() && e.monsterCount > 0) {
                o.insert("monster", ids.unit(e.monster));
                o.insert("count", e.monsterCount);
                o.insert("x", e.where.x);
                o.insert("y", e.where.y);
            }
            events.append(o);
        }
        root.insert("events", events);
    }
    if (!news.isEmpty()) root.insert("news", QJsonArray::fromStringList(news));
    if (m_weekEvent.isValid()) root.insert("weekEvent", ids.text ? QJsonValue(content.weekEvent(m_weekEvent)->key) : QJsonValue(m_weekEvent.index()));
    if (!m_history.isEmpty()) root.insert("history", m_history);
    if (m_victory.type != VictoryCondition::Type::Conquest) {
        static const char* names[] = {"conquest", "town", "gold", "days"};
        QJsonObject v{{"type", names[static_cast<int>(m_victory.type)]}};
        if (m_victory.town.isValid()) v.insert("town", static_cast<int>(m_victory.town.value()));
        if (m_victory.amount) v.insert("amount", m_victory.amount);
        root.insert("victory", v);
    }
    root.insert("nextId", static_cast<int>(m_nextId));
    root.insert("currentIndex", m_currentIndex);

    // Map: the terrain id of every tile, row-major; with text ids a palette of the
    // terrain keys and an index into it per tile.
    {
        QJsonObject map;
        map.insert("width", m_map.width());
        map.insert("height", m_map.height());
        QJsonArray tiles;
        if (ids.text) {
            QJsonArray palette;
            QHash<int, int> index;
            for (const TerrainId t : m_map.cells()) {
                auto it = index.find(t.index());
                if (it == index.end()) {
                    it = index.insert(t.index(), static_cast<int>(palette.size()));
                    palette.append(content.terrain(t).key);
                }
                tiles.append(it.value());
            }
            map.insert("terrainPalette", palette);
        } else {
            for (const TerrainId t : m_map.cells()) tiles.append(t.index());
        }
        map.insert("tiles", tiles);
        root.insert("map", map);
    }

    QJsonObject explored;
    for (auto it = m_explored.begin(); it != m_explored.end(); ++it)
        explored.insert(QString::number(it.key().value()),
                        QString::fromLatin1(it.value().toBase64()));
    root.insert("explored", explored);

    QJsonObject rng;
    rng.insert("seed", QString::number(m_rng.seed()));  // string: 64-bit does not fit a JSON number
    rng.insert("state", m_rng.engineState());
    root.insert("rng", rng);

    QJsonArray turnOrder;
    for (PlayerId id : m_turnOrder) turnOrder.append(static_cast<int>(id.value()));
    root.insert("turnOrder", turnOrder);

    QJsonArray players;
    for (const Player& p : m_players) {
        QJsonObject o;
        o.insert("id", static_cast<int>(p.id.value()));
        o.insert("name", p.name);
        o.insert("team", static_cast<int>(p.team.value()));
        o.insert("controller", p.controller == Player::Controller::Ai ? "ai" : "human");
        if (!p.aiProfile.isEmpty()) o.insert("aiProfile", p.aiProfile);
        if (p.incomePercent) o.insert("incomePercent", p.incomePercent);
        if (p.growthPercent) o.insert("growthPercent", p.growthPercent);
        if (p.townlessSince) o.insert("townlessSince", p.townlessSince);
        if (!p.seatKeyHash.isEmpty()) o.insert("seatKeyHash", p.seatKeyHash);
        o.insert("race", ids.race(p.race));
        o.insert("color", p.color);
        o.insert("resources", ids.amounts(p.resources));
        players.append(o);
    }
    root.insert("players", players);

    QJsonArray heroes;
    for (const Hero& h : m_heroes) {
        QJsonObject o;
        o.insert("id", static_cast<int>(h.id.value()));
        o.insert("name", h.name);
        o.insert("class", ids.heroClass(h.classId));
        o.insert("owner", static_cast<int>(h.owner.value()));
        o.insert("level", h.level);
        o.insert("experience", h.experience);
        o.insert("traits", ids.traits(h.traits));
        o.insert("spellPoints", h.spellPoints);
        o.insert("pendingTraitPicks", h.pendingTraitPicks);
        o.insert("movementPoints", h.movementPoints);
        o.insert("army", static_cast<int>(h.army.value()));
        if (h.templateId.isValid()) o.insert("template", ids.heroTemplate(h.templateId));
        if (!h.spells.isEmpty()) o.insert("spells", ids.spells(h.spells));
        if (!h.artifacts.isEmpty()) o.insert("artifacts", ids.artifacts(h.artifacts));
        if (!h.backpack.isEmpty()) o.insert("backpack", ids.artifacts(h.backpack));
        if (h.inBoat) o.insert("inBoat", true);
        if (h.destination) o.insert("destination", QJsonArray{h.destination->x, h.destination->y});
        heroes.append(o);
    }
    root.insert("heroes", heroes);

    QJsonArray armies;
    for (const Army& a : m_armies) {
        QJsonObject o;
        o.insert("id", static_cast<int>(a.id.value()));
        putOptionalId(o, "owner", a.owner.value());
        putOptionalId(o, "hero", a.hero.value());
        o.insert("x", a.pos.x);
        o.insert("y", a.pos.y);
        QJsonArray stacks;
        for (const UnitStack& s : a.stacks) {
            QJsonObject so;
            so.insert("unit", ids.unit(s.unitTypeId()));
            so.insert("maxHp", s.maxHp());
            so.insert("totalHp", s.totalHp());
            if (s.experience() > 0) so.insert("experience", s.experience());
            stacks.append(so);
        }
        o.insert("stacks", stacks);
        armies.append(o);
    }
    root.insert("armies", armies);

    QJsonArray towns;
    for (const Town& t : m_towns) {
        QJsonObject o;
        o.insert("id", static_cast<int>(t.id.value()));
        o.insert("name", t.name);
        o.insert("race", ids.race(t.race));
        putOptionalId(o, "owner", t.owner.value());
        o.insert("x", t.pos.x);
        o.insert("y", t.pos.y);
        o.insert("buildings", ids.buildings(t.buildings));
        putOptionalId(o, "garrison", t.garrison.value());
        o.insert("lastBuildDay", t.lastBuildDay);
        if (t.lastHireWeek) o.insert("lastHireWeek", t.lastHireWeek);
        o.insert("recruitPool", ids.buildingCounts(t.recruitPool));
        o.insert("importedUnits", ids.unitCounts(t.importedUnits));
        {
            QJsonArray offers;
            for (const HeroTemplateId h : t.tavernOffers) offers.append(ids.heroTemplate(h));
            o.insert("tavernOffers", offers);
        }
        towns.append(o);
    }
    root.insert("towns", towns);

    QJsonArray dwellings;
    for (const MapDwelling& d : m_mapDwellings) {
        QJsonObject o;
        o.insert("id", static_cast<int>(d.id.value()));
        o.insert("unit", ids.unit(d.unitId));
        putOptionalId(o, "owner", d.owner.value());
        o.insert("x", d.pos.x);
        o.insert("y", d.pos.y);
        o.insert("available", d.available);
        dwellings.append(o);
    }
    root.insert("mapDwellings", dwellings);

    QJsonArray objects;
    for (const MapObject& m : m_mapObjects) {
        QJsonObject o;
        o.insert("id", static_cast<int>(m.id.value()));
        o.insert("kind", m.kind == MapObject::Kind::Artifact ? "artifact" : m.kind == MapObject::Kind::Site ? "site" : "treasure");
        o.insert("x", m.pos.x);
        o.insert("y", m.pos.y);
        if (!m.resources.isEmpty()) o.insert("resources", ids.amounts(m.resources));
        if (m.artifact.isValid()) o.insert("artifact", ids.artifact(m.artifact));
        if (!m.site.isEmpty()) o.insert("site", m.site);
        if (m.site == QLatin1String("quest_hut")) {
            const Quest& q = m.quest;
            QJsonObject qo;
            if (q.needArtifact.isValid()) qo.insert("needArtifact", ids.artifact(q.needArtifact));
            if (!q.needResources.isEmpty()) qo.insert("needResources", ids.amounts(q.needResources));
            if (q.needLevel > 0) qo.insert("needLevel", q.needLevel);
            if (!q.rewardResources.isEmpty()) qo.insert("rewardResources", ids.amounts(q.rewardResources));
            if (q.rewardArtifact.isValid()) qo.insert("rewardArtifact", ids.artifact(q.rewardArtifact));
            if (q.rewardExperience > 0) qo.insert("rewardExperience", q.rewardExperience);
            if (q.done) qo.insert("done", true);
            o.insert("quest", qo);
        }
        if (!m.visited.isEmpty()) {
            QJsonObject v;
            for (auto it = m.visited.begin(); it != m.visited.end(); ++it) v.insert(QString::number(it.key()), it.value());
            o.insert("visited", v);
        }
        objects.append(o);
    }
    root.insert("mapObjects", objects);

    QJsonArray mines;
    for (const Mine& m : m_mines) {
        QJsonObject o;
        o.insert("id", static_cast<int>(m.id.value()));
        o.insert("resource", ids.resource(m.resource));
        o.insert("dailyAmount", m.dailyAmount);
        putOptionalId(o, "owner", m.owner.value());
        o.insert("x", m.pos.x);
        o.insert("y", m.pos.y);
        mines.append(o);
    }
    root.insert("mines", mines);
    return root;
}

bool GameState::fromJson(const QJsonObject& json, const ContentRegistry& content, GameState* out,
                         QStringList* errors) {
    QStringList errs;
    GameState gs;
    QSet<quint32> usedIds;
    quint32 maxId = 0;

    auto note = [&](quint32 id) { maxId = std::max(maxId, id); };
    auto claim = [&](const QString& ctx, quint32 id) {
        note(id);
        if (usedIds.contains(id)) {
            errs << ctx + QStringLiteral(": id %1 is used twice").arg(id);
            return false;
        }
        usedIds.insert(id);
        return true;
    };
    // Calls fn(reader, context) for every object in `arr`.
    auto each = [&](const QJsonArray& arr, const QString& name, auto fn) {
        for (qsizetype i = 0; i < arr.size(); ++i) {
            const QString ctx = QStringLiteral("%1[%2]").arg(name).arg(i);
            if (!arr[i].isObject()) {
                errs << ctx + QStringLiteral(": entry must be an object");
                continue;
            }
            JsonReader r(arr[i].toObject(), ctx, &errs);
            fn(r, ctx);
        }
    };

    JsonReader top(json, QStringLiteral("save"), &errs);
    const int version = top.requireInt("formatVersion", 1, 1000000);
    if (top.ok() && version != kSaveFormatVersion) {
        const QString by = json.value("gameVersion").toString();
        top.addError(QStringLiteral("saved by another version of the game%1: save format %2, this version (%3) reads only format %4; "
                                    "load it with the game version that saved it")
                         .arg(by.isEmpty() ? QString() : QStringLiteral(" (%1)").arg(by)).arg(version).arg(kGameVersion).arg(kSaveFormatVersion));
        if (errors) *errors = errs;
        return false;
    }
    top.optionalString("gameVersion");
    top.optionalString("savedAt");
    // Content ids: the text keys (scenarios), or numbers that only mean the same with
    // the same game data (the checksum).
    const QString idStyle = top.optionalString("contentIds", QStringLiteral("numbers"));
    if (idStyle != QLatin1String("numbers") && idStyle != QLatin1String("text")) top.addError(QStringLiteral("contentIds must be numbers or text"));
    const Ids ids{content, idStyle == QLatin1String("text")};
    const QString checksum = top.optionalString("contentChecksum");
    if (!ids.text && checksum != content.checksum()) {
        top.addError(checksum.isEmpty() ? QStringLiteral("no contentChecksum (a save with numbers for content ids needs it)")
                                        : QStringLiteral("made with other game data (content checksum %1, this game's data %2): "
                                                         "load it with the game version and data that saved it").arg(checksum, content.checksum()));
        if (errors) *errors = errs;
        return false;
    }
    gs.m_day = top.requireInt("day", 1, 10000000);
    gs.m_difficulty = top.optionalString("difficulty", QStringLiteral("normal"));
    gs.m_fogOfWar = top.optionalBool("fogOfWar", true);
    gs.m_defeatRule = top.optionalString("defeat", QStringLiteral("week"));
    if (!QStringList{"week", "town", "all"}.contains(gs.m_defeatRule))
        top.addError(QStringLiteral("field 'defeat' must be week, town or all"));

    gs.m_hiddenMap = top.optionalBool("hiddenMap", true);
    gs.intro = top.optionalString("intro");
    gs.news = top.optionalStringList("news");
    if (top.has("events")) {
        const QJsonArray events = top.requireArray("events");
        for (qsizetype i = 0; i < events.size(); ++i) {
            JsonReader e(events[i].toObject(), QStringLiteral("events[%1]").arg(i), &errs);
            StoryEvent ev;
            ev.day = e.requireInt("day", 1, 100000);
            ev.text = e.requireString("text");
            ev.gift = ids.readAmounts(e, "gift", -100000, 100000000);
            if (e.has("monster")) {
                ev.monster = ids.readUnit(e, "monster");
                ev.monsterCount = e.requireInt("count", 1, 100000);
                ev.where = {e.requireInt("x", 0, 100000), e.requireInt("y", 0, 100000)};
            }
            e.finish();
            gs.storyEvents.append(ev);
        }
    }
    if (top.has("weekEvent")) {
        gs.m_weekEvent = ids.text ? content.weekEventId(top.requireId("weekEvent")) : WeekEventId(top.requireInt("weekEvent", 0, 32767));
        if (!content.weekEvent(gs.m_weekEvent)) errs << QStringLiteral("save: unknown week event");
    }
    if (top.has("history")) gs.m_history = top.requireArray("history");
    const QJsonObject victoryObj = top.has("victory") ? top.requireObject("victory") : QJsonObject();
    if (!victoryObj.isEmpty()) {
        JsonReader vr(victoryObj, QStringLiteral("save.victory"), &errs);
        const QString type = vr.requireString("type");
        gs.m_victory.town = TownId(static_cast<quint32>(vr.optionalInt("town", 0, 0, kMaxId)));
        gs.m_victory.amount = vr.optionalInt("amount", 0, 0, 1000000000);
        vr.finish();
        if (type == "town") gs.m_victory.type = VictoryCondition::Type::HoldTown;
        else if (type == "gold") gs.m_victory.type = VictoryCondition::Type::Gold;
        else if (type == "days") gs.m_victory.type = VictoryCondition::Type::Days;
        else if (type != "conquest") errs << QStringLiteral("save.victory: unknown type '%1'").arg(type);
    }
    const int nextId = top.requireInt("nextId", 1, kMaxId);
    const int currentIndex = top.optionalInt("currentIndex", 0, 0, kMaxId);
    const QJsonObject mapObj = top.requireObject("map");
    const QJsonObject rngObj = top.requireObject("rng");
    const QJsonObject exploredObj = top.requireObject("explored");
    const QJsonArray turnOrderA = top.requireArray("turnOrder");
    const QJsonArray playersA = top.requireArray("players");
    const QJsonArray heroesA = top.requireArray("heroes");
    const QJsonArray armiesA = top.requireArray("armies");
    const QJsonArray townsA = top.requireArray("towns");
    const QJsonArray minesA = top.requireArray("mines");
    const QJsonArray dwellingsA = top.requireArray("mapDwellings");
    const QJsonArray objectsA = top.has("mapObjects") ? top.requireArray("mapObjects") : QJsonArray();
    top.finish();
    if (!top.ok()) {
        if (errors) *errors = errs;
        return false;
    }

    // ---- map
    {
        JsonReader r(mapObj, QStringLiteral("map"), &errs);
        const int w = r.requireInt("width", 1, 4096);
        const int h = r.requireInt("height", 1, 4096);
        const QJsonArray paletteA = ids.text ? r.requireArray("terrainPalette") : QJsonArray();
        const QJsonArray tilesA = r.requireArray("tiles");
        r.finish();
        if (r.ok()) {
            // The terrain a tile number stands for: a palette entry, or the id itself.
            QVector<TerrainId> palette;
            bool good = true;
            if (ids.text) {
                for (const QJsonValue& v : paletteA) {
                    const TerrainId t = content.terrainId(v.toString());
                    if (!t.isValid()) {
                        errs << QStringLiteral("map: unknown terrain '%1' in terrainPalette").arg(v.toString());
                        good = false;
                    }
                    palette << t;
                }
            } else {
                for (const TerrainDef& t : content.terrain()) palette << t.id;
            }
            if (tilesA.size() != static_cast<qsizetype>(w) * h) {
                errs << QStringLiteral("map: tiles has %1 entries, expected width*height = %2")
                            .arg(tilesA.size()).arg(static_cast<qsizetype>(w) * h);
                good = false;
            }
            QVector<TerrainId> tiles;
            if (good) {
                tiles.reserve(tilesA.size());
                for (const QJsonValue& v : tilesA) {
                    const int i = v.toInt(-1);
                    if (!v.isDouble() || i < 0 || i >= palette.size()) {
                        errs << QStringLiteral("map: a tile refers to terrain %1, which does not exist").arg(v.toDouble());
                        good = false;
                        break;
                    }
                    tiles.append(palette[i]);
                }
            }
            if (good) gs.m_map = GameMap::fromCells(w, h, std::move(tiles));
        }
    }

    // ---- rng
    {
        JsonReader r(rngObj, QStringLiteral("rng"), &errs);
        const QString seedText = r.requireString("seed");
        const QString state = r.requireString("state");
        r.finish();
        if (r.ok()) {
            bool ok = false;
            const quint64 seed = seedText.toULongLong(&ok);
            if (!ok) {
                errs << QStringLiteral("rng: seed must be a number written as text");
            } else {
                gs.m_rng.reseed(seed);
                if (!gs.m_rng.restoreEngineState(state))
                    errs << QStringLiteral("rng: state is not valid");
            }
        }
    }

    const auto checkPos = [&](const QString& ctx, Pos p) {
        if (gs.m_map.width() > 0 && !gs.m_map.inBounds(p))
            errs << ctx + QStringLiteral(": position (%1, %2) is outside the %3x%4 map")
                              .arg(p.x).arg(p.y).arg(gs.m_map.width()).arg(gs.m_map.height());
    };

    // ---- players
    each(playersA, QStringLiteral("players"), [&](JsonReader& r, const QString& ctx) {
        Player p;
        p.id = PlayerId(static_cast<quint32>(r.requireInt("id", 1, kMaxId)));
        p.name = r.requireString("name");
        p.team = TeamId(static_cast<quint32>(r.requireInt("team", 1, kMaxId)));
        const QString controller = r.requireString("controller");
        if (controller == QLatin1String("human")) p.controller = Player::Controller::Human;
        else if (controller == QLatin1String("ai")) p.controller = Player::Controller::Ai;
        else if (!controller.isEmpty())
            r.addError(QStringLiteral("field 'controller' must be 'human' or 'ai'"));
        p.aiProfile = r.optionalString("aiProfile");
        p.incomePercent = r.optionalInt("incomePercent", 0, -100, 1000);
        p.growthPercent = r.optionalInt("growthPercent", 0, -100, 1000);
        p.townlessSince = r.optionalInt("townlessSince", 0, 0, 1000000);
        p.seatKeyHash = r.optionalString("seatKeyHash");
        p.race = ids.readRace(r, "race");
        p.color = r.requireColor("color");
        p.resources = ids.readAmounts(r, "resources", 0, 1000000000);
        r.finish();
        note(p.team.value());
        if (r.ok() && claim(ctx, p.id.value())) gs.m_players.insert(p.id, p);
    });

    // ---- heroes
    each(heroesA, QStringLiteral("heroes"), [&](JsonReader& r, const QString& ctx) {
        Hero h;
        h.id = HeroId(static_cast<quint32>(r.requireInt("id", 1, kMaxId)));
        h.name = r.requireString("name");
        h.classId = ids.readHeroClass(r, "class");
        h.owner = PlayerId(static_cast<quint32>(r.requireInt("owner", 1, kMaxId)));
        h.level = r.requireInt("level", 1, std::max(1, content.maxLevel()));
        h.experience = r.requireInt("experience", 0, 1000000000);
        h.traits = ids.readTraits(r, "traits");
        h.spellPoints = r.requireInt("spellPoints", 0, 1000000);
        h.pendingTraitPicks = r.requireInt("pendingTraitPicks", 0, 10000);
        h.movementPoints = r.requireInt("movementPoints", 0, 10000000);
        h.army = ArmyId(static_cast<quint32>(r.requireInt("army", 1, kMaxId)));
        if (r.has("template")) {
            const QJsonValue v = ids.text ? QJsonValue(r.requireId("template")) : QJsonValue(r.requireInt("template", 0, 32767));
            h.templateId = ids.readHeroTemplate(v);
            if (!h.templateId.isValid()) r.addError(QStringLiteral("unknown hero template"));
        }
        h.spells = ids.readSpells(r, "spells");
        h.artifacts = ids.readArtifacts(r, "artifacts");
        h.backpack = ids.readArtifacts(r, "backpack");
        h.inBoat = r.optionalBool("inBoat", false);
        if (r.has("destination")) {
            const QJsonArray d = r.requireArray("destination");
            if (d.size() == 2 && d[0].isDouble() && d[1].isDouble()) h.destination = Pos{d[0].toInt(), d[1].toInt()};
            else r.addError(QStringLiteral("destination must be [x, y]"));
        }
        r.finish();
        if (r.ok() && claim(ctx, h.id.value())) gs.m_heroes.insert(h.id, h);
    });

    // ---- armies
    each(armiesA, QStringLiteral("armies"), [&](JsonReader& r, const QString& ctx) {
        Army a;
        a.id = ArmyId(static_cast<quint32>(r.requireInt("id", 1, kMaxId)));
        a.owner = PlayerId(static_cast<quint32>(r.optionalInt("owner", 0, 0, kMaxId)));
        a.hero = HeroId(static_cast<quint32>(r.optionalInt("hero", 0, 0, kMaxId)));
        a.pos.x = r.requireInt("x", 0, 100000);
        a.pos.y = r.requireInt("y", 0, 100000);
        const QJsonArray stacksA = r.requireArray("stacks");
        r.finish();
        for (qsizetype i = 0; i < stacksA.size(); ++i) {
            const QString sctx = QStringLiteral("%1.stacks[%2]").arg(ctx).arg(i);
            if (!stacksA[i].isObject()) {
                errs << sctx + QStringLiteral(": entry must be an object");
                continue;
            }
            JsonReader sr(stacksA[i].toObject(), sctx, &errs);
            const UnitTypeId unit = ids.readUnit(sr, "unit");
            const int maxHp = sr.requireInt("maxHp", 1, 100000000);
            const int totalHp = sr.requireInt("totalHp", 1, 2000000000);
            const int experience = sr.optionalInt("experience", 0, 0, 100000000);
            sr.finish();
            if (sr.ok()) {
                UnitStack st = UnitStack::fromTotalHp(unit, totalHp, maxHp);
                st.setExperience(experience);
                a.stacks.append(st);
            }
        }
        if (a.stacks.size() > kMaxArmyStacks)
            r.addError(QStringLiteral("more than %1 stacks").arg(kMaxArmyStacks));
        checkPos(ctx, a.pos);
        if (r.ok() && claim(ctx, a.id.value())) gs.m_armies.insert(a.id, a);
    });

    // ---- towns
    each(townsA, QStringLiteral("towns"), [&](JsonReader& r, const QString& ctx) {
        Town t;
        t.id = TownId(static_cast<quint32>(r.requireInt("id", 1, kMaxId)));
        t.name = r.requireString("name");
        t.race = ids.readRace(r, "race");
        t.owner = PlayerId(static_cast<quint32>(r.optionalInt("owner", 0, 0, kMaxId)));
        t.pos.x = r.requireInt("x", 0, 100000);
        t.pos.y = r.requireInt("y", 0, 100000);
        t.buildings = ids.readBuildings(r, "buildings");
        t.garrison = ArmyId(static_cast<quint32>(r.optionalInt("garrison", 0, 0, kMaxId)));
        t.lastBuildDay = r.optionalInt("lastBuildDay", 0, 0, 10000000);
        t.lastHireWeek = r.optionalInt("lastHireWeek", 0, 0, 10000000);
        t.recruitPool = ids.readBuildingCounts(r, "recruitPool", 0, 1000000);
        t.importedUnits = ids.readUnitCounts(r, "importedUnits", 0, 1000000);
        if (r.has("tavernOffers"))
            for (const QJsonValue& v : r.requireArray("tavernOffers")) {
                const HeroTemplateId h = ids.readHeroTemplate(v);
                if (h.isValid()) t.tavernOffers << h;
                else r.addError(QStringLiteral("unknown hero template in tavernOffers"));
            }
        r.finish();
        checkPos(ctx, t.pos);
        if (r.ok() && claim(ctx, t.id.value())) gs.m_towns.insert(t.id, t);
    });

    // ---- map dwellings
    each(dwellingsA, QStringLiteral("mapDwellings"), [&](JsonReader& r, const QString& ctx) {
        MapDwelling d;
        d.id = MapDwellingId(static_cast<quint32>(r.requireInt("id", 1, kMaxId)));
        d.unitId = ids.readUnit(r, "unit");
        d.owner = PlayerId(static_cast<quint32>(r.optionalInt("owner", 0, 0, kMaxId)));
        d.pos.x = r.requireInt("x", 0, 100000);
        d.pos.y = r.requireInt("y", 0, 100000);
        d.available = r.requireInt("available", 0, 1000000);
        r.finish();
        checkPos(ctx, d.pos);
        if (r.ok() && claim(ctx, d.id.value())) gs.m_mapDwellings.insert(d.id, d);
    });

    // ---- map objects (treasures, artifacts)
    each(objectsA, QStringLiteral("mapObjects"), [&](JsonReader& r, const QString& ctx) {
        MapObject m;
        m.id = MapObjectId(static_cast<quint32>(r.requireInt("id", 1, kMaxId)));
        const QString kind = r.requireString("kind");
        m.pos.x = r.requireInt("x", 0, 100000);
        m.pos.y = r.requireInt("y", 0, 100000);
        m.resources = ids.readAmounts(r, "resources", 0, 100000000);
        if (r.has("artifact")) m.artifact = ids.readArtifactField(r, "artifact");
        m.site = r.optionalString("site");
        if (r.has("quest")) {
            JsonReader q(r.requireObject("quest"), ctx + QStringLiteral(" quest"), &errs);
            if (q.has("needArtifact")) m.quest.needArtifact = ids.readArtifactField(q, "needArtifact");
            m.quest.needResources = ids.readAmounts(q, "needResources", 0, 100000000);
            m.quest.needLevel = q.optionalInt("needLevel", 0, 0, 100);
            m.quest.rewardResources = ids.readAmounts(q, "rewardResources", 0, 100000000);
            if (q.has("rewardArtifact")) m.quest.rewardArtifact = ids.readArtifactField(q, "rewardArtifact");
            m.quest.rewardExperience = q.optionalInt("rewardExperience", 0, 0, 10000000);
            m.quest.done = q.optionalBool("done", false);
            q.finish();
        }
        const QMap<QString, int> visited = r.optionalIntMap("visited", 0, 10000000);
        for (auto it = visited.begin(); it != visited.end(); ++it) m.visited.insert(it.key().toInt(), it.value());
        r.finish();
        if (kind == "artifact") m.kind = MapObject::Kind::Artifact;
        else if (kind == "site") m.kind = MapObject::Kind::Site;
        else if (kind != "treasure") r.addError(QStringLiteral("kind must be treasure, artifact or site"));
        if (m.kind == MapObject::Kind::Site && !siteIds().contains(m.site)) r.addError(QStringLiteral("unknown site '%1'").arg(m.site));
        if (r.ok() && m.kind == MapObject::Kind::Artifact && !m.artifact.isValid()) r.addError(QStringLiteral("unknown artifact"));
        checkPos(ctx, m.pos);
        if (r.ok() && claim(ctx, m.id.value())) gs.m_mapObjects.insert(m.id, m);
    });

    // ---- mines
    each(minesA, QStringLiteral("mines"), [&](JsonReader& r, const QString& ctx) {
        Mine m;
        m.id = MineId(static_cast<quint32>(r.requireInt("id", 1, kMaxId)));
        m.resource = ids.readResource(r, "resource");
        m.dailyAmount = r.requireInt("dailyAmount", 1, 1000000);
        m.owner = PlayerId(static_cast<quint32>(r.optionalInt("owner", 0, 0, kMaxId)));
        m.pos.x = r.requireInt("x", 0, 100000);
        m.pos.y = r.requireInt("y", 0, 100000);
        r.finish();
        checkPos(ctx, m.pos);
        if (r.ok() && claim(ctx, m.id.value())) gs.m_mines.insert(m.id, m);
    });

    // ---- fog of war memory
    if (gs.m_map.width() > 0) {
        QSet<quint32> teams;
        for (const Player& p : gs.m_players) teams.insert(p.team.value());
        const qsizetype expected = static_cast<qsizetype>(gs.m_map.width()) * gs.m_map.height();
        for (auto it = exploredObj.begin(); it != exploredObj.end(); ++it) {
            bool ok = false;
            const quint32 teamId = it.key().toUInt(&ok);
            if (!ok || !teams.contains(teamId)) {
                errs << QStringLiteral("explored: '%1' is not a team in this game").arg(it.key());
                continue;
            }
            const QByteArray data = QByteArray::fromBase64(it.value().toString().toLatin1());
            if (!it.value().isString() || data.size() != expected) {
                errs << QStringLiteral("explored: team %1 needs %2 tiles of data")
                            .arg(teamId).arg(expected);
                continue;
            }
            gs.m_explored.insert(TeamId(teamId), data);
        }
    }

    // ---- turn order
    for (qsizetype i = 0; i < turnOrderA.size(); ++i) {
        const PlayerId id(static_cast<quint32>(turnOrderA[i].toInt(0)));
        if (!gs.m_players.contains(id))
            errs << QStringLiteral("turnOrder[%1]: unknown player %2").arg(i).arg(id.value());
        else
            gs.m_turnOrder.append(id);
    }
    if (currentIndex != 0 && currentIndex >= gs.m_turnOrder.size())
        errs << QStringLiteral("save: currentIndex %1 is outside turnOrder").arg(currentIndex);
    gs.m_currentIndex = currentIndex;

    // ---- cross references between objects
    const auto needPlayer = [&](const QString& what, quint32 id, bool optional) {
        if (id == 0 && optional) return;
        if (!gs.m_players.contains(PlayerId(id)))
            errs << QStringLiteral("%1: owner %2 is not a player").arg(what).arg(id);
    };
    for (const Hero& h : gs.m_heroes) {
        const QString what = QStringLiteral("hero %1 (%2)").arg(h.id.value()).arg(h.name);
        needPlayer(what, h.owner.value(), false);
        const Army* a = gs.army(h.army);
        if (!a) errs << what + QStringLiteral(": army %1 does not exist").arg(h.army.value());
        else if (a->hero != h.id)
            errs << what + QStringLiteral(": army %1 does not list this hero").arg(h.army.value());
    }
    for (const Army& a : gs.m_armies) {
        const QString what = QStringLiteral("army %1").arg(a.id.value());
        needPlayer(what, a.owner.value(), true);
        if (a.hero.isValid()) {
            const Hero* h = gs.hero(a.hero);
            if (!h) errs << what + QStringLiteral(": hero %1 does not exist").arg(a.hero.value());
            else if (h->army != a.id)
                errs << what + QStringLiteral(": hero %1 leads a different army").arg(a.hero.value());
        }
    }
    if (gs.m_victory.type == VictoryCondition::Type::HoldTown && !gs.m_towns.contains(gs.m_victory.town))
        errs << QStringLiteral("save.victory: town %1 does not exist").arg(gs.m_victory.town.value());
    for (const Town& t : gs.m_towns) {
        const QString what = QStringLiteral("town %1 (%2)").arg(t.id.value()).arg(t.name);
        needPlayer(what, t.owner.value(), true);
        if (t.garrison.isValid() && !gs.army(t.garrison))
            errs << what + QStringLiteral(": garrison army %1 does not exist").arg(t.garrison.value());
    }
    for (const MapDwelling& d : gs.m_mapDwellings)
        needPlayer(QStringLiteral("dwelling %1").arg(d.id.value()), d.owner.value(), true);
    for (const Mine& m : gs.m_mines)
        needPlayer(QStringLiteral("mine %1").arg(m.id.value()), m.owner.value(), true);

    if (!errs.isEmpty()) {
        if (errors) *errors = errs;
        return false;
    }
    gs.m_nextId = std::max<quint32>(static_cast<quint32>(nextId), maxId + 1);
    *out = std::move(gs);
    if (errors) errors->clear();
    return true;
}


SaveInfo inspectSave(const QString& path, const ContentRegistry& content) {
    SaveInfo info;
    const QFileInfo fi(path);
    info.name = fi.completeBaseName();
    info.savedAt = fi.lastModified().toString(Qt::ISODate);
    QFile file(path);
    QJsonParseError pe;
    const QJsonDocument doc = file.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(file.readAll(), &pe) : QJsonDocument();
    if (!doc.isObject()) {
        info.status = SaveInfo::Status::Incompatible;
        info.problems << QStringLiteral("not a saved game");
        return info;
    }
    const QJsonObject o = doc.object();
    info.formatVersion = o.value("formatVersion").toInt();
    info.gameVersion = o.value("gameVersion").toString();
    if (o.contains("savedAt")) info.savedAt = o.value("savedAt").toString();
    info.day = o.value("day").toInt();
    for (const QJsonValue& p : o.value("players").toArray()) info.players << p.toObject().value("name").toString();
    const QJsonObject map = o.value("map").toObject();
    info.mapWidth = map.value("width").toInt();
    info.mapHeight = map.value("height").toInt();
    GameState gs;
    QStringList errors;
    if (!GameState::fromJson(o, content, &gs, &errors)) {
        info.status = SaveInfo::Status::Incompatible;
        info.problems = errors.mid(0, 5);
        if (errors.size() > 5) info.problems << QStringLiteral("... and %1 more problems").arg(errors.size() - 5);
    }
    return info;
}

QByteArray GameState::saveBytes(const ContentRegistry& content) const {
    QJsonObject root = toJson(content);
    root.insert("gameVersion", QString::fromLatin1(kGameVersion));
    root.insert("savedAt", QDateTime::currentDateTime().toString(Qt::ISODate));
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

bool GameState::saveToFile(const QString& path, const ContentRegistry& content, QString* error) const {
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) *error = file.errorString();
        return false;
    }
    file.write(saveBytes(content));
    if (!file.commit()) {
        if (error) *error = file.errorString();
        return false;
    }
    return true;
}

bool GameState::loadFromFile(const QString& path, const ContentRegistry& content, GameState* out,
                             QStringList* errors) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (errors) *errors = {QStringLiteral("%1: cannot open (%2)").arg(path, file.errorString())};
        return false;
    }
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
        if (errors)
            *errors = {QStringLiteral("%1: not a valid save file (%2)").arg(path, pe.errorString())};
        return false;
    }
    return fromJson(doc.object(), content, out, errors);
}

}  // namespace kot::core
