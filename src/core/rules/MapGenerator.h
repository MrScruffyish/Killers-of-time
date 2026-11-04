#pragma once

#include <QString>
#include <cstdint>

#include "core/model/GameMap.h"

namespace kot::core {

class ContentRegistry;

struct MapGenParams {
    int width = 64;
    int height = 64;
    std::uint64_t seed = 1;
    double seaLevel = 0.32;       // fraction of the elevation range that is sea
    double hillFraction = 0.16;       // share of all tiles that are hills/snow
    double mountainFraction = 0.06;   // share of all tiles that are mountains
    int rivers = 2;               // rivers running from mountains toward the sea
    int ridges = -1;              // mountain ranges (with passes); -1 = by map size
    int islands = 1;              // > 1: sea channels cut the land into about this many islands
};

// Generates a terrain-only test map: land in the middle, sea all around (the
// outermost ring is always sea), beaches,
// grass, forests, swamps, deserts, hills, snow, mountains and a few rivers.
// Deterministic: the same params always give the same map. Needs the terrain
// ids sea, sand, grass, dirt, forest, swamp, hills, snow, mountains, river
// in the content; otherwise returns false and fills `error`.
bool generateMap(const ContentRegistry& content, const MapGenParams& params, GameMap* out,
                 QString* error = nullptr);

}  // namespace kot::core
