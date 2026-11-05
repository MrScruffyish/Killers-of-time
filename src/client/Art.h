#pragma once

#include <QColor>
#include <QIcon>
#include <QList>
#include <QPixmap>
#include <QRectF>
#include <QString>

#include "client/ArtPaths.h"
#include "core/content/ContentIds.h"

namespace kot::core {
class ContentRegistry;
}

class QPainter;

namespace kot::client {

// The game's pictures, loaded from PNG files (data/art, paths in ArtPaths.h, made by
// kot_artexport from the drawings in src/artgen; better pictures can replace them one
// by one). Each file is a big master, scaled smoothly to the size asked for and cached.
// A missing file shows as a placeholder (a warning is printed once).
namespace art {

// Where the pictures are loaded from (data/art).
void setArtDirectory(const QString& dir);
const QString& artDirectory();
// The content, for the overloads that take content ids (pictures are named by key).
void setContent(const core::ContentRegistry* content);
QString unitKey(core::UnitTypeId unit);           // "militia"; empty when unknown
QString heroClassKey(core::HeroClassId heroClass);  // "knight"
QString raceKey(core::RaceId race);               // "humans"; empty when unknown

// A picture `size` pixels square (or w x h) from its file, and the same for a picture
// with the owner's colour in it (flags, roofs, cloaks): two files, `base` and `tint`
// (how much of the owner's colour each pixel takes), put together as
// picture = base + tint * owner.
const QPixmap& sprite(const QString& path, int size);
const QPixmap& sprite(const QString& path, int width, int height);
const QPixmap& ownerSprite(const QString& path, const QColor& owner, int size);
const QPixmap& ownerSprite(const QString& path, const QColor& owner, int width, int height);
// Whether the art folder has the picture (for the optional ones).
bool hasPicture(const QString& path);

// ---- the map
// A terrain tile; `variant` (0..3, variantFor) picks one of a few looks so the map is
// not a grid of copies.
const QPixmap& terrainTile(const QString& terrain, int size, int variant);
int variantFor(int x, int y);
// Where two terrains meet: a soft band of the neighbour's colour reaching into a tile
// from one side (0 left, 1 top, 2 right, 3 bottom).
const QPixmap& terrainEdge(const QString& neighbour, int side, int size);
// A small decoration for a tile (flowers, stones...), if the terrain has one for the
// variant; decorationAt says whether (and which) a tile gets one (one tile in six).
const QPixmap& decoration(const QString& terrain, int variant, int size);
int decorationAt(int x, int y);  // -1 = none, else a variant
// The unexplored world: a seamless texture to tile with a brush.
const QPixmap& fogTexture();
// What stands on a forest, hills or mountain tile (trees, mounds, peaks), drawn
// bigger than the tile: a 2x2-tile pixmap whose bottom middle is the tile itself
// (draw it at the tile's corner minus (size/2, size)). Null for flat terrain.
const QPixmap& terrainFeature(const QString& terrain, int size, int variant);
// Coasts: where a water tile touches land, the land reaches into it with a wavy edge.
// Directions 0-3 are the sides (left, top, right, bottom), 4-7 the corners (top-left,
// top-right, bottom-right, bottom-left); `mask` has bit d set when the land is in
// direction d. shoreUnder is the foam and beach; shoreLand the land's piece from one
// direction, in that land's tile (`landVariant`).
const QPixmap& shoreUnder(int mask, int variant, int size);
const QPixmap& shoreLand(const QString& land, int landVariant, int direction, int variant, int size);
// Which of two neighbouring terrains spreads over the other (higher wins).
int terrainLayer(const QString& terrain);
// Map zoom: tile sizes in pixels, smallest first, and the default.
inline constexpr int kZoomSteps[] = {16, 24, 32, 48, 64, 96};
inline constexpr int kDefaultTile = 48;
int nextZoom(int tile, int direction);  // the next step up (+1) or down (-1)

// Map objects (size = the tile; a town covers 2x2 tiles).
const QPixmap& townSprite(const QColor& owner, int size);
const QPixmap& townSprite(const TownLook& look, int size);
const QPixmap& mineSprite(const QString& resource, const QColor& owner, int size);
const QPixmap& dwellingSprite(const QColor& owner, int size);
const QPixmap& dwellingSprite(const QColor& owner, core::UnitTypeId unit, int size);  // with the unit on a sign
const QPixmap& heroSprite(const QColor& owner, bool inBoat, int size, core::HeroClassId heroClass);
const QPixmap& monsterSprite(int size);
const QPixmap& chestSprite(const QString& resource, int size);  // empty: a plain treasure chest
const QPixmap& artifactSprite(int size);
const QPixmap& siteSprite(const QString& site, bool glowing, int size);

// ---- icons and pictures
const QPixmap& resourceIcon(const QString& resource, int size);
const QPixmap& artifactIcon(const QString& artifact, int size);
const QPixmap& spellIcon(const QString& spell, int size);
// A unit (rank 0 regular .. 2 elite); weaker units are smaller in their square.
const QPixmap& unitIcon(const QString& unit, int rank, int size);
const QPixmap& unitIcon(core::UnitTypeId unit, int rank, int size);
// A hero's portrait in the owner's colours; `seed` (the hero id) picks the face.
const QPixmap& portrait(core::HeroClassId heroClass, const QColor& owner, int seed, int size);
// Small icons for buttons and tabs: "build", "buy", ... (buttonNames()).
QIcon buttonIcon(const QString& name);
// Mouse pointers for the adventure map.
QPixmap pointerImage(Pointer kind);

// ---- composed pictures
// The town screen: the race's backdrop, one figure per building, the walls in front.
void drawTownScene(QPainter& p, const QRectF& r, const TownLook& look, const QList<SceneBuilding>& buildings);
// A battle stack token: a round shield in the side's colour with the unit's picture
// (or its name, when `unitId` is empty), the count, stars and rank chevrons.
void drawUnitToken(QPainter& p, const QPointF& centre, double radius, const QColor& side, const QString& name,
                   int count, int stars, bool active, bool target, const QString& unitId = QString(), int rank = 0);
// A unit standing on the battle field: its picture big (about twice the hex) on a
// base in its side's colour (gold ring: acting, red ring: a target), the count on
// a plate, veterancy stars and rank chevrons. `c` is the hex centre, `r` its
// radius; `faceLeft` mirrors the picture (the defenders). Draw back rows first.
void drawBattleFigure(QPainter& p, const QPointF& c, double r, const QColor& side, const QString& unitId, int rank,
                      int count, int stars, bool active, bool target, bool faceLeft);

// The look of the whole client (a dark wood-and-gold style sheet).
QString styleSheet();

}  // namespace art
}  // namespace kot::client
