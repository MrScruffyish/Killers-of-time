#include "core/content/ContentRegistry.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <cmath>

#include "core/data/JsonReader.h"

namespace kot::core {

QString traitGroupName(TraitGroup group) {
    switch (group) {
        case TraitGroup::Battle: return QStringLiteral("battle");
        case TraitGroup::Magic: return QStringLiteral("magic");
        case TraitGroup::Explore: return QStringLiteral("explore");
    }
    return {};
}

namespace {

bool readObjectFile(const QString& path, const QString& display, QJsonObject* out,
                    QStringList& errors) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        errors << display + QStringLiteral(": cannot open file (%1)").arg(file.errorString());
        return false;
    }
    const QByteArray data = file.readAll();
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        int line = 1;
        for (int i = 0; i < parseError.offset && i < data.size(); ++i) {
            if (data[i] == '\n') ++line;
        }
        errors << display + QStringLiteral(": invalid JSON near line %1: %2")
                                .arg(line)
                                .arg(parseError.errorString());
        return false;
    }
    if (!doc.isObject()) {
        errors << display + QStringLiteral(": top level must be a JSON object");
        return false;
    }
    *out = doc.object();
    return true;
}

// Reads <dir>/<file>, whose top level has an array under `key`, and calls
// fn(entryObject, context) for each entry.
template <typename Fn>
void forEachEntry(const QString& dir, const QString& file, const QString& key,
                  QStringList& errors, Fn fn) {
    QJsonObject root;
    if (!readObjectFile(QDir(dir).filePath(file), file, &root, errors)) return;
    if (!root.value(key).isArray()) {
        errors << file + QStringLiteral(": top-level field '%1' must be a list").arg(key);
        return;
    }
    const QJsonArray entries = root.value(key).toArray();
    for (qsizetype i = 0; i < entries.size(); ++i) {
        const QString context = QStringLiteral("%1[%2]").arg(file).arg(i);
        if (!entries[i].isObject()) {
            errors << context + QStringLiteral(": entry must be an object");
            continue;
        }
        fn(entries[i].toObject(), context);
    }
}

}  // namespace

void ContentRegistry::clear() {
    m_resources.clear();
    m_terrain.clear();
    m_traits.clear();
    m_units.clear();
    m_heroClasses.clear();
    m_buildings.clear();
    m_heroTemplates.clear();
    m_xpThresholds.clear();
    m_balance = Balance();
}

bool ContentRegistry::loadFromDirectory(const QString& dataDir, QStringList* errors) {
    clear();
    QStringList problems;
    loadResources(dataDir, problems);
    loadTerrain(dataDir, problems);
    loadTraits(dataDir, problems);
    loadUnits(dataDir, problems);
    loadBuildings(dataDir, problems);
    loadHeroClasses(dataDir, problems);
    loadHeroTemplates(dataDir, problems);
    loadProgression(dataDir, problems);
    loadBalance(dataDir, problems);
    if (problems.isEmpty()) validateReferences(problems);
    if (!problems.isEmpty()) {
        clear();
        if (errors) *errors = problems;
        return false;
    }
    if (errors) errors->clear();
    return true;
}

void ContentRegistry::loadResources(const QString& dir, QStringList& errors) {
    forEachEntry(dir, QStringLiteral("resources.json"), QStringLiteral("resources"), errors,
                 [&](const QJsonObject& o, const QString& ctx) {
                     JsonReader r(o, ctx, &errors);
                     ResourceDef d;
                     d.id = r.requireId(QStringLiteral("id"));
                     d.name = r.requireString(QStringLiteral("name"));
                     d.isCurrency = r.optionalBool(QStringLiteral("currency"), false);
                     d.value = r.optionalInt(QStringLiteral("value"), d.isCurrency ? 1 : 100, 1, 1000000);
                     r.finish();
                     if (!r.ok()) return;
                     if (m_resources.contains(d.id)) {
                         errors << ctx + QStringLiteral(": duplicate resource id '%1'").arg(d.id);
                         return;
                     }
                     m_resources.insert(d.id, d);
                 });
}

