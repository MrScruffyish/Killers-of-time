#pragma once

#include <QColor>
#include <QPixmap>
#include <QRectF>
#include <QString>
#include <functional>

class QPainter;

namespace kot::client {

// Placeholder art drawn in code (decision D7), a step up from flat colours: terrain
// textures made from the terrain's data colour (grass tufts, trees, peaks, waves,
// dunes, ...), and simple icons for map objects. Deterministic, cached per tile size.
namespace art {

// A terrain tile; `variant` (0..3) picks one of a few looks so the map is not a grid
// of copies. The terrain id chooses the pattern, `base` is the data colour.
const QPixmap& terrainTile(const QString& terrainId, const QColor& base, int size, int variant);
int variantFor(int x, int y);

// Map zoom: tile sizes in pixels, smallest first, and the default.
inline constexpr int kZoomSteps[] = {16, 24, 32, 48, 64, 96};
inline constexpr int kDefaultTile = 48;
int nextZoom(int tile, int direction);  // the next step up (+1) or down (-1)

// A map icon drawn once at a large master size (with a dark outline so it reads on
// any terrain), then scaled down smoothly to `size` pixels and cached under `key`.
const QPixmap& sprite(const QString& key, int size, const std::function<void(QPainter&, const QRectF&)>& draw);
// The cached sprites of the map objects (size = the tile; a town covers 2x2 tiles).
const QPixmap& townSprite(const QColor& owner, int size);
const QPixmap& mineSprite(const QString& resource, const QColor& resourceColor, const QColor& owner, int size);
const QPixmap& dwellingSprite(const QColor& owner, int size);
const QPixmap& heroSprite(const QColor& owner, bool inBoat, int size);
const QPixmap& monsterSprite(int size);
const QPixmap& chestSprite(const QColor& resource, int size);
const QPixmap& artifactSprite(int size);
// Mouse pointers for the adventure map.
enum class Pointer { Normal, Move, MoveLater, Attack, Enter, Select, Blocked };
QPixmap pointerImage(Pointer kind);

void drawTown(QPainter& p, const QRectF& r, const QColor& owner);
void drawMine(QPainter& p, const QRectF& r, const QColor& resource, const QColor& owner);
void drawDwelling(QPainter& p, const QRectF& r, const QColor& owner);
void drawHero(QPainter& p, const QRectF& r, const QColor& owner, bool selected, bool inBoat = false);
void drawMonster(QPainter& p, const QRectF& r);
void drawChest(QPainter& p, const QRectF& r, const QColor& resource);
void drawArtifact(QPainter& p, const QRectF& r);

// A battle stack token: a round shield in the side's colour with the unit name and count.
void drawUnitToken(QPainter& p, const QPointF& centre, double radius, const QColor& side, const QString& name,
                   int count, int stars, bool active, bool target);

// The look of the whole client (a dark wood-and-gold style sheet).
QString styleSheet();

}  // namespace art
}  // namespace kot::client
