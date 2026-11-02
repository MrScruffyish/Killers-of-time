#pragma once

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

    const QMap<QString, ResourceDef>& resources() const { return m_resources; }
    const QMap<QString, TerrainDef>& terrain() const { return m_terrain; }
    const QMap<QString, TraitDef>& traits() const { return m_traits; }
    const QMap<QString, UnitDef>& units() const { return m_units; }
    const QMap<QString, BuildingDef>& buildings() const { return m_buildings; }
    const QMap<QString, HeroTemplateDef>& heroTemplates() const { return m_heroTemplates; }
    const QMap<QString, HeroClassDef>& heroClasses() const { return m_heroClasses; }
    // xpThresholds()[i] = total experience needed to reach level i + 2.
    const QList<int>& xpThresholds() const { return m_xpThresholds; }
    const Balance& balance() const { return m_balance; }
    // Playable races: the races that have a hero class.
    QStringList raceIds() const;
    int maxLevel() const { return static_cast<int>(m_xpThresholds.size()) + 1; }

    const ResourceDef* resource(const QString& id) const;
    const TerrainDef* terrainType(const QString& id) const;
    const TraitDef* trait(const QString& id) const;
    const UnitDef* unit(const QString& id) const;
    const HeroClassDef* heroClass(const QString& id) const;
    const BuildingDef* building(const QString& id) const;
    const HeroTemplateDef* heroTemplate(const QString& id) const;
    const SpellDef* spell(const QString& id) const;
    // The race's data, or defaults (no bonuses) for a race without a file.
    const RaceDef& race(const QString& id) const;
    const QMap<QString, RaceDef>& races() const { return m_races; }
    const QMap<QString, SpellDef>& spells() const { return m_spells; }
    const ResourceDef* currency() const;

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
    void loadProgression(const QString& dir, QStringList& errors);
    void loadBalance(const QString& dir, QStringList& errors);
    void validateReferences(QStringList& errors) const;

    QMap<QString, ResourceDef> m_resources;
    QMap<QString, TerrainDef> m_terrain;
    QMap<QString, TraitDef> m_traits;
    QMap<QString, UnitDef> m_units;
    QMap<QString, HeroClassDef> m_heroClasses;
    QMap<QString, BuildingDef> m_buildings;
    QMap<QString, HeroTemplateDef> m_heroTemplates;
    QMap<QString, SpellDef> m_spells;
    QMap<QString, RaceDef> m_races;
    RaceDef m_defaultRace;
    QList<int> m_xpThresholds;
    Balance m_balance;
};

}  // namespace kot::core