void ContentRegistry::loadTerrain(const QString& dir, QStringList& errors) {
    forEachEntry(dir, QStringLiteral("terrain.json"), QStringLiteral("terrain"), errors,
                 [&](const QJsonObject& o, const QString& ctx) {
                     JsonReader r(o, ctx, &errors);
                     TerrainDef d;
                     d.id = r.requireId(QStringLiteral("id"));
                     d.name = r.requireString(QStringLiteral("name"));
                     d.walkable = r.optionalBool(QStringLiteral("walkable"), true);
                     d.sailable = r.optionalBool(QStringLiteral("sailable"), false);
                     d.moveCost = r.optionalInt(QStringLiteral("moveCost"), 100, 1, 10000);
                     d.blocksVision = r.optionalBool(QStringLiteral("blocksVision"), false);
                     d.color = r.requireColor(QStringLiteral("color"));
                     r.finish();
                     if (!r.ok()) return;
                     if (m_terrain.contains(d.id)) {
                         errors << ctx + QStringLiteral(": duplicate terrain id '%1'").arg(d.id);
                         return;
                     }
                     m_terrain.insert(d.id, d);
                 });
}

void ContentRegistry::loadTraits(const QString& dir, QStringList& errors) {
    forEachEntry(dir, QStringLiteral("traits.json"), QStringLiteral("traits"), errors,
                 [&](const QJsonObject& o, const QString& ctx) {
                     JsonReader r(o, ctx, &errors);
                     TraitDef d;
                     d.id = r.requireId(QStringLiteral("id"));
                     d.name = r.requireString(QStringLiteral("name"));
                     const QString group = r.requireString(QStringLiteral("group"));
                     if (group == QLatin1String("battle")) d.group = TraitGroup::Battle;
                     else if (group == QLatin1String("magic")) d.group = TraitGroup::Magic;
                     else if (group == QLatin1String("explore")) d.group = TraitGroup::Explore;
                     else if (!group.isEmpty())
                         r.addError(QStringLiteral("field 'group' must be one of battle, magic, "
                                                   "explore (got '%1')").arg(group));
                     d.pickable = r.requireBool(QStringLiteral("pickable"));
                     d.description = r.optionalString(QStringLiteral("description"));
                     r.finish();
                     if (!r.ok()) return;
                     if (m_traits.contains(d.id)) {
                         errors << ctx + QStringLiteral(": duplicate trait id '%1'").arg(d.id);
                         return;
                     }
                     m_traits.insert(d.id, d);
                 });
}

void ContentRegistry::loadUnits(const QString& dir, QStringList& errors) {
    const QDir unitsDir(QDir(dir).filePath(QStringLiteral("units")));
    if (!unitsDir.exists()) {
        errors << QStringLiteral("units: directory not found");
        return;
    }
    const QFileInfoList files =
        unitsDir.entryInfoList({QStringLiteral("*.json")}, QDir::Files, QDir::Name);
    for (const QFileInfo& info : files) {
        const QString ctx = QStringLiteral("units/") + info.fileName();
        QJsonObject o;
        if (!readObjectFile(info.filePath(), ctx, &o, errors)) continue;

        JsonReader r(o, ctx, &errors);
        UnitDef d;
        d.id = r.requireId(QStringLiteral("id"));
        d.name = r.requireString(QStringLiteral("name"));
        d.race = r.requireId(QStringLiteral("race"));
        d.tier = r.requireInt(QStringLiteral("tier"), 1, 7);
        d.attack = r.requireInt(QStringLiteral("attack"), 0, 1000);
        d.defense = r.requireInt(QStringLiteral("defense"), 0, 1000);
        d.damageMin = r.requireInt(QStringLiteral("damageMin"), 0, 100000);
        d.damageMax = r.requireInt(QStringLiteral("damageMax"), 0, 100000);
        d.maxHp = r.requireInt(QStringLiteral("maxHp"), 1, 100000);
        d.battleSpeed = r.requireInt(QStringLiteral("battleSpeed"), 1, 100);
        d.mapSpeed = r.optionalInt(QStringLiteral("mapSpeed"), d.battleSpeed, 1, 100);
        d.initiative = r.optionalInt(QStringLiteral("initiative"), d.battleSpeed, 1, 100);
        d.range = r.optionalInt(QStringLiteral("range"), 0, 0, 100);
        d.shots = r.optionalInt(QStringLiteral("shots"), 0, 0, 1000);
        d.size = r.optionalInt(QStringLiteral("size"), 1, 1, 2);
        d.weeklyGrowth = r.optionalInt(QStringLiteral("weeklyGrowth"), 0, 0, 10000);
        d.spellPoints = r.optionalInt(QStringLiteral("spellPoints"), 0, 0, 10000);
        d.cost = r.optionalIntMap(QStringLiteral("cost"), 1, 1000000);
        d.abilities = r.optionalStringList(QStringLiteral("abilities"));
        d.upgradesTo = r.optionalString(QStringLiteral("upgradesTo"));
        r.finish();
        if (!r.ok()) continue;

        if (d.damageMin > d.damageMax)
            r.addError(QStringLiteral("damageMin (%1) is greater than damageMax (%2)")
                           .arg(d.damageMin).arg(d.damageMax));
        if (d.range > 0 && d.shots < 1)
            r.addError(QStringLiteral("a ranged unit (range > 0) needs shots >= 1"));
        if (d.range == 0 && d.shots > 0)
            r.addError(QStringLiteral("shots > 0 but range is 0 (melee units have no shots)"));
        if (d.id != info.completeBaseName())
            r.addError(QStringLiteral("id '%1' must match the file name").arg(d.id));
        if (!r.ok()) continue;

        if (m_units.contains(d.id)) {
            errors << ctx + QStringLiteral(": duplicate unit id '%1'").arg(d.id);
            continue;
        }
        m_units.insert(d.id, d);
    }
}

