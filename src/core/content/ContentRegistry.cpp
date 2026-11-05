#include "core/content/ContentRegistry.h"

#include <QCryptographicHash>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <cmath>

#include "core/content/ContentChecks.h"
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
    m_checksum.clear();
    m_resources.clear();
    m_currency = ResourceId();
    m_terrain.clear();
    m_traits.clear();
    m_known = KnownTraits();
    m_units.clear();
    m_unitIndex.clear();
    m_heroClasses.clear();
    m_buildings.clear();
    m_buildingIndex.clear();
    m_heroTemplates.clear();
    m_spells.clear();
    m_spellIndex.clear();
    m_races.clear();
    m_raceIndex.clear();
    m_artifacts.clear();
    m_weekEvents.clear();
    m_xpThresholds.clear();
    m_balance = Balance();
}

bool ContentRegistry::loadFromDirectory(const QString& dataDir, QStringList* errors) {
    clear();
    QStringList problems;
    // Spells are named by units (read before them): their ids from the file names first.
    for (const QFileInfo& info : QDir(QDir(dataDir).filePath(QStringLiteral("spells"))).entryInfoList({QStringLiteral("*.json")}, QDir::Files, QDir::Name))
        m_spellIndex.insert(info.completeBaseName(), static_cast<int>(m_spellIndex.size()));
    m_spells.resize(m_spellIndex.size());
    // Races too (units, buildings and hero classes name them).
    for (const QFileInfo& info : QDir(QDir(dataDir).filePath(QStringLiteral("races"))).entryInfoList({QStringLiteral("*.json")}, QDir::Files, QDir::Name))
        m_raceIndex.insert(info.completeBaseName(), static_cast<int>(m_raceIndex.size()));
    m_races.resize(m_raceIndex.size());
    loadResources(dataDir, problems);
    loadTerrain(dataDir, problems);
    loadTraits(dataDir, problems);
    loadUnits(dataDir, problems);
    loadBuildings(dataDir, problems);
    loadRaces(dataDir, problems);
    loadArtifacts(dataDir, problems);
    loadWeekEvents(dataDir, problems);
    loadSpells(dataDir, problems);
    loadHeroClasses(dataDir, problems);
    loadHeroTemplates(dataDir, problems);
    loadProgression(dataDir, problems);
    loadBalance(dataDir, problems);
    if (problems.isEmpty()) validateReferences(problems);
    if (problems.isEmpty()) problems << checkContentGraphs(*this);  // loops, unbuildable buildings
    if (problems.isEmpty()) {
        // Ranks: regular, then one more per upgrade step (no loops: checked above).
        for (UnitDef& u : m_units) u.rank = 0;
        for (const UnitDef& u : m_units) {
            int rank = 0;
            for (UnitTypeId next = u.upgradesTo; next.isValid() && rank < 10; next = m_units[next.index()].upgradesTo) {
                ++rank;
                UnitDef& n = m_units[next.index()];
                n.rank = std::max(n.rank, rank);
            }
        }
    }
    if (!problems.isEmpty()) {
        clear();
        if (errors) *errors = problems;
        return false;
    }
    if (errors) errors->clear();
    m_dataDir = dataDir;
    {
        // Every kind's keys in id order (all kinds, also those still keyed by text).
        QCryptographicHash hash(QCryptographicHash::Sha1);
        const auto add = [&](const char* kind, const QStringList& keys) {
            hash.addData(QByteArray(kind) + ':' + keys.join(',').toUtf8() + ';');
        };
        QStringList keys;
        for (const ResourceDef& r : m_resources) keys << r.key;
        add("resources", keys);
        keys.clear();
        for (const TerrainDef& t : m_terrain) keys << t.key;
        add("terrain", keys);
        keys.clear();
        for (const TraitDef& t : m_traits) keys << t.key;
        add("traits", keys);
        keys.clear();
        for (const UnitDef& u : m_units) keys << u.key;
        add("units", keys);
        keys.clear();
        for (const BuildingDef& b : m_buildings) keys << b.key;
        add("buildings", keys);
        keys.clear();
        for (const HeroClassDef& c : m_heroClasses) keys << c.key;
        add("heroClasses", keys);
        keys.clear();
        for (const HeroTemplateDef& h : m_heroTemplates) keys << h.key;
        add("heroTemplates", keys);
        keys.clear();
        for (const SpellDef& sp : m_spells) keys << sp.key;
        add("spells", keys);
        keys.clear();
        for (const RaceDef& rc : m_races) keys << rc.key;
        add("races", keys);
        keys.clear();
        for (const ArtifactDef& a : m_artifacts) keys << a.key;
        add("artifacts", keys);
        keys.clear();
        for (const WeekEventDef& e : m_weekEvents) keys << e.key;
        add("weekEvents", keys);
        m_checksum = QString::fromLatin1(hash.result().toHex().left(16));
    }
    return true;
}

void ContentRegistry::loadResources(const QString& dir, QStringList& errors) {
    QMap<QString, ResourceDef> byKey;  // sorted by key: the ids
    forEachEntry(dir, QStringLiteral("resources.json"), QStringLiteral("resources"), errors,
                 [&](const QJsonObject& o, const QString& ctx) {
                     JsonReader r(o, ctx, &errors);
                     ResourceDef d;
                     d.key = r.requireId(QStringLiteral("id"));
                     d.name = r.requireString(QStringLiteral("name"));
                     d.isCurrency = r.optionalBool(QStringLiteral("currency"), false);
                     d.value = r.optionalInt(QStringLiteral("value"), d.isCurrency ? 1 : 100, 1, 1000000);
                     r.finish();
                     if (!r.ok()) return;
                     if (byKey.contains(d.key)) {
                         errors << ctx + QStringLiteral(": duplicate resource id '%1'").arg(d.key);
                         return;
                     }
                     byKey.insert(d.key, d);
                 });
    if (byKey.size() > ResourceAmounts::kMax) {
        errors << QStringLiteral("resources.json: at most %1 resources").arg(ResourceAmounts::kMax);
        return;
    }
    int currencies = 0;
    for (ResourceDef& d : byKey) {
        d.id = ResourceId(static_cast<int>(m_resources.size()));
        if (d.isCurrency) {
            m_currency = d.id;
            ++currencies;
            if (d.value != 1) errors << QStringLiteral("resource '%1': the currency must have value 1").arg(d.key);
        }
        m_resources.append(d);
    }
    if (currencies != 1)
        errors << QStringLiteral("resources.json: exactly one resource must have \"currency\": true (found %1)").arg(currencies);
    // Defaults for balance.json values that name resources.
    const ResourceId wood = resourceId(QStringLiteral("wood"));
    if (m_currency.isValid()) {
        m_balance.economy.startingResources[m_currency] = 10000;
        m_balance.economy.boatCost[m_currency] = 1000;
        m_balance.siege.engineCost[m_currency] = 600;
    }
    if (wood.isValid()) {
        m_balance.economy.boatCost[wood] = 10;
        m_balance.siege.engineCost[wood] = 10;
    }
}

