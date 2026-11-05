#include "core/model/GameMap.h"

#include <algorithm>

namespace kot::core {

GameMap::GameMap(int width, int height, TerrainId terrain)
    : m_width(std::max(0, width)), m_height(std::max(0, height)), m_cells(static_cast<qsizetype>(m_width) * m_height, terrain) {}

GameMap GameMap::fromCells(int width, int height, QVector<TerrainId> cells) {
    GameMap map(width, height, TerrainId());
    if (cells.size() == map.m_cells.size()) map.m_cells = std::move(cells);
    return map;
}

}  // namespace kot::core