void ContentRegistry::loadBuildings(const QString& dir, QStringList& errors) {
    const QDir buildingsDir(QDir(dir).filePath(QStringLiteral("buildings")));
    if (!buildingsDir.exists()) {
        errors << QStringLiteral("buildings: directory not found");
        return;
    }
    const QFileInfoList files =
        buildingsDir.entryInfoList({QStringLiteral("*.json")}, QDir::Files, QDir::Name);
    for (const QFileInfo& info : files) {
        const QString ctx = QStringLiteral("buildings/") + info.fileName();
        QJsonObject o;
        if (!readObjectFile(info.filePath(), ctx, &o, errors)) continue;

        JsonReader r(o, ctx, &errors);
        BuildingDef d;
        d.id = r.requireId(QStringLiteral("id"));
        d.name = r.requireString(QStringLiteral("name"));
        d.race = r.requireId(QStringLiteral("race"));
        d.cost = r.optionalIntMap(QStringLiteral("cost"), 1, 1000000);
        d.requiredBuildings = r.optionalStringList(QStringLiteral("requires"));
        d.excludes = r.optionalStringList(QStringLiteral("excludes"));
        d.upgradeOf = r.optionalString(QStringLiteral("upgradeOf"));
        const QJsonArray effects =
            r.has(QStringLiteral("effects")) ? r.requireArray(QStringLiteral("effects")) : QJsonArray();
        r.finish();
        for (qsizetype i = 0; i < effects.size(); ++i) {
            const QString ectx = QStringLiteral("%1: effects[%2]").arg(ctx).arg(i);
            if (!effects[i].isObject()) {
                errors << ectx + QStringLiteral(": entry must be an object");
                continue;
            }
            JsonReader er(effects[i].toObject(), ectx, &errors);
            BuildingEffect e;
            const QString type = er.requireString(QStringLiteral("type"));
            if (type == QLatin1String("dwelling")) {
                e.type = BuildingEffect::Type::Dwelling;
                e.unit = er.requireId(QStringLiteral("unit"));
            } else if (type == QLatin1String("tavern")) {
                e.type = BuildingEffect::Type::Tavern;
            } else if (type == QLatin1String("income")) {
                e.type = BuildingEffect::Type::Income;
                e.resource = er.requireId(QStringLiteral("resource"));
                e.amount = er.requireInt(QStringLiteral("amount"), 1, 1000000);
            } else if (!type.isEmpty()) {
                er.addError(QStringLiteral("unknown effect type '%1' (use dwelling, income or tavern)").arg(type));
            }
            er.finish();
            if (er.ok()) d.effects.append(e);
            else r.addError(QStringLiteral("has an invalid effect"));
        }
        if (d.id != info.completeBaseName())
            r.addError(QStringLiteral("id '%1' must match the file name").arg(d.id));
        if (!r.ok()) continue;
        if (m_buildings.contains(d.id)) {
            errors << ctx + QStringLiteral(": duplicate building id '%1'").arg(d.id);
            continue;
        }
        m_buildings.insert(d.id, d);
    }
}