ResourceId ContentRegistry::resourceId(const QString& key) const {
    for (const ResourceDef& r : m_resources)
        if (r.key == key) return r.id;
    return ResourceId();
}

ResourceAmounts ContentRegistry::toResources(const QMap<QString, int>& amounts, const QString& context, QStringList& errors) const {
    ResourceAmounts out;
    for (auto it = amounts.cbegin(); it != amounts.cend(); ++it) {
        const ResourceId r = resourceId(it.key());
        if (!r.isValid()) errors << QStringLiteral("%1: unknown resource '%2'").arg(context, it.key());
        else out[r] = it.value();
    }
    return out;
}

void ContentRegistry::loadTerrain(const QString& dir, QStringList& errors) {
    QMap<QString, TerrainDef> byKey;  // sorted by key: the ids
    forEachEntry(dir, QStringLiteral("terrain.json"), QStringLiteral("terrain"), errors,
                 [&](const QJsonObject& o, const QString& ctx) {
                     JsonReader r(o, ctx, &errors);
                     TerrainDef d;
                     d.key = r.requireId(QStringLiteral("id"));
                     d.name = r.requireString(QStringLiteral("name"));
                     d.walkable = r.optionalBool(QStringLiteral("walkable"), true);
                     d.sailable = r.optionalBool(QStringLiteral("sailable"), false);
                     d.moveCost = r.optionalInt(QStringLiteral("moveCost"), 100, 1, 10000);
                     d.blocksVision = r.optionalBool(QStringLiteral("blocksVision"), false);
                     d.color = r.requireColor(QStringLiteral("color"));
                     r.finish();
                     if (!r.ok()) return;
                     if (byKey.contains(d.key)) {
                         errors << ctx + QStringLiteral(": duplicate terrain id '%1'").arg(d.key);
                         return;
                     }
                     byKey.insert(d.key, d);
                 });
    for (TerrainDef& d : byKey) {
        d.id = TerrainId(static_cast<int>(m_terrain.size()));
        m_terrain.append(d);
    }
}

TerrainId ContentRegistry::terrainId(const QString& key) const {
    for (const TerrainDef& t : m_terrain)
        if (t.key == key) return t.id;
    return TerrainId();
}

void ContentRegistry::loadTraits(const QString& dir, QStringList& errors) {
    QMap<QString, TraitDef> byKey;  // sorted by key: the ids
    forEachEntry(dir, QStringLiteral("traits.json"), QStringLiteral("traits"), errors,
                 [&](const QJsonObject& o, const QString& ctx) {
                     JsonReader r(o, ctx, &errors);
                     TraitDef d;
                     d.key = r.requireId(QStringLiteral("id"));
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
                     if (byKey.contains(d.key)) {
                         errors << ctx + QStringLiteral(": duplicate trait id '%1'").arg(d.key);
                         return;
                     }
                     byKey.insert(d.key, d);
                 });
    if (byKey.size() > TraitValues::kMax) {
        errors << QStringLiteral("traits.json: at most %1 traits").arg(TraitValues::kMax);
        return;
    }
    for (TraitDef& d : byKey) {
        d.id = TraitId(static_cast<int>(m_traits.size()));
        m_traits.append(d);
    }
    m_known.attack = traitId(QStringLiteral("attack"));
    m_known.defense = traitId(QStringLiteral("defense"));
    m_known.morale = traitId(QStringLiteral("morale"));
    m_known.luck = traitId(QStringLiteral("luck"));
    m_known.hitPoints = traitId(QStringLiteral("hit_points"));
    m_known.magic = traitId(QStringLiteral("magic"));
    m_known.spellPower = traitId(QStringLiteral("spell_power"));
    m_known.spellRecovery = traitId(QStringLiteral("spell_recovery"));
    m_known.spellPointsMax = traitId(QStringLiteral("spell_points_max"));
    m_known.movementPoints = traitId(QStringLiteral("movement_points"));
    m_known.scouting = traitId(QStringLiteral("scouting"));
    m_known.attackRange = traitId(QStringLiteral("attack_range"));
    m_known.scoutingRadius = traitId(QStringLiteral("scouting_radius"));
}

TraitId ContentRegistry::traitId(const QString& key) const {
    for (const TraitDef& t : m_traits)
        if (t.key == key) return t.id;
    return TraitId();
}

TraitValues ContentRegistry::toTraits(const QMap<QString, int>& values, const QString& context, QStringList& errors) const {
    TraitValues out;
    for (auto it = values.cbegin(); it != values.cend(); ++it) {
        const TraitId t = traitId(it.key());
        if (!t.isValid()) errors << QStringLiteral("%1 uses unknown trait '%2'").arg(context, it.key());
        else out[t] = it.value();
    }
    return out;
}

