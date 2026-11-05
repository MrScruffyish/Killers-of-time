#pragma once

// Test helpers: content ids from the text keys of the data files, so tests can say
// R("wood") and amounts({{"gold", 100}}). Each test sets testContent() to its loaded
// registry in initTestCase (useContent).

#include <QtTest>
#include <initializer_list>
#include <utility>

#include "core/content/ContentRegistry.h"
#include "core/model/GameMap.h"

namespace kot::core {

inline const ContentRegistry*& testContent() {
    static const ContentRegistry* content = nullptr;
    return content;
}
inline void useContent(const ContentRegistry& content) { testContent() = &content; }

inline ResourceId R(const char* key) {
    const ResourceId id = testContent() ? testContent()->resourceId(QString::fromLatin1(key)) : ResourceId();
    if (!id.isValid()) qWarning("test: unknown resource '%s'", key);
    return id;
}

inline TerrainId T(const char* key) {
    const TerrainId id = testContent() ? testContent()->terrainId(QString::fromLatin1(key)) : TerrainId();
    if (!id.isValid()) qWarning("test: unknown terrain '%s'", key);
    return id;
}

inline UnitTypeId U(const char* key) {
    const UnitTypeId id = testContent() ? testContent()->unitId(QString::fromLatin1(key)) : UnitTypeId();
    if (!id.isValid()) qWarning("test: unknown unit '%s'", key);
    return id;
}

inline BuildingTypeId B(const char* key) {
    const BuildingTypeId id = testContent() ? testContent()->buildingId(QString::fromLatin1(key)) : BuildingTypeId();
    if (!id.isValid()) qWarning("test: unknown building '%s'", key);
    return id;
}

inline SpellId S(const char* key) {
    const SpellId id = testContent() ? testContent()->spellId(QString::fromLatin1(key)) : SpellId();
    if (!id.isValid()) qWarning("test: unknown spell '%s'", key);
    return id;
}

inline ArtifactId A(const char* key) {
    const ArtifactId id = testContent() ? testContent()->artifactId(QString::fromLatin1(key)) : ArtifactId();
    if (!id.isValid()) qWarning("test: unknown artifact '%s'", key);
    return id;
}

inline HeroClassId C(const char* key) {
    const HeroClassId id = testContent() ? testContent()->heroClassId(QString::fromLatin1(key)) : HeroClassId();
    if (!id.isValid()) qWarning("test: unknown hero class '%s'", key);
    return id;
}

inline HeroTemplateId H(const char* key) {
    const HeroTemplateId id = testContent() ? testContent()->heroTemplateId(QString::fromLatin1(key)) : HeroTemplateId();
    if (!id.isValid()) qWarning("test: unknown hero '%s'", key);
    return id;
}

inline TraitId TR(const char* key) {
    const TraitId id = testContent() ? testContent()->traitId(QString::fromLatin1(key)) : TraitId();
    if (!id.isValid()) qWarning("test: unknown trait '%s'", key);
    return id;
}

inline RaceId RC(const char* key) {
    const RaceId id = testContent() ? testContent()->raceId(QString::fromLatin1(key)) : RaceId();
    if (!id.isValid()) qWarning("test: unknown race '%s'", key);
    return id;
}

// The terrain keys of every tile, row-major.
inline QStringList terrainKeys(const GameMap& map) {
    QStringList keys;
    for (const TerrainId t : map.cells()) keys << testContent()->terrain(t).key;
    return keys;
}

inline ResourceAmounts amounts(std::initializer_list<std::pair<const char*, int>> list) {
    ResourceAmounts out;
    for (const auto& [key, amount] : list) out[R(key)] = amount;
    return out;
}

}  // namespace kot::core
