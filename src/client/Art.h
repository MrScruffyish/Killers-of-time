#pragma once

#include <QColor>
#include <QPixmap>
#include <QRectF>
#include <QString>

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