void ContentRegistry::loadUnits(const QString& dir, QStringList& errors) {
    const QDir unitsDir(QDir(dir).filePath(QStringLiteral("units")));
    if (!unitsDir.exists()) {
        errors << QStringLiteral("units: directory not found");
        return;
    }
    const QFileInfoList files =
        unitsDir.entryInfoList({QStringLiteral("*.json")}, QDir::Files, QDir::Name);
    // The ids come from the file names (sorted), so units can name each other.
    for (const QFileInfo& info : files) m_unitIndex.insert(info.completeBaseName(), static_cast<int>(m_unitIndex.size()));
    m_units.resize(files.size());
    for (int index = 0; index < files.size(); ++index) {
        const QFileInfo& info = files[index];
        const QString ctx = QStringLiteral("units/") + info.fileName();
        QJsonObject o;
        if (!readObjectFile(info.filePath(), ctx, &o, errors)) continue;

        JsonReader r(o, ctx, &errors);
        UnitDef d;
        d.id = UnitTypeId(index);
        d.key = r.requireId(QStringLiteral("id"));
        d.name = r.requireString(QStringLiteral("name"));
        d.race = toRace(r.requireId(QStringLiteral("race")), ctx, errors);
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
        d.weeklyGrowth = r.optionalInt(QStringLiteral("weeklyGrowth"), 0, 0, 10000);
        d.spellPoints = r.optionalInt(QStringLiteral("spellPoints"), 0, 0, 10000);
        d.cost = toResources(r.optionalIntMap(QStringLiteral("cost"), 1, 1000000), ctx + QStringLiteral(" cost"), errors);
        const QStringList abilities = r.optionalStringList(QStringLiteral("abilities"));
        for (const QString& sp : r.optionalStringList(QStringLiteral("spells"))) {
            if (spellId(sp).isValid()) d.spells << spellId(sp);
            else r.addError(QStringLiteral("unknown spell '%1'").arg(sp));
        }
        const QString upgradesTo = r.optionalString(QStringLiteral("upgradesTo"));
        r.finish();
        if (!r.ok()) continue;

        if (d.damageMin > d.damageMax)
            r.addError(QStringLiteral("damageMin (%1) is greater than damageMax (%2)")
                           .arg(d.damageMin).arg(d.damageMax));
        if (d.range > 0 && d.shots < 1)
            r.addError(QStringLiteral("a ranged unit (range > 0) needs shots >= 1"));
        if (d.range == 0 && d.shots > 0)
            r.addError(QStringLiteral("shots > 0 but range is 0 (melee units have no shots)"));
        static const QList<std::pair<QString, Ability>> knownAbilities = {
            {QStringLiteral("flying"), Ability::Flying}, {QStringLiteral("no_retaliation"), Ability::NoRetaliation},
            {QStringLiteral("double_attack"), Ability::DoubleAttack}, {QStringLiteral("undead"), Ability::Undead},
            {QStringLiteral("siege_engine"), Ability::SiegeEngine}};
        for (const QString& a : abilities) {
            bool known = false;
            for (const auto& [name, bit] : knownAbilities)
                if (name == a) {
                    d.abilities |= static_cast<quint32>(bit);
                    known = true;
                }
            if (!known) r.addError(QStringLiteral("unknown ability '%1' (known: flying, no_retaliation, double_attack, undead, siege_engine)").arg(a));
        }
        if (!upgradesTo.isEmpty()) {
            d.upgradesTo = unitId(upgradesTo);
            if (!d.upgradesTo.isValid()) r.addError(QStringLiteral("upgradesTo unknown unit '%1'").arg(upgradesTo));
            else if (d.upgradesTo == d.id) r.addError(QStringLiteral("cannot upgrade to itself"));
        }
        if (d.key != info.completeBaseName())
            r.addError(QStringLiteral("id '%1' must match the file name").arg(d.key));
        if (!r.ok()) continue;
        m_units[index] = d;
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
    // The ids come from the file names (sorted), so buildings can name each other.
    for (const QFileInfo& info : files) m_buildingIndex.insert(info.completeBaseName(), static_cast<int>(m_buildingIndex.size()));
    m_buildings.resize(files.size());
    for (int index = 0; index < files.size(); ++index) {
        const QFileInfo& info = files[index];
        const QString ctx = QStringLiteral("buildings/") + info.fileName();
        QJsonObject o;
        if (!readObjectFile(info.filePath(), ctx, &o, errors)) continue;

        JsonReader r(o, ctx, &errors);
        BuildingDef d;
        d.id = BuildingTypeId(index);
        d.key = r.requireId(QStringLiteral("id"));
        d.name = r.requireString(QStringLiteral("name"));
        if (r.has(QStringLiteral("race"))) d.race = toRace(r.requireId(QStringLiteral("race")), ctx, errors);
        d.cost = toResources(r.optionalIntMap(QStringLiteral("cost"), 1, 1000000), ctx + QStringLiteral(" cost"), errors);
        const auto ids = [&](const QStringList& keys, const char* field) {
            QList<BuildingTypeId> out;
            for (const QString& k : keys) {
                const BuildingTypeId b = buildingId(k);
                if (!b.isValid()) r.addError(QStringLiteral("%1: unknown building '%2'").arg(QLatin1String(field), k));
                else if (b == d.id) r.addError(QStringLiteral("%1: refers to itself").arg(QLatin1String(field)));
                else out << b;
            }
            return out;
        };
        d.requiredBuildings = ids(r.optionalStringList(QStringLiteral("requires")), "requires");
        d.excludes = ids(r.optionalStringList(QStringLiteral("excludes")), "excludes");
        for (const BuildingTypeId b : d.requiredBuildings)
            if (d.excludes.contains(b)) r.addError(QStringLiteral("'%1' is both required and excluded").arg(files[b.index()].completeBaseName()));
        if (const QString up = r.optionalString(QStringLiteral("upgradeOf")); !up.isEmpty()) {
            d.upgradeOf = buildingId(up);
            if (!d.upgradeOf.isValid()) r.addError(QStringLiteral("upgradeOf unknown building '%1'").arg(up));
        }
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
                const QString key = er.requireId(QStringLiteral("unit"));
                e.unit = unitId(key);
                if (!key.isEmpty() && !e.unit.isValid()) er.addError(QStringLiteral("dwelling of unknown unit '%1'").arg(key));
            } else if (type == QLatin1String("tavern")) {
                e.type = BuildingEffect::Type::Tavern;
            } else if (type == QLatin1String("income")) {
                e.type = BuildingEffect::Type::Income;
                const QString key = er.requireId(QStringLiteral("resource"));
                e.resource = resourceId(key);
                if (!key.isEmpty() && !e.resource.isValid()) er.addError(QStringLiteral("income of unknown resource '%1'").arg(key));
                e.amount = er.requireInt(QStringLiteral("amount"), 1, 1000000);
            } else if (type == QLatin1String("shipyard")) {
                e.type = BuildingEffect::Type::Shipyard;
            } else if (type == QLatin1String("market")) {
                e.type = BuildingEffect::Type::Market;
                e.amount = er.requireInt(QStringLiteral("amount"), 1, 50);
            } else if (type == QLatin1String("defense")) {
                e.type = BuildingEffect::Type::Defense;
                e.amount = er.requireInt(QStringLiteral("amount"), 1, 100);
            } else if (type == QLatin1String("vision")) {
                e.type = BuildingEffect::Type::Vision;
                e.amount = er.requireInt(QStringLiteral("amount"), 1, 50);
            } else if (type == QLatin1String("growth")) {
                e.type = BuildingEffect::Type::Growth;
                e.amount = er.requireInt(QStringLiteral("amount"), 1, 500);
            } else if (type == QLatin1String("mageGuild")) {
                e.type = BuildingEffect::Type::MageGuild;
                e.amount = er.requireInt(QStringLiteral("level"), 1, 10);
            } else if (!type.isEmpty()) {
                er.addError(QStringLiteral("unknown effect type '%1' (use dwelling, income, tavern, mageGuild, defense, market, shipyard, growth or vision)").arg(type));
            }
            er.finish();
            if (er.ok()) d.effects.append(e);
            else r.addError(QStringLiteral("has an invalid effect"));
        }
        if (d.key != info.completeBaseName())
            r.addError(QStringLiteral("id '%1' must match the file name").arg(d.key));
        if (!r.ok()) continue;
        m_buildings[index] = d;
    }
}

