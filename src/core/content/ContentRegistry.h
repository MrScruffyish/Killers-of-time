#pragma once

#include <QHash>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

#include "core/content/Defs.h"

namespace kot::core {

// All game content loaded from the data directory:
//   resources.json, terrain.json, traits.json, hero_classes.json,
//   progression.json, balance.json, units/<id>.json, buildings/<id>.json, heroes.json
// See docs/data-format.md. Loading either fully succeeds or leaves the
// registry empty and returns every problem found in `errors`.
class ContentRegistry {
public:
    bool loadFromDirectory(const QString& dataDir, QStringList* errors);
    void clear();

    // Every kind of content is a list sorted by key; an id is a position in it.
    const QVector<ResourceDef>& resources() const { return m_resources; }
    const ResourceDef& resource(ResourceId id) const { return m_resources[id.index()]; }
    // The id of a key from the data or a save (invalid when there is none): for loading
    // and tools, not for game code (that keeps ids).
    ResourceId resourceId(const QString& key) const;
    // The currency (gold): exactly one resource is.
    ResourceId currency() const { return m_currency; }
    const QVector<TerrainDef>& terrain() const { return m_terrain; }
    const TerrainDef& terrain(TerrainId id) const { return m_terrain[id.index()]; }
    TerrainId terrainId(const QString& key) const;
    // By key (art and tools name terrains by key): null when there is none.
    const TerrainDef* terrainByKey(const QString& key) const {
        const TerrainId id = terrainId(key);
        return id.isValid() ? &terrain(id) : nullptr;
    }
    const QVector<TraitDef>& traits() const { return m_traits; }
    // The traits the rules name (attack, scouting, ...).
    const KnownTraits& known() const { return m_known; }
    const QVector<UnitDef>& units() const { return m_units; }
    // Null for an invalid id. unitId / unitByKey: by the key in the data (loading, tools).
    const UnitDef* unit(UnitTypeId id) const {
        return id.isValid() && id.index() < m_units.size() ? &m_units[id.index()] : nullptr;
    }
    UnitTypeId unitId(const QString& key) const { return UnitTypeId(m_unitIndex.value(key, -1)); }
    const UnitDef* unitByKey(const QString& key) const { return unit(unitId(key)); }
    const QVector<BuildingDef>& buildings() const { return m_buildings; }
    const BuildingDef* building(BuildingTypeId id) const {
        return id.isValid() && id.index() < m_buildings.size() ? &m_buildings[id.index()] : nullptr;
    }
    BuildingTypeId buildingId(const QString& key) const { return BuildingTypeId(m_buildingIndex.value(key, -1)); }
    const BuildingDef* buildingByKey(const QString& key) const { return building(buildingId(key)); }
    const QVector<HeroTemplateDef>& heroTemplates() const { return m_heroTemplates; }
    const QVector<HeroClassDef>& heroClasses() const { return m_heroClasses; }
    // xpThresholds()[i] = total experience needed to reach level i + 2.
    const QList<int>& xpThresholds() const { return m_xpThresholds; }
    const Balance& balance() const { return m_balance; }
    // The folder the data was loaded from (scenarios live in its scenarios/ folder).
    const QString& dataDir() const { return m_dataDir; }
    // A fingerprint of every content key in id order: saves and network messages carry
    // ids as numbers, which only mean the same with the same checksum.
    const QString& checksum() const { return m_checksum; }
    // Playable races: the races that have a hero class (in id order), and their keys.
    QList<RaceId> playableRaces() const;
    QStringList playableRaceKeys() const;
    int maxLevel() const { return static_cast<int>(m_xpThresholds.size()) + 1; }

