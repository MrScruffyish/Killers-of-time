#pragma once

#include <QMap>
#include <QString>
#include <QStringList>

#include "core/content/Defs.h"

namespace kot::core {

// All game content loaded from the data directory:
//   resources.json, terrain.json, traits.json, units/<id>.json
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

    const ResourceDef* resource(const QString& id) const;
    const TerrainDef* terrainType(const QString& id) const;
    const TraitDef* trait(const QString& id) const;
    const UnitDef* unit(const QString& id) const;
    const ResourceDef* currency() const;

private:
    void loadResources(const QString& dir, QStringList& errors);
    void loadTerrain(const QString& dir, QStringList& errors);
    void loadTraits(const QString& dir, QStringList& errors);
    void loadUnits(const QString& dir, QStringList& errors);
    void validateReferences(QStringList& errors) const;

    QMap<QString, ResourceDef> m_resources;
    QMap<QString, TerrainDef> m_terrain;
    QMap<QString, TraitDef> m_traits;
    QMap<QString, UnitDef> m_units;
};

}  // namespace kot::core