void ContentRegistry::loadArtifacts(const QString& dir, QStringList& errors) {
    if (!QFile::exists(QDir(dir).filePath(QStringLiteral("artifacts.json")))) return;  // optional
    QMap<QString, ArtifactDef> byKey;  // sorted by key: the ids
    forEachEntry(dir, QStringLiteral("artifacts.json"), QStringLiteral("artifacts"), errors,
                 [&](const QJsonObject& o, const QString& ctx) {
                     JsonReader r(o, ctx, &errors);
                     ArtifactDef d;
                     d.key = r.requireId(QStringLiteral("id"));
                     d.name = r.requireString(QStringLiteral("name"));
                     d.description = r.optionalString(QStringLiteral("description"));
                     d.traits = toTraits(r.optionalIntMap(QStringLiteral("traits"), -100, 100), ctx, errors);
                     d.value = r.optionalInt(QStringLiteral("value"), 1, 1, 3);
                     d.slot = r.optionalString(QStringLiteral("slot"), QStringLiteral("misc"));
                     if (artifactSlotCount(d.slot) == 0) r.addError(QStringLiteral("unknown slot '%1'").arg(d.slot));
                     r.finish();
                     if (!r.ok()) return;
                     if (byKey.contains(d.key)) {
                         errors << ctx + QStringLiteral(": duplicate artifact id '%1'").arg(d.key);
                         return;
                     }
                     byKey.insert(d.key, d);
                 });
    for (ArtifactDef& d : byKey) {
        d.id = ArtifactId(static_cast<int>(m_artifacts.size()));
        m_artifacts.append(d);
    }
}

ArtifactId ContentRegistry::artifactId(const QString& key) const {
    for (const ArtifactDef& a : m_artifacts)
        if (a.key == key) return a.id;
    return ArtifactId();
}

void ContentRegistry::loadWeekEvents(const QString& dir, QStringList& errors) {
    QMap<QString, WeekEventDef> byKey;  // sorted by key: the ids
    if (!QFile::exists(QDir(dir).filePath(QStringLiteral("events.json")))) return;  // optional
    forEachEntry(dir, QStringLiteral("events.json"), QStringLiteral("events"), errors,
                 [&](const QJsonObject& o, const QString& ctx) {
                     JsonReader r(o, ctx, &errors);
                     WeekEventDef d;
                     d.key = r.requireId(QStringLiteral("id"));
                     d.name = r.requireString(QStringLiteral("name"));
                     d.description = r.optionalString(QStringLiteral("description"));
                     d.weight = r.optionalInt(QStringLiteral("weight"), 10, 0, 10000);
                     d.resources = toResources(r.optionalIntMap(QStringLiteral("resources"), -1000000, 1000000), ctx, errors);
                     d.growthPercent = r.optionalInt(QStringLiteral("growthPercent"), 0, -100, 1000);
                     const QMap<QString, int> growth = r.optionalIntMap(QStringLiteral("unitGrowth"), 1, 10000);
                     for (auto it = growth.cbegin(); it != growth.cend(); ++it) {
                         if (unitId(it.key()).isValid()) d.unitGrowth.insert(unitId(it.key()), it.value());
                         else errors << ctx + QStringLiteral(": unknown unit '%1'").arg(it.key());
                     }
                     r.finish();
                     if (r.ok()) byKey.insert(d.key, d);
                 });
    for (WeekEventDef& d : byKey) {
        d.id = WeekEventId(static_cast<int>(m_weekEvents.size()));
        m_weekEvents.append(d);
    }
}

WeekEventId ContentRegistry::weekEventId(const QString& key) const {
    for (const WeekEventDef& e : m_weekEvents)
        if (e.key == key) return e.id;
    return WeekEventId();
}