void ContentRegistry::loadHeroTemplates(const QString& dir, QStringList& errors) {
    forEachEntry(dir, QStringLiteral("heroes.json"), QStringLiteral("heroes"), errors,
                 [&](const QJsonObject& o, const QString& ctx) {
                     JsonReader r(o, ctx, &errors);
                     HeroTemplateDef d;
                     d.id = r.requireId(QStringLiteral("id"));
                     d.name = r.requireString(QStringLiteral("name"));
                     d.classId = r.requireId(QStringLiteral("class"));
                     r.finish();
                     if (!r.ok()) return;
                     if (m_heroTemplates.contains(d.id)) {
                         errors << ctx + QStringLiteral(": duplicate hero id '%1'").arg(d.id);
                         return;
                     }
                     m_heroTemplates.insert(d.id, d);
                 });
}

void ContentRegistry::loadHeroClasses(const QString& dir, QStringList& errors) {
    forEachEntry(dir, QStringLiteral("hero_classes.json"), QStringLiteral("heroClasses"), errors,
                 [&](const QJsonObject& o, const QString& ctx) {
                     JsonReader r(o, ctx, &errors);
                     HeroClassDef d;
                     d.id = r.requireId(QStringLiteral("id"));
                     d.name = r.requireString(QStringLiteral("name"));
                     d.race = r.requireId(QStringLiteral("race"));
                     d.baseTraits = r.optionalIntMap(QStringLiteral("baseTraits"), 0, 1000);
                     d.pickIncrement = r.optionalIntMap(QStringLiteral("pickIncrement"), 1, 100);
                     d.autoGrowth = r.optionalIntMap(QStringLiteral("autoGrowth"), 1, 1000);
                     r.finish();
                     if (!r.ok()) return;
                     if (m_heroClasses.contains(d.id)) {
                         errors << ctx + QStringLiteral(": duplicate hero class id '%1'").arg(d.id);
                         return;
                     }
                     m_heroClasses.insert(d.id, d);
                 });
}

void ContentRegistry::loadProgression(const QString& dir, QStringList& errors) {
    const QString file = QStringLiteral("progression.json");
    QJsonObject root;
    if (!readObjectFile(QDir(dir).filePath(file), file, &root, errors)) return;
    if (!root.value(QStringLiteral("xpThresholds")).isArray()) {
        errors << file + QStringLiteral(": top-level field 'xpThresholds' must be a list");
        return;
    }
    const QJsonArray arr = root.value(QStringLiteral("xpThresholds")).toArray();
    int previous = 0;
    for (qsizetype i = 0; i < arr.size(); ++i) {
        const double d = arr[i].toDouble(-1);
        if (!arr[i].isDouble() || d != std::floor(d) || d <= previous || d > 100000000) {
            errors << file + QStringLiteral(": xpThresholds[%1] must be a whole number greater "
                                            "than the previous one (%2)").arg(i).arg(previous);
            m_xpThresholds.clear();
            return;
        }
        previous = static_cast<int>(d);
        m_xpThresholds.append(previous);
    }
    if (m_xpThresholds.isEmpty())
        errors << file + QStringLiteral(": xpThresholds must not be empty");
}

