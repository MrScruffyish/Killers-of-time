#include "core/content/ContentRegistry.h"

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
    m_resources.clear();
    m_terrain.clear();
    m_traits.clear();
    m_units.clear();
    m_heroClasses.clear();
    m_buildings.clear();
    m_heroTemplates.clear();
    m_spells.clear();
    m_races.clear();
    m_artifacts.clear();
    m_weekEvents.clear();
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
            for (QString next = u.upgradesTo; !next.isEmpty() && rank < 10; next = m_units.value(next).upgradesTo) {
                ++rank;
                UnitDef& n = m_units[next];
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
        d.weeklyGrowth = r.optionalInt(QStringLiteral("weeklyGrowth"), 0, 0, 10000);
        d.spellPoints = r.optionalInt(QStringLiteral("spellPoints"), 0, 0, 10000);
        d.cost = r.optionalIntMap(QStringLiteral("cost"), 1, 1000000);
        d.abilities = r.optionalStringList(QStringLiteral("abilities"));
        d.spells = r.optionalStringList(QStringLiteral("spells"));
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
        static const QStringList knownAbilities = {QStringLiteral("flying"), QStringLiteral("no_retaliation"),
                                                   QStringLiteral("double_attack"), QStringLiteral("undead"),
                                                   QStringLiteral("siege_engine")};
        for (const QString& a : d.abilities)
            if (!knownAbilities.contains(a))
                r.addError(QStringLiteral("unknown ability '%1' (known: %2)").arg(a, knownAbilities.join(", ")));
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
        d.race = r.has(QStringLiteral("race")) ? r.requireId(QStringLiteral("race")) : QString();
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

void ContentRegistry::loadArtifacts(const QString& dir, QStringList& errors) {
    if (!QFile::exists(QDir(dir).filePath(QStringLiteral("artifacts.json")))) return;  // optional
    forEachEntry(dir, QStringLiteral("artifacts.json"), QStringLiteral("artifacts"), errors,
                 [&](const QJsonObject& o, const QString& ctx) {
                     JsonReader r(o, ctx, &errors);
                     ArtifactDef d;
                     d.id = r.requireId(QStringLiteral("id"));
                     d.name = r.requireString(QStringLiteral("name"));
                     d.description = r.optionalString(QStringLiteral("description"));
                     d.traits = r.optionalIntMap(QStringLiteral("traits"), -100, 100);
                     d.value = r.optionalInt(QStringLiteral("value"), 1, 1, 3);
                     d.slot = r.optionalString(QStringLiteral("slot"), QStringLiteral("misc"));
                     if (artifactSlotCount(d.slot) == 0) r.addError(QStringLiteral("unknown slot '%1'").arg(d.slot));
                     r.finish();
                     if (!r.ok()) return;
                     if (m_artifacts.contains(d.id)) {
                         errors << ctx + QStringLiteral(": duplicate artifact id '%1'").arg(d.id);
                         return;
                     }
                     m_artifacts.insert(d.id, d);
                 });
}

void ContentRegistry::loadWeekEvents(const QString& dir, QStringList& errors) {
    if (!QFile::exists(QDir(dir).filePath(QStringLiteral("events.json")))) return;  // optional
    forEachEntry(dir, QStringLiteral("events.json"), QStringLiteral("events"), errors,
                 [&](const QJsonObject& o, const QString& ctx) {
                     JsonReader r(o, ctx, &errors);
                     WeekEventDef d;
                     d.id = r.requireId(QStringLiteral("id"));
                     d.name = r.requireString(QStringLiteral("name"));
                     d.description = r.optionalString(QStringLiteral("description"));
                     d.weight = r.optionalInt(QStringLiteral("weight"), 10, 0, 10000);
                     d.resources = r.optionalIntMap(QStringLiteral("resources"), -1000000, 1000000);
                     d.growthPercent = r.optionalInt(QStringLiteral("growthPercent"), 0, -100, 1000);
                     d.unitGrowth = r.optionalIntMap(QStringLiteral("unitGrowth"), 1, 10000);
                     r.finish();
                     if (r.ok()) m_weekEvents.insert(d.id, d);
                 });
}

void ContentRegistry::loadRaces(const QString& dir, QStringList& errors) {
    const QDir racesDir(QDir(dir).filePath(QStringLiteral("races")));
    if (!racesDir.exists()) return;  // races without a file have no bonuses
    for (const QFileInfo& info : racesDir.entryInfoList({QStringLiteral("*.json")}, QDir::Files, QDir::Name)) {
        const QString ctx = QStringLiteral("races/") + info.fileName();
        QJsonObject o;
        if (!readObjectFile(info.filePath(), ctx, &o, errors)) continue;
        JsonReader r(o, ctx, &errors);
        RaceDef d;
        d.id = r.requireId(QStringLiteral("id"));
        d.name = r.requireString(QStringLiteral("name"));
        d.description = r.optionalString(QStringLiteral("description"));
        d.growthPercent = r.optionalInt(QStringLiteral("growthPercent"), 0, -90, 500);
        d.incomePercent = r.optionalInt(QStringLiteral("incomePercent"), 0, -90, 500);
        d.terrainCostPercent = r.optionalIntMap(QStringLiteral("terrainCostPercent"), 10, 1000);
        d.spellSchools = r.optionalStringList(QStringLiteral("spellSchools"));
        d.raiseDeadUnit = r.optionalString(QStringLiteral("raiseDeadUnit"));
        d.raiseDeadPercent = r.optionalInt(QStringLiteral("raiseDeadPercent"), 0, 0, 100);
        d.battleLootPercent = r.optionalInt(QStringLiteral("battleLootPercent"), 0, 0, 1000);
        d.tradeBonus = r.optionalInt(QStringLiteral("tradeBonus"), 0, 0, 50);
        d.wallDefenseBonus = r.optionalInt(QStringLiteral("wallDefenseBonus"), 0, 0, 50);
        d.visionBonus = r.optionalInt(QStringLiteral("visionBonus"), 0, 0, 10);
        d.spellSchoolPercent = r.optionalIntMap(QStringLiteral("spellSchoolPercent"), 10, 500);
        d.abilityText = r.optionalString(QStringLiteral("ability"));
        r.finish();
        if (d.id != info.completeBaseName())
            r.addError(QStringLiteral("id '%1' must match the file name").arg(d.id));
        if (!r.ok()) continue;
        m_races.insert(d.id, d);
    }
}

void ContentRegistry::loadSpells(const QString& dir, QStringList& errors) {
    const QDir spellsDir(QDir(dir).filePath(QStringLiteral("spells")));
    if (!spellsDir.exists()) return;  // a game without magic is fine
    for (const QFileInfo& info : spellsDir.entryInfoList({QStringLiteral("*.json")}, QDir::Files, QDir::Name)) {
        const QString ctx = QStringLiteral("spells/") + info.fileName();
        QJsonObject o;
        if (!readObjectFile(info.filePath(), ctx, &o, errors)) continue;
        JsonReader r(o, ctx, &errors);
        SpellDef d;
        d.id = r.requireId(QStringLiteral("id"));
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
        if (d.id != info.completeBaseName())
            r.addError(QStringLiteral("id '%1' must match the file name").arg(d.id));
        if (!r.ok()) continue;
        m_spells.insert(d.id, d);
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
                     d.startingArmy = r.optionalIntMap(QStringLiteral("startingArmy"), 1, 10000);
                     d.startingSpells = r.optionalStringList(QStringLiteral("startingSpells"));
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
        if (sr.has(QStringLiteral("engineCost"))) sb.engineCost = sr.optionalIntMap(QStringLiteral("engineCost"), 0, 1000000);
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
        EconomyBalance eb;
        eb.townDailyGold = er.requireInt(QStringLiteral("townDailyGold"), 0, 1000000);
        eb.tradeEfficiencyPercent = er.optionalInt(QStringLiteral("tradeEfficiencyPercent"), 80, 1, 100);
        eb.heroCost = er.optionalInt(QStringLiteral("heroCost"), 2500, 0, 10000000);
        eb.tavernOffers = er.optionalInt(QStringLiteral("tavernOffers"), 2, 1, 10);
        eb.maxHeroesPerPlayer = er.optionalInt(QStringLiteral("maxHeroesPerPlayer"), 8, 1, 100);
        const ResourceAmounts start = er.optionalIntMap(QStringLiteral("startingResources"), 0, 1000000000);
        const ResourceAmounts boat = er.optionalIntMap(QStringLiteral("boatCost"), 1, 1000000);
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
        for (const QString& sp : u.spells) {
            const auto it = m_spells.constFind(sp);
            if (it == m_spells.cend()) errors << QStringLiteral("unit '%1': unknown spell '%2'").arg(u.id, sp);
            else if (it->scope != SpellDef::Scope::Battle) errors << QStringLiteral("unit '%1': '%2' is not a battle spell").arg(u.id, sp);
        }
        if (!u.spells.isEmpty() && u.spellPoints <= 0)
            errors << QStringLiteral("unit '%1': has spells but no spellPoints").arg(u.id);
    }
    for (const WeekEventDef& e : m_weekEvents) {
        for (auto it = e.resources.begin(); it != e.resources.end(); ++it)
            if (!m_resources.contains(it.key())) errors << QStringLiteral("event '%1': unknown resource '%2'").arg(e.id, it.key());
        for (auto it = e.unitGrowth.begin(); it != e.unitGrowth.end(); ++it)
            if (!m_units.contains(it.key())) errors << QStringLiteral("event '%1': unknown unit '%2'").arg(e.id, it.key());
    }
    for (const ArtifactDef& a : m_artifacts) {
        for (auto it = a.traits.begin(); it != a.traits.end(); ++it)
            if (!m_traits.contains(it.key()))
                errors << QStringLiteral("artifact '%1': unknown trait '%2'").arg(a.id, it.key());
    }
    for (const RaceDef& race : m_races) {
        for (auto it = race.terrainCostPercent.begin(); it != race.terrainCostPercent.end(); ++it)
            if (!m_terrain.contains(it.key()))
                errors << QStringLiteral("race '%1': unknown terrain '%2'").arg(race.id, it.key());
    }
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
        if (!m_races.isEmpty() && !m_races.contains(c.race))
            errors << QStringLiteral("hero class '%1': race '%2' has no file in races/").arg(c.id, c.race);
        for (const QString& s : c.startingSpells) {
            if (!m_spells.contains(s))
                errors << QStringLiteral("hero class '%1': startingSpells uses unknown spell '%2'").arg(c.id, s);
        }
        for (auto it = c.startingArmy.begin(); it != c.startingArmy.end(); ++it) {
            if (!m_units.contains(it.key()))
                errors << QStringLiteral("hero class '%1': startingArmy uses unknown unit '%2'").arg(c.id, it.key());
        }
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

    for (auto it = m_balance.economy.startingResources.begin();
         it != m_balance.economy.startingResources.end(); ++it) {
        if (!m_resources.contains(it.key()))
            errors << QStringLiteral("balance.json: startingResources uses unknown resource '%1'").arg(it.key());
    }
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
QStringList ContentRegistry::raceIds() const {
    QStringList races;
    for (const HeroClassDef& c : m_heroClasses) {
        if (!races.contains(c.race)) races.append(c.race);
    }
    races.sort();
    return races;
}

const RaceDef& ContentRegistry::race(const QString& id) const {
    const auto it = m_races.constFind(id);
    return it == m_races.cend() ? m_defaultRace : it.value();
}

const WeekEventDef* ContentRegistry::weekEvent(const QString& id) const {
    const auto it = m_weekEvents.constFind(id);
    return it == m_weekEvents.cend() ? nullptr : &it.value();
}

const ArtifactDef* ContentRegistry::artifact(const QString& id) const {
    const auto it = m_artifacts.constFind(id);
    return it == m_artifacts.cend() ? nullptr : &it.value();
}

const SpellDef* ContentRegistry::spell(const QString& id) const {
    const auto it = m_spells.constFind(id);
    return it == m_spells.cend() ? nullptr : &it.value();
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