void ContentRegistry::loadRaces(const QString& dir, QStringList& errors) {
    const QDir racesDir(QDir(dir).filePath(QStringLiteral("races")));
    for (const QFileInfo& info : racesDir.entryInfoList({QStringLiteral("*.json")}, QDir::Files, QDir::Name)) {
        const int index = m_raceIndex.value(info.completeBaseName());
        const QString ctx = QStringLiteral("races/") + info.fileName();
        QJsonObject o;
        if (!readObjectFile(info.filePath(), ctx, &o, errors)) continue;
        JsonReader r(o, ctx, &errors);
        RaceDef d;
        d.id = RaceId(index);
        d.key = r.requireId(QStringLiteral("id"));
        d.name = r.requireString(QStringLiteral("name"));
        d.description = r.optionalString(QStringLiteral("description"));
        d.growthPercent = r.optionalInt(QStringLiteral("growthPercent"), 0, -90, 500);
        d.incomePercent = r.optionalInt(QStringLiteral("incomePercent"), 0, -90, 500);
        {
            const QMap<QString, int> percent = r.optionalIntMap(QStringLiteral("terrainCostPercent"), 10, 1000);
            if (!percent.isEmpty()) d.terrainCostPercent.fill(100, m_terrain.size());
            for (auto it = percent.cbegin(); it != percent.cend(); ++it) {
                const TerrainId t = terrainId(it.key());
                if (t.isValid()) d.terrainCostPercent[t.index()] = it.value();
                else errors << ctx + QStringLiteral(": unknown terrain '%1'").arg(it.key());
            }
        }
        d.spellSchools = r.optionalStringList(QStringLiteral("spellSchools"));
        if (const QString raise = r.optionalString(QStringLiteral("raiseDeadUnit")); !raise.isEmpty()) {
            d.raiseDeadUnit = unitId(raise);
            if (!d.raiseDeadUnit.isValid()) r.addError(QStringLiteral("raiseDeadUnit: unknown unit '%1'").arg(raise));
        }
        d.raiseDeadPercent = r.optionalInt(QStringLiteral("raiseDeadPercent"), 0, 0, 100);
        d.battleLootPercent = r.optionalInt(QStringLiteral("battleLootPercent"), 0, 0, 1000);
        d.tradeBonus = r.optionalInt(QStringLiteral("tradeBonus"), 0, 0, 50);
        d.wallDefenseBonus = r.optionalInt(QStringLiteral("wallDefenseBonus"), 0, 0, 50);
        d.visionBonus = r.optionalInt(QStringLiteral("visionBonus"), 0, 0, 10);
        d.spellSchoolPercent = r.optionalIntMap(QStringLiteral("spellSchoolPercent"), 10, 500);
        d.abilityText = r.optionalString(QStringLiteral("ability"));
        r.finish();
        if (d.key != info.completeBaseName())
            r.addError(QStringLiteral("id '%1' must match the file name").arg(d.key));
        if (!r.ok()) continue;
        m_races[index] = d;
    }
}

void ContentRegistry::loadSpells(const QString& dir, QStringList& errors) {
    const QDir spellsDir(QDir(dir).filePath(QStringLiteral("spells")));
    if (!spellsDir.exists()) return;  // a game without magic is fine
    for (const QFileInfo& info : spellsDir.entryInfoList({QStringLiteral("*.json")}, QDir::Files, QDir::Name)) {
        const int index = m_spellIndex.value(info.completeBaseName());
        const QString ctx = QStringLiteral("spells/") + info.fileName();
        QJsonObject o;
        if (!readObjectFile(info.filePath(), ctx, &o, errors)) continue;
        JsonReader r(o, ctx, &errors);
        SpellDef d;
        d.id = SpellId(index);
        d.key = r.requireId(QStringLiteral("id"));
        d.name = r.requireString(QStringLiteral("name"));
        d.school = r.optionalString(QStringLiteral("school"), QStringLiteral("general"));
        d.description = r.optionalString(QStringLiteral("description"));
        d.level = r.requireInt(QStringLiteral("level"), 1, 10);
        d.cost = r.requireInt(QStringLiteral("cost"), 0, 10000);
        d.requiredMagic = r.optionalInt(QStringLiteral("requiredMagic"), d.level, 0, 100);
        const QString scope = r.requireString(QStringLiteral("scope"));
        const QString target = r.optionalString(QStringLiteral("target"), QStringLiteral("none"));
        d.area = r.optionalInt(QStringLiteral("area"), 0, 0, 5);
        const QJsonArray effects = r.requireArray(QStringLiteral("effects"));
        r.finish();
        if (scope == QLatin1String("battle")) d.scope = SpellDef::Scope::Battle;
        else if (scope == QLatin1String("adventure")) d.scope = SpellDef::Scope::Adventure;
        else if (!scope.isEmpty()) r.addError(QStringLiteral("scope must be battle or adventure"));
        if (target == QLatin1String("enemy")) d.target = SpellDef::Target::Enemy;
        else if (target == QLatin1String("ally")) d.target = SpellDef::Target::Ally;
        else if (target != QLatin1String("none")) r.addError(QStringLiteral("target must be none, enemy or ally"));
        if (d.scope == SpellDef::Scope::Battle && d.target == SpellDef::Target::None)
            r.addError(QStringLiteral("a battle spell needs a target (enemy or ally)"));
        if (d.scope == SpellDef::Scope::Adventure && d.target != SpellDef::Target::None)
            r.addError(QStringLiteral("adventure spells have no target"));
        if (effects.isEmpty()) r.addError(QStringLiteral("a spell needs at least one effect"));
        for (qsizetype i = 0; i < effects.size(); ++i) {
            const QString ectx = QStringLiteral("%1: effects[%2]").arg(ctx).arg(i);
            if (!effects[i].isObject()) {
                errors << ectx + QStringLiteral(": entry must be an object");
                continue;
            }
            JsonReader er(effects[i].toObject(), ectx, &errors);
            SpellEffect e;
            const QString type = er.requireString(QStringLiteral("type"));
            e.amount = er.optionalInt(QStringLiteral("amount"), 0, -10000, 100000);
            e.perPower = er.optionalInt(QStringLiteral("perPower"), 0, -10000, 100000);
            bool battleOnly = true;
            if (type == QLatin1String("damage")) e.type = SpellEffect::Type::Damage;
            else if (type == QLatin1String("heal")) e.type = SpellEffect::Type::Heal;
            else if (type == QLatin1String("modifyStat")) {
                e.type = SpellEffect::Type::ModifyStat;
                e.stat = er.requireString(QStringLiteral("stat"));
                e.rounds = er.requireInt(QStringLiteral("rounds"), 1, 100);
                if (!e.stat.isEmpty() && e.stat != "attack" && e.stat != "defense" && e.stat != "battleSpeed")
                    er.addError(QStringLiteral("stat must be attack, defense or battleSpeed"));
            } else if (type == QLatin1String("movement")) {
                e.type = SpellEffect::Type::Movement;
                battleOnly = false;
            } else if (type == QLatin1String("reveal")) {
                e.type = SpellEffect::Type::Reveal;
                battleOnly = false;
            } else if (!type.isEmpty()) {
                er.addError(QStringLiteral("unknown effect type '%1' (use damage, heal, modifyStat, movement or reveal)").arg(type));
            }
            er.finish();
            if (er.ok() && battleOnly != (d.scope == SpellDef::Scope::Battle))
                er.addError(QStringLiteral("effect '%1' does not fit a %2 spell").arg(type, scope));
            if (er.ok()) d.effects.append(e);
            else r.addError(QStringLiteral("has an invalid effect"));
        }
        if (d.key != info.completeBaseName())
            r.addError(QStringLiteral("id '%1' must match the file name").arg(d.key));
        if (!r.ok()) continue;
        m_spells[index] = d;
    }
}

