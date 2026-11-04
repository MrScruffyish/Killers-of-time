#pragma once

#include <QList>
#include <QString>

#include "core/model/Position.h"

namespace kot::core {

// Rectangular tile grid. Each tile has a terrain id (see terrain.json).
// Objects (towns, mines, heroes) are not stored in tiles; they live in
// GameState and know their own position.
class GameMap {
public:
    GameMap() = default;
    GameMap(int width, int height, const QString& terrainId);

    int width() const { return m_width; }
    int height() const { return m_height; }
    bool inBounds(Pos p) const { return p.x >= 0 && p.y >= 0 && p.x < m_width && p.y < m_height; }

    const QString& terrainAt(Pos p) const { return m_tiles[index(p)]; }
    void setTerrain(Pos p, const QString& terrainId) { m_tiles[index(p)] = terrainId; }

    // Row-major list of all terrain ids (for saving).
    const QList<QString>& tiles() const { return m_tiles; }
    // Rebuilds a map from a row-major tile list. Size must equal width * height.
    static GameMap fromTiles(int width, int height, QList<QString> tiles);

private:
    int index(Pos p) const { return p.y * m_width + p.x; }

    int m_width = 0;
    int m_height = 0;
    QList<QString> m_tiles;
};

}  // namespace kot::core
