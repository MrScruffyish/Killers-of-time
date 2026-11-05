#pragma once

// The art generator: every picture of the game drawn in code (placeholder art,
// decision D7), saved as PNG files by kot_artexport. The client never draws these: it
// loads the files (client/Art.h, paths in client/ArtPaths.h), so they can be replaced
// by better pictures one by one.

#include <QColor>
#include <QImage>
#include <QPainterPath>
#include <QPixmap>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <functional>

#include "client/ArtPaths.h"

class QPainter;

namespace kot::core {
class ContentRegistry;
}

namespace kot::artgen {

using client::art::Pointer;
using client::art::SceneBuilding;
using client::art::TownLook;

// ---- export (ArtExport.cpp)
struct ExportStats {
    int files = 0;
    qint64 bytes = 0;
};
// Draws every picture the content can need and saves it under `outDir` (PNG, by the
// paths of ArtPaths.h). False (and `error`) when a file cannot be written.
bool exportAll(const core::ContentRegistry& content, const QString& outDir, ExportStats* stats, QString* error);
// Only draws everything (for timing), saves nothing.
void drawAll(const core::ContentRegistry& content);

// A map icon at master size: the drawing, light from the top left, a dark outline
// and a soft shadow (so it reads on any terrain).
QImage renderMaster(const std::function<void(QPainter&, const QRectF&)>& draw, int size = client::art::kMasterSize);
// The tint of a picture with the owner's colour: drawn with white minus drawn with
// black as that colour (how much of the colour each pixel takes).
QImage tintOf(const QImage& black, const QImage& white);
// Like QColor::lighter(percent), but a straight blend towards white (1 - 100/percent
// of the way), which is linear in the colour: use it for the owner's colour, whose
// pictures are put together as base + tint * colour (client art::ownerSprite).
QColor towardsWhite(const QColor& c, int percent);

// ---- terrain (MapArt.cpp)
// The colour a terrain's ground is drawn in (and spreads to its neighbours with).
QColor groundColor(const QString& terrainId, const QColor& base);
// A terrain tile, `s` pixels; `variant` (0..3) picks one of a few looks. The terrain id
// chooses the pattern, `dataColour` is its colour from the data.
QPixmap drawTerrainTile(const QString& terrainId, const QColor& dataColour, int s, int variant);
// A small decoration for a tile (flowers, stones, a bush, reeds...), if the terrain
// has one for the variant (0-5).
bool hasDecoration(const QString& terrainId, int variant);
void drawDecoration(QPainter& p, const QRectF& r, const QString& terrainId, int variant);
// The unexplored world: dark drifting clouds, seamless.
QImage drawFog(int size);
// Trees, hills and peaks standing out of a tile: a 2x2-tile canvas (2s square) whose
// bottom middle is the tile.
bool hasFeature(const QString& terrainId);
QPixmap drawTerrainFeature(const QString& terrainId, const QColor& base, int s, int variant);
// Coasts (see client art::shoreUnder): the land's shape from one direction, and the
// foam and beach under a set of directions.
QPainterPath shorePiece(int direction, int variant, int size);
QImage drawShoreUnder(int mask, int variant, int size);
QImage drawShoreLand(int direction, int variant, int size);  // white where the land is
// Where two terrains meet: a soft band of the neighbour's colour reaching into a tile
// from one side (0 left, 1 top, 2 right, 3 bottom); `foam`: a surf line instead.
QPixmap drawTerrainEdge(const QColor& colour, int side, int size, bool foam);

// ---- map objects (MapArt.cpp, IconArt.cpp, TownArt.cpp, UnitArt.cpp)
QColor resourceColor(const QString& id);
void drawTown(QPainter& p, const QRectF& r, const QColor& owner);
void drawTownIcon(QPainter& p, const QRectF& r, const TownLook& look);
void drawMine(QPainter& p, const QRectF& r, const QColor& resource, const QColor& owner);
void drawSawmill(QPainter& p, const QRectF& r, const QColor& owner);
void drawQuarry(QPainter& p, const QRectF& r, const QColor& owner);
void drawCrystalCave(QPainter& p, const QRectF& r, const QColor& crystal, const QColor& owner);
void drawResourceIcon(QPainter& p, const QRectF& r, const QString& id, const QColor& c);
void drawDwelling(QPainter& p, const QRectF& r, const QColor& owner);
void drawHero(QPainter& p, const QRectF& r, const QColor& owner, bool selected, bool inBoat = false,
              const QString& heroClass = QStringLiteral("knight"));
void drawHeroRider(QPainter& p, const QRectF& r, const QColor& owner);  // the knight on horseback
// A hero of a class on his mount: wizards with a hat and staff, orcs on wolves, dwarves on bears...
void drawHeroRider(QPainter& p, const QRectF& r, const QColor& owner, const QString& heroClass);
void drawMonster(QPainter& p, const QRectF& r);
void drawChest(QPainter& p, const QRectF& r, const QColor& resource);
void drawArtifact(QPainter& p, const QRectF& r);
// An artifact's own picture, by where it is worn and its name (sword, crown, boots...).
void drawArtifactIcon(QPainter& p, const QRectF& r, const QString& id, const QString& slot);
// A place to visit (shrine, magic_well, ...); a visited one without its glow.
void drawSite(QPainter& p, const QRectF& r, const QString& site, bool glowing);
// A spell's picture: a medallion in its school's colour with what it does.
void drawSpellIcon(QPainter& p, const QRectF& r, const QString& id, const QString& school);
// A unit (chosen by unit id, rank 0 regular .. 2 elite), and the same smaller in its
// square by `scale` (unitScale: weaker units smaller), standing on the ground line.
void drawUnitIcon(QPainter& p, const QRectF& r, const QString& unitId, int rank);
double unitScale(int tier, int rank);
void drawUnitPicture(QPainter& p, const QRectF& r, const QString& unitId, int rank, double scale);
// A hero's portrait: headgear by class, in the owner's colours; `seed` varies the face.
void drawPortrait(QPainter& p, const QRectF& r, const QString& classId, const QColor& owner, int seed);
void drawFramedPortrait(QPainter& p, const QRectF& r, const QString& classId, const QColor& owner, int seed);
// Icons for buttons and tabs ("build", "buy", ..., buttonNames()).
void drawButtonIcon(QPainter& p, const QRectF& box, const QString& name);
// Mouse pointers (drawn for kPointerSize, scaled to `size`) and the style sheet's arrows.
QImage drawPointer(Pointer kind, int size);
QImage drawArrow(bool up);

// ---- the town screen (TownArt.cpp), drawn in scene coordinates (ArtPaths.h)
void drawSceneBackdrop(QPainter& p, const QRectF& r, const QString& race, bool water);
// A building whose base line has its middle at (cx, base); `unit` its size.
void drawSceneBuilding(QPainter& p, double cx, double base, double unit, const SceneBuilding& b, const TownLook& look);
void drawSceneBoat(QPainter& p, const QRectF& r, const QString& race);
void drawSceneWalls(QPainter& p, const QRectF& r, const TownLook& look, int walls);

}  // namespace kot::artgen