void ContentRegistry::loadHeroTemplates(const QString& dir, QStringList& errors) {
    QMap<QString, HeroTemplateDef> byKey;  // sorted by key: the ids
    forEachEntry(dir, QStringLiteral("heroes.json"), QStringLiteral("heroes"), errors,
                 [&](const QJsonObject& o, const QString& ctx) {
                     JsonReader r(o, ctx, &errors);
                     HeroTemplateDef d;
                     d.key = r.requireId(QStringLiteral("id"));
                     d.name = r.requireString(QStringLiteral("name"));
                     const QString cls = r.requireId(QStringLiteral("class"));
                     d.classId = heroClassId(cls);
                     if (!cls.isEmpty() && !d.classId.isValid()) r.addError(QStringLiteral("unknown class '%1'").arg(cls));
                     r.finish();
                     if (!r.ok()) return;
                     if (byKey.contains(d.key)) {
                         errors << ctx + QStringLiteral(": duplicate hero id '%1'").arg(d.key);
                         return;
                     }
                     byKey.insert(d.key, d);
                 });
    for (HeroTemplateDef& d : byKey) {
        d.id = HeroTemplateId(static_cast<int>(m_heroTemplates.size()));
        m_heroTemplates.append(d);
    }
}

HeroTemplateId ContentRegistry::heroTemplateId(const QString& key) const {
    for (const HeroTemplateDef& h : m_heroTemplates)
        if (h.key == key) return h.id;
    return HeroTemplateId();
}

HeroClassId ContentRegistry::heroClassId(const QString& key) const {
    for (const HeroClassDef& c : m_heroClasses)
        if (c.key == key) return c.id;
    return HeroClassId();
}