void ContentRegistry::loadBalance(const QString& dir, QStringList& errors) {
    const QString file = QStringLiteral("balance.json");
    QJsonObject root;
    if (!readObjectFile(QDir(dir).filePath(file), file, &root, errors)) return;
    JsonReader top(root, file, &errors);
    const QJsonObject movement = top.requireObject(QStringLiteral("movement"));
    const QJsonObject vision = top.requireObject(QStringLiteral("vision"));
    const QJsonObject economy = top.requireObject(QStringLiteral("economy"));
    top.finish();
    if (!top.ok()) return;

    {
        JsonReader vr(vision, file + QStringLiteral("/vision"), &errors);
        VisionBalance vb;
        vb.townRadius = vr.requireInt(QStringLiteral("townRadius"), 1, 100);
        vb.mineRadius = vr.requireInt(QStringLiteral("mineRadius"), 1, 100);
        vr.finish();
        JsonReader er(economy, file + QStringLiteral("/economy"), &errors);
        EconomyBalance eb;
        eb.townDailyGold = er.requireInt(QStringLiteral("townDailyGold"), 0, 1000000);
        eb.tradeEfficiencyPercent = er.optionalInt(QStringLiteral("tradeEfficiencyPercent"), 80, 1, 100);
        eb.heroCost = er.optionalInt(QStringLiteral("heroCost"), 2500, 0, 10000000);
        eb.tavernOffers = er.optionalInt(QStringLiteral("tavernOffers"), 2, 1, 10);
        eb.maxHeroesPerPlayer = er.optionalInt(QStringLiteral("maxHeroesPerPlayer"), 8, 1, 100);
        er.finish();
        if (vr.ok() && er.ok()) {
            m_balance.vision = vb;
            m_balance.economy = eb;
        }
    }

    JsonReader r(movement, file + QStringLiteral("/movement"), &errors);
    MovementBalance mb;
    mb.base = r.requireInt(QStringLiteral("base"), 100, 1000000);
    mb.perTraitLevel = r.requireInt(QStringLiteral("perTraitLevel"), 0, 100000);
    mb.minimum = r.requireInt(QStringLiteral("minimum"), 1, 1000000);
    const QJsonArray steps = r.requireArray(QStringLiteral("slowPenalties"));
    r.finish();
    int previous = 0;
    for (qsizetype i = 0; i < steps.size(); ++i) {
        const QString ctx = QStringLiteral("%1/movement/slowPenalties[%2]").arg(file).arg(i);
        if (!steps[i].isObject()) {
            errors << ctx + QStringLiteral(": entry must be an object");
            continue;
        }
        JsonReader sr(steps[i].toObject(), ctx, &errors);
        SlowPenaltyStep step;
        step.mapSpeedAtMost = sr.requireInt(QStringLiteral("mapSpeedAtMost"), 1, 100);
        step.penalty = sr.requireInt(QStringLiteral("penalty"), 0, 100000);
        sr.finish();
        if (!sr.ok()) continue;
        if (step.mapSpeedAtMost <= previous) {
            sr.addError(QStringLiteral("mapSpeedAtMost must be greater than the previous "
                                       "entry (%1)").arg(previous));
            continue;
        }
        previous = step.mapSpeedAtMost;
        mb.slowPenalties.append(step);
    }
    if (r.ok()) m_balance.movement = mb;
}

