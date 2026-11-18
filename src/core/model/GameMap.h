#pragma once

#include <QVector>

#include "core/content/ContentIds.h"
#include "core/model/Position.h"

namespace kot::core {

// Rectangular tile grid. Each tile has a terrain (an id into the content's terrain
// list, see terrain.json). Objects (towns, mines, heroes) are not stored in tiles;
// they live in GameState and know their own position.
class GameMap {
public:
    GameMap() = default;
    GameMap(int width, int height, TerrainId terrain);

    int width() const { return m_width; }
    int height() const { return m_height; }
    bool inBounds(Pos p) const { return p.x >= 0 && p.y >= 0 && p.x < m_width && p.y < m_height; }

    TerrainId terrainAt(Pos p) const { return m_cells[index(p)]; }
    void setTerrain(Pos p, TerrainId terrain) { m_cells[index(p)] = terrain; }

    // Row-major terrain of every tile.
    const QVector<TerrainId>& cells() const { return m_cells; }
    // Rebuilds a map from a row-major tile list. Size must equal width * height.
    static GameMap fromCells(int width, int height, QVector<TerrainId> cells);

    friend bool operator==(const GameMap&, const GameMap&) = default;

private:
    int index(Pos p) const { return p.y * m_width + p.x; }

    int m_width = 0;
    int m_height = 0;
    QVector<TerrainId> m_cells;
};

}  // namespace kot::core
