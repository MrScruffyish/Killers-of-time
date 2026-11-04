#include "core/model/GameMap.h"

#include <algorithm>

namespace kot::core {

GameMap::GameMap(int width, int height, const QString& terrainId)
    : m_width(std::max(0, width)),
      m_height(std::max(0, height)),
      m_tiles(static_cast<qsizetype>(m_width) * m_height, terrainId) {}

GameMap GameMap::fromTiles(int width, int height, QList<QString> tiles) {
    GameMap map(width, height, QString());
    if (tiles.size() == map.m_tiles.size()) map.m_tiles = std::move(tiles);
    return map;
}

}  // namespace kot::core