void ContentRegistry::loadHeroClasses(const QString& dir, QStringList& errors) {
    QMap<QString, HeroClassDef> byKey;  // sorted by key: the ids
    forEachEntry(dir, QStringLiteral("hero_classes.json"), QStringLiteral("heroClasses"), errors,
                 [&](const QJsonObject& o, const QString& ctx) {
                     JsonReader r(o, ctx, &errors);
                     HeroClassDef d;
                     d.key = r.requireId(QStringLiteral("id"));
                     d.name = r.requireString(QStringLiteral("name"));
                     d.race = toRace(r.requireId(QStringLiteral("race")), ctx, errors);
                     const QString what = QStringLiteral("hero class '%1': ").arg(d.key);
                     d.baseTraits = toTraits(r.optionalIntMap(QStringLiteral("baseTraits"), 0, 1000), what + "baseTraits", errors);
                     d.pickIncrement = toTraits(r.optionalIntMap(QStringLiteral("pickIncrement"), 1, 100), what + "pickIncrement", errors);
                     d.autoGrowth = toTraits(r.optionalIntMap(QStringLiteral("autoGrowth"), 1, 1000), what + "autoGrowth", errors);
                     for (const auto [t, v] : d.pickIncrement)
                         if (!trait(t)->pickable)
                             errors << what + QStringLiteral("pickIncrement lists trait '%1' which is not pickable").arg(trait(t)->key);
                     const QMap<QString, int> army = r.optionalIntMap(QStringLiteral("startingArmy"), 1, 10000);
                     for (auto it = army.cbegin(); it != army.cend(); ++it) {
                         if (unitId(it.key()).isValid()) d.startingArmy.insert(unitId(it.key()), it.value());
                         else errors << ctx + QStringLiteral(": startingArmy uses unknown unit '%1'").arg(it.key());
                     }
                     for (const QString& sp : r.optionalStringList(QStringLiteral("startingSpells"))) {
                         if (spellId(sp).isValid()) d.startingSpells << spellId(sp);
                         else errors << ctx + QStringLiteral(": startingSpells uses unknown spell '%1'").arg(sp);
                     }
                     r.finish();
                     if (!r.ok()) return;
                     if (byKey.contains(d.key)) {
                         errors << ctx + QStringLiteral(": duplicate hero class id '%1'").arg(d.key);
                         return;
                     }
                     byKey.insert(d.key, d);
                 });
    for (HeroClassDef& d : byKey) {
        d.id = HeroClassId(static_cast<int>(m_heroClasses.size()));
        m_heroClasses.append(d);
    }
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
    const QJsonObject battle = root.contains(QStringLiteral("battle"))
        ? top.requireObject(QStringLiteral("battle")) : QJsonObject();
    const QJsonObject difficulty = root.contains(QStringLiteral("difficulty"))
        ? top.requireObject(QStringLiteral("difficulty")) : QJsonObject();
    const QJsonObject veterancy = root.contains(QStringLiteral("veterancy"))
        ? top.requireObject(QStringLiteral("veterancy")) : QJsonObject();
    const QJsonObject siege = root.contains(QStringLiteral("siege"))
        ? top.requireObject(QStringLiteral("siege")) : QJsonObject();
    const QJsonObject scouting = root.contains(QStringLiteral("scouting"))
        ? top.requireObject(QStringLiteral("scouting")) : QJsonObject();
    const QJsonObject neutralTowns = root.contains(QStringLiteral("neutralTowns"))
        ? top.requireObject(QStringLiteral("neutralTowns")) : QJsonObject();
    const QJsonObject aiSlot = root.contains(QStringLiteral("aiSlotDifficulty"))
        ? top.requireObject(QStringLiteral("aiSlotDifficulty")) : QJsonObject();
    top.finish();
    if (!top.ok()) return;

    const auto readLevels = [&](const QJsonObject& obj, const QString& name, QMap<QString, DifficultyLevel>* out) {
        for (auto it = obj.begin(); it != obj.end(); ++it) {
            if (it.key().startsWith('_')) continue;
            const QString ctx = file + "/" + name + "/" + it.key();
            if (!it.value().isObject()) {
                errors << ctx + QStringLiteral(": must be an object");
                continue;
            }
            JsonReader lr(it.value().toObject(), ctx, &errors);
            DifficultyLevel d;
            d.humanStartPercent = lr.optionalInt(QStringLiteral("humanStartPercent"), 100, 0, 1000);
            d.monsterPercent = lr.optionalInt(QStringLiteral("monsterPercent"), 100, 10, 1000);
            d.aiIncomePercent = lr.optionalInt(QStringLiteral("aiIncomePercent"), 0, -90, 500);
            d.aiGrowthPercent = lr.optionalInt(QStringLiteral("aiGrowthPercent"), 0, -90, 500);
            lr.finish();
            if (lr.ok()) out->insert(it.key(), d);
        }
    };
    readLevels(difficulty, QStringLiteral("difficulty"), &m_balance.difficulty);
    if (!siege.isEmpty()) {
        JsonReader sr(siege, file + QStringLiteral("/siege"), &errors);
        SiegeBalance sb;
        sb.wallHpPerLevel = sr.optionalInt(QStringLiteral("wallHpPerLevel"), sb.wallHpPerLevel, 1, 100000);
        sb.towerDamagePerLevel = sr.optionalInt(QStringLiteral("towerDamagePerLevel"), sb.towerDamagePerLevel, 0, 100000);
        sb.defenderShotPercentPerLevel = sr.optionalInt(QStringLiteral("defenderShotPercentPerLevel"), sb.defenderShotPercentPerLevel, 0, 1000);
        sb.catapultDamageMin = sr.optionalInt(QStringLiteral("catapultDamageMin"), sb.catapultDamageMin, 1, 100000);
        sb.catapultDamageMax = sr.optionalInt(QStringLiteral("catapultDamageMax"), sb.catapultDamageMax, 1, 100000);
        sb.engineCost = m_balance.siege.engineCost;
        if (sr.has(QStringLiteral("engineCost")))
            sb.engineCost = toResources(sr.optionalIntMap(QStringLiteral("engineCost"), 0, 1000000), file + QStringLiteral("/siege/engineCost"), errors);
        sr.finish();
        if (sb.catapultDamageMax < sb.catapultDamageMin) sr.addError(QStringLiteral("catapultDamageMax below catapultDamageMin"));
        if (sr.ok()) m_balance.siege = sb;
    }
    if (!scouting.isEmpty()) {
        JsonReader sr(scouting, file + QStringLiteral("/scouting"), &errors);
        ScoutingBalance sb;
        sb.visionPerLevel = sr.optionalInt(QStringLiteral("visionPerLevel"), sb.visionPerLevel, 0, 100);
        sb.movementPercentPerLevel = sr.optionalInt(QStringLiteral("movementPercentPerLevel"), sb.movementPercentPerLevel, 0, 1000);
        sb.zoneOfControlRadius = sr.optionalInt(QStringLiteral("zoneOfControlRadius"), sb.zoneOfControlRadius, 0, 20);
        sb.zoneOfControlMinRadius = sr.optionalInt(QStringLiteral("zoneOfControlMinRadius"), sb.zoneOfControlMinRadius, 0, 20);
        sb.levelsPerZoneStep = sr.optionalInt(QStringLiteral("levelsPerZoneStep"), sb.levelsPerZoneStep, 1, 100);
        sr.finish();
        if (sb.zoneOfControlMinRadius > sb.zoneOfControlRadius)
            sr.addError(QStringLiteral("zoneOfControlMinRadius must not be above zoneOfControlRadius"));
        if (sr.ok()) m_balance.scouting = sb;
    }
    if (!neutralTowns.isEmpty()) {
        JsonReader nr(neutralTowns, file + QStringLiteral("/neutralTowns"), &errors);
        NeutralTownBalance nb;
        nb.growthPercent = nr.optionalInt(QStringLiteral("growthPercent"), nb.growthPercent, 0, 1000);
        nb.capWeeks = nr.optionalInt(QStringLiteral("capWeeks"), nb.capWeeks, 0, 100);
        nr.finish();
        if (nr.ok()) m_balance.neutralTowns = nb;
    }
    if (!veterancy.isEmpty()) {
        JsonReader vr(veterancy, file + QStringLiteral("/veterancy"), &errors);
        VeterancyBalance vb;
        vb.xpPercent = vr.optionalInt(QStringLiteral("xpPercent"), vb.xpPercent, 0, 10000);
        const QJsonArray th = vr.has(QStringLiteral("thresholds")) ? vr.requireArray(QStringLiteral("thresholds")) : QJsonArray();
        const QJsonArray bonuses = vr.has(QStringLiteral("starBonuses")) ? vr.requireArray(QStringLiteral("starBonuses")) : QJsonArray();
        vr.finish();
        if (!th.isEmpty()) {
            vb.thresholds.clear();
            int previous = 0;
            for (const QJsonValue& v : th) {
                if (!v.isDouble() || v.toInt() <= previous) {
                    vr.addError(QStringLiteral("thresholds must be ascending positive numbers"));
                    break;
                }
                previous = v.toInt();
                vb.thresholds << previous;
            }
        }
        if (!bonuses.isEmpty()) {
            vb.starBonuses.clear();
            static const QStringList stats = {"attack", "defense", "initiative", "battleSpeed", "morale"};
            for (const QJsonValue& v : bonuses) {
                QMap<QString, int> m;
                const QJsonObject o = v.toObject();
                for (auto it = o.begin(); it != o.end(); ++it) {
                    if (!stats.contains(it.key())) vr.addError(QStringLiteral("starBonuses: unknown stat '%1' (%2)").arg(it.key(), stats.join(", ")));
                    m.insert(it.key(), it.value().toInt());
                }
                vb.starBonuses << m;
            }
        }
        if (vb.starBonuses.size() < vb.thresholds.size())
            vr.addError(QStringLiteral("starBonuses needs one entry per threshold"));
        if (vr.ok()) m_balance.veterancy = vb;
    }
    readLevels(aiSlot, QStringLiteral("aiSlotDifficulty"), &m_balance.aiSlot);

    {
        JsonReader br(battle, file + QStringLiteral("/battle"), &errors);
        BattleBalance bb;
        bb.maxRounds = br.optionalInt(QStringLiteral("maxRounds"), bb.maxRounds, 1, 1000);
        bb.experiencePerHp = br.optionalInt(QStringLiteral("experiencePerHp"), bb.experiencePerHp, 0, 1000);
        bb.attackPercentPerPoint = br.optionalInt(QStringLiteral("attackPercentPerPoint"), bb.attackPercentPerPoint, 0, 100);
        bb.maxAttackPercent = br.optionalInt(QStringLiteral("maxAttackPercent"), bb.maxAttackPercent, 0, 10000);
        bb.defensePercentPerPoint = br.optionalInt(QStringLiteral("defensePercentPerPoint"), bb.defensePercentPerPoint, 0, 100);
        bb.maxDefensePercent = br.optionalInt(QStringLiteral("maxDefensePercent"), bb.maxDefensePercent, 0, 99);
        bb.moralePercentPerPoint = br.optionalInt(QStringLiteral("moralePercentPerPoint"), bb.moralePercentPerPoint, 0, 100);
        bb.luckPercentPerPoint = br.optionalInt(QStringLiteral("luckPercentPerPoint"), bb.luckPercentPerPoint, 0, 100);
        bb.hpPercentPerTraitLevel = br.optionalInt(QStringLiteral("hpPercentPerTraitLevel"), bb.hpPercentPerTraitLevel, 0, 100);
        bb.obstacles = br.optionalInt(QStringLiteral("obstacles"), bb.obstacles, 0, 30);
        bb.joinStrengthRatio = br.optionalInt(QStringLiteral("joinStrengthRatio"), bb.joinStrengthRatio, 0, 1000);
        bb.joinCostPercent = br.optionalInt(QStringLiteral("joinCostPercent"), bb.joinCostPercent, 0, 1000);
        bb.surrenderCostPercent = br.optionalInt(QStringLiteral("surrenderCostPercent"), bb.surrenderCostPercent, 0, 1000);
        br.finish();
        if (br.ok()) m_balance.battle = bb;
    }

    {
        JsonReader vr(vision, file + QStringLiteral("/vision"), &errors);
        VisionBalance vb;
        vb.townRadius = vr.requireInt(QStringLiteral("townRadius"), 1, 100);
        vb.mineRadius = vr.requireInt(QStringLiteral("mineRadius"), 1, 100);
        vr.finish();
        JsonReader er(economy, file + QStringLiteral("/economy"), &errors);
        EconomyBalance eb = m_balance.economy;  // the resource defaults
        eb.townDailyGold = er.requireInt(QStringLiteral("townDailyGold"), 0, 1000000);
        eb.tradeEfficiencyPercent = er.optionalInt(QStringLiteral("tradeEfficiencyPercent"), 80, 1, 100);
        eb.heroCost = er.optionalInt(QStringLiteral("heroCost"), 2500, 0, 10000000);
        eb.tavernOffers = er.optionalInt(QStringLiteral("tavernOffers"), 2, 1, 10);
        eb.maxHeroesPerPlayer = er.optionalInt(QStringLiteral("maxHeroesPerPlayer"), 8, 1, 100);
        const ResourceAmounts start = toResources(er.optionalIntMap(QStringLiteral("startingResources"), 0, 1000000000),
                                                  file + QStringLiteral("/economy/startingResources"), errors);
        const ResourceAmounts boat = toResources(er.optionalIntMap(QStringLiteral("boatCost"), 1, 1000000), file + QStringLiteral("/economy/boatCost"), errors);
        if (!boat.isEmpty()) eb.boatCost = boat;
        if (!start.isEmpty()) eb.startingResources = start;
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
    for (const UnitDef& u : m_units) {
        for (const SpellId sp : u.spells)
            if (spell(sp)->scope != SpellDef::Scope::Battle) errors << QStringLiteral("unit '%1': '%2' is not a battle spell").arg(u.key, spell(sp)->key);
        if (!u.spells.isEmpty() && u.spellPoints <= 0)
            errors << QStringLiteral("unit '%1': has spells but no spellPoints").arg(u.key);
    }

    for (const BuildingDef& b : m_buildings) {
        const QString what = QStringLiteral("building '%1'").arg(b.key);
        if (const BuildingDef* base = building(b.upgradeOf)) {
            // The upgraded dwelling must produce the upgrade of the base dwelling's unit.
            UnitTypeId baseUnit, newUnit;
            for (const BuildingEffect& e : base->effects)
                if (e.type == BuildingEffect::Type::Dwelling) baseUnit = e.unit;
            for (const BuildingEffect& e : b.effects)
                if (e.type == BuildingEffect::Type::Dwelling) newUnit = e.unit;
            const UnitDef* bu = unit(baseUnit);
            if (!newUnit.isValid() || !bu || bu->upgradesTo != newUnit)
                errors << what + QStringLiteral(": upgradeOf '%1' needs dwellings where the "
                                                "unit of '%1' upgrades to this unit").arg(base->key);
        }
    }
}




QList<RaceId> ContentRegistry::playableRaces() const {
    QList<RaceId> races;
    for (const RaceDef& rc : m_races)
        for (const HeroClassDef& c : m_heroClasses)
            if (c.race == rc.id) {
                races.append(rc.id);
                break;
            }
    return races;
}

QStringList ContentRegistry::playableRaceKeys() const {
    QStringList keys;
    for (const RaceId id : playableRaces()) keys << race(id).key;
    return keys;
}

RaceId ContentRegistry::toRace(const QString& key, const QString& context, QStringList& errors) const {
    if (key.isEmpty()) return {};
    const RaceId id = raceId(key);
    if (!id.isValid()) errors << context + QStringLiteral(": race '%1' has no file in races/").arg(key);
    return id;
}







}  // namespace kot::core