void ContentRegistry::validateReferences(QStringList& errors) const {
    for (const HeroTemplateDef& h : m_heroTemplates) {
        if (!m_heroClasses.contains(h.classId))
            errors << QStringLiteral("hero '%1': unknown class '%2'").arg(h.id, h.classId);
    }
    for (const HeroClassDef& c : m_heroClasses) {
        auto check = [&](const QMap<QString, int>& m, const char* what, bool needPickable) {
            for (auto it = m.begin(); it != m.end(); ++it) {
                const TraitDef* t = trait(it.key());
                if (!t)
                    errors << QStringLiteral("hero class '%1': %2 uses unknown trait '%3'")
                                  .arg(c.id, QString::fromLatin1(what), it.key());
                else if (needPickable && !t->pickable)
                    errors << QStringLiteral("hero class '%1': %2 lists trait '%3' which is not "
                                             "pickable").arg(c.id, QString::fromLatin1(what), it.key());
            }
        };
        check(c.baseTraits, "baseTraits", false);
        check(c.pickIncrement, "pickIncrement", true);
        check(c.autoGrowth, "autoGrowth", false);
    }

    int currencies = 0;
    for (const ResourceDef& r : m_resources) currencies += r.isCurrency ? 1 : 0;
    for (const ResourceDef& r : m_resources) {
        if (r.isCurrency && r.value != 1)
            errors << QStringLiteral("resource '%1': the currency must have value 1").arg(r.id);
    }
    if (currencies != 1)
        errors << QStringLiteral("resources.json: exactly one resource must have "
                                 "\"currency\": true (found %1)").arg(currencies);

    for (const BuildingDef& b : m_buildings) {
        const QString what = QStringLiteral("building '%1'").arg(b.id);
        for (auto it = b.cost.begin(); it != b.cost.end(); ++it) {
            if (!m_resources.contains(it.key()))
                errors << what + QStringLiteral(": cost uses unknown resource '%1'").arg(it.key());
        }
        for (const QString& other : b.requiredBuildings + b.excludes) {
            if (other == b.id)
                errors << what + QStringLiteral(": refers to itself in requires/excludes");
            else if (!m_buildings.contains(other))
                errors << what + QStringLiteral(": refers to unknown building '%1'").arg(other);
        }
        for (const QString& other : b.requiredBuildings) {
            if (b.excludes.contains(other))
                errors << what + QStringLiteral(": '%1' is both required and excluded").arg(other);
        }
        for (const BuildingEffect& e : b.effects) {
            if (e.type == BuildingEffect::Type::Dwelling && !m_units.contains(e.unit))
                errors << what + QStringLiteral(": dwelling of unknown unit '%1'").arg(e.unit);
            if (e.type == BuildingEffect::Type::Income && !m_resources.contains(e.resource))
                errors << what + QStringLiteral(": income of unknown resource '%1'").arg(e.resource);
        }
        if (!b.upgradeOf.isEmpty()) {
            const auto base = m_buildings.constFind(b.upgradeOf);
            if (base == m_buildings.constEnd()) {
                errors << what + QStringLiteral(": upgradeOf unknown building '%1'").arg(b.upgradeOf);
            } else {
                // The upgraded dwelling must produce the upgrade of the base dwelling's unit.
                QString baseUnit, newUnit;
                for (const BuildingEffect& e : base->effects)
                    if (e.type == BuildingEffect::Type::Dwelling) baseUnit = e.unit;
                for (const BuildingEffect& e : b.effects)
                    if (e.type == BuildingEffect::Type::Dwelling) newUnit = e.unit;
                const auto bu = m_units.constFind(baseUnit);
                if (baseUnit.isEmpty() || newUnit.isEmpty() || bu == m_units.constEnd() ||
                    bu->upgradesTo != newUnit)
                    errors << what + QStringLiteral(": upgradeOf '%1' needs dwellings where the "
                                                    "unit of '%1' upgrades to this unit").arg(b.upgradeOf);
            }
        }
    }
    for (const UnitDef& u : m_units) {
        for (auto it = u.cost.begin(); it != u.cost.end(); ++it) {
            if (!m_resources.contains(it.key()))
                errors << QStringLiteral("unit '%1': cost uses unknown resource '%2'")
                              .arg(u.id, it.key());
        }
        if (!u.upgradesTo.isEmpty()) {
            if (u.upgradesTo == u.id)
                errors << QStringLiteral("unit '%1': cannot upgrade to itself").arg(u.id);
            else if (!m_units.contains(u.upgradesTo))
                errors << QStringLiteral("unit '%1': upgradesTo unknown unit '%2'")
                              .arg(u.id, u.upgradesTo);
        }
    }
}

const ResourceDef* ContentRegistry::resource(const QString& id) const {
    const auto it = m_resources.constFind(id);
    return it == m_resources.constEnd() ? nullptr : &it.value();
}
const TerrainDef* ContentRegistry::terrainType(const QString& id) const {
    const auto it = m_terrain.constFind(id);
    return it == m_terrain.constEnd() ? nullptr : &it.value();
}
const TraitDef* ContentRegistry::trait(const QString& id) const {
    const auto it = m_traits.constFind(id);
    return it == m_traits.constEnd() ? nullptr : &it.value();
}
const UnitDef* ContentRegistry::unit(const QString& id) const {
    const auto it = m_units.constFind(id);
    return it == m_units.constEnd() ? nullptr : &it.value();
}
const HeroClassDef* ContentRegistry::heroClass(const QString& id) const {
    const auto it = m_heroClasses.constFind(id);
    return it == m_heroClasses.constEnd() ? nullptr : &it.value();
}
const HeroTemplateDef* ContentRegistry::heroTemplate(const QString& id) const {
    const auto it = m_heroTemplates.constFind(id);
    return it == m_heroTemplates.constEnd() ? nullptr : &it.value();
}
const BuildingDef* ContentRegistry::building(const QString& id) const {
    const auto it = m_buildings.constFind(id);
    return it == m_buildings.constEnd() ? nullptr : &it.value();
}
const ResourceDef* ContentRegistry::currency() const {
    for (const ResourceDef& r : m_resources) {
        if (r.isCurrency) return &r;
    }
    return nullptr;
}

}  // namespace kot::core
