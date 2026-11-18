#pragma once

#include <QStringList>

namespace kot::core {

class ContentRegistry;

// Structural checks of the content graphs (used when loading data and by kot_graph):
//  - loops in building requirements (requires + upgradeOf),
//  - loops in unit upgrades (upgradesTo),
//  - buildings that can never be built: everything they need (transitively) must fit
//    their race, and nothing they need may exclude another thing they need or themselves.
// Each entry is one readable problem; empty = fine.
QStringList findBuildingLoops(const ContentRegistry& content);
QStringList findUnitUpgradeLoops(const ContentRegistry& content);
QStringList findUnbuildableBuildings(const ContentRegistry& content);
QStringList checkContentGraphs(const ContentRegistry& content);

}  // namespace kot::core