    const TraitDef* trait(TraitId id) const { return id.isValid() && id.index() < m_traits.size() ? &m_traits[id.index()] : nullptr; }
    TraitId traitId(const QString& key) const;
    const HeroClassDef* heroClass(HeroClassId id) const {
        return id.isValid() && id.index() < m_heroClasses.size() ? &m_heroClasses[id.index()] : nullptr;
    }
    HeroClassId heroClassId(const QString& key) const;
    const HeroClassDef* heroClassByKey(const QString& key) const { return heroClass(heroClassId(key)); }
    const HeroTemplateDef* heroTemplate(HeroTemplateId id) const {
        return id.isValid() && id.index() < m_heroTemplates.size() ? &m_heroTemplates[id.index()] : nullptr;
    }
    HeroTemplateId heroTemplateId(const QString& key) const;
    const SpellDef* spell(SpellId id) const { return id.isValid() && id.index() < m_spells.size() ? &m_spells[id.index()] : nullptr; }
    SpellId spellId(const QString& key) const { return SpellId(m_spellIndex.value(key, -1)); }
    const SpellDef* spellByKey(const QString& key) const { return spell(spellId(key)); }
    // Tools only (kot_balance_gui): change a loaded unit's numbers in memory.
    void replaceUnit(const UnitDef& u) { if (unit(u.id)) m_units[u.id.index()] = u; }
    void replaceResource(const ResourceDef& r) { if (r.id.isValid() && r.id.index() < m_resources.size()) m_resources[r.id.index()] = r; }
    const ArtifactDef* artifact(ArtifactId id) const {
        return id.isValid() && id.index() < m_artifacts.size() ? &m_artifacts[id.index()] : nullptr;
    }
    ArtifactId artifactId(const QString& key) const;
    const ArtifactDef* artifactByKey(const QString& key) const { return artifact(artifactId(key)); }
    const QVector<WeekEventDef>& weekEvents() const { return m_weekEvents; }
    const WeekEventDef* weekEvent(WeekEventId id) const {
        return id.isValid() && id.index() < m_weekEvents.size() ? &m_weekEvents[id.index()] : nullptr;
    }
    WeekEventId weekEventId(const QString& key) const;
    const QVector<ArtifactDef>& artifacts() const { return m_artifacts; }
    // The race's data, or defaults (no bonuses) for an invalid id.
    const RaceDef& race(RaceId id) const { return id.isValid() && id.index() < m_races.size() ? m_races[id.index()] : m_defaultRace; }
    RaceId raceId(const QString& key) const { return RaceId(m_raceIndex.value(key, -1)); }
    const QVector<RaceDef>& races() const { return m_races; }
    const QVector<SpellDef>& spells() const { return m_spells; }

private:
    void loadResources(const QString& dir, QStringList& errors);
    void loadTerrain(const QString& dir, QStringList& errors);
    void loadTraits(const QString& dir, QStringList& errors);
    void loadUnits(const QString& dir, QStringList& errors);
    void loadHeroTemplates(const QString& dir, QStringList& errors);
    void loadBuildings(const QString& dir, QStringList& errors);
    void loadHeroClasses(const QString& dir, QStringList& errors);
    void loadSpells(const QString& dir, QStringList& errors);
    void loadRaces(const QString& dir, QStringList& errors);
    void loadArtifacts(const QString& dir, QStringList& errors);
    void loadWeekEvents(const QString& dir, QStringList& errors);
    void loadProgression(const QString& dir, QStringList& errors);
    void loadBalance(const QString& dir, QStringList& errors);
    void validateReferences(QStringList& errors) const;

    QVector<ResourceDef> m_resources;
    ResourceId m_currency;
    // Text amounts from the data ({"gold": 100}) as ids; unknown keys are errors.
    ResourceAmounts toResources(const QMap<QString, int>& amounts, const QString& context, QStringList& errors) const;
    QVector<TerrainDef> m_terrain;
    QVector<TraitDef> m_traits;
    KnownTraits m_known;
    // Text trait values from the data ({"attack": 2}) as ids; unknown keys are errors.
    TraitValues toTraits(const QMap<QString, int>& values, const QString& context, QStringList& errors) const;
    QVector<UnitDef> m_units;
    QHash<QString, int> m_unitIndex;  // key -> id (from the file names, before reading them)
    QVector<HeroClassDef> m_heroClasses;
    QVector<BuildingDef> m_buildings;
    QHash<QString, int> m_buildingIndex;  // key -> id (from the file names)
    QVector<HeroTemplateDef> m_heroTemplates;
    QVector<SpellDef> m_spells;
    QHash<QString, int> m_spellIndex;  // key -> id (from the file names, read first: units name spells)
    // A race key from the data as an id; a key without a file in races/ is an error.
    RaceId toRace(const QString& key, const QString& context, QStringList& errors) const;
    QVector<RaceDef> m_races;
    QHash<QString, int> m_raceIndex;  // key -> id (from the file names, read first: units name races)
    QVector<ArtifactDef> m_artifacts;
    QVector<WeekEventDef> m_weekEvents;
    RaceDef m_defaultRace;
    QString m_dataDir;
    QString m_checksum;
    QList<int> m_xpThresholds;
    Balance m_balance;
};

}  // namespace kot::core
