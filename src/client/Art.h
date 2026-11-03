#pragma once

#include <QColor>
#include <QIcon>
#include <QPainterPath>
#include <QPixmap>
#include <QRectF>
#include <QList>
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
// The colour a terrain's ground is drawn in (and spreads to its neighbours with).
QColor groundColor(const QString& terrainId, const QColor& base);
// Where two terrains meet: a soft band of the neighbour's colour reaching into a
// tile from one side (0 left, 1 top, 2 right, 3 bottom), slightly wavy; `foam`
// draws a light surf line instead (water next to land).
const QPixmap& terrainEdge(const QColor& colour, int side, int size, bool foam);
// A small decoration for a map tile (flowers, stones, a bush, reeds, a cactus,
// mushrooms...) chosen by the terrain and `variant`; null pixmap = nothing here.
// decorationAt says whether (and which) a tile gets one (about one tile in six).
const QPixmap& decoration(const QString& terrainId, int variant, int size);
int decorationAt(int x, int y);  // -1 = none, else a variant
// The unexplored world: dark drifting clouds, a seamless texture to tile with a
// brush (align its origin with the map so it scrolls along).
const QPixmap& fogTexture();
// What stands on a forest, hills or mountain tile (trees, mounds, peaks), drawn
// bigger than the tile so it overlaps its neighbours: a 2x2-tile pixmap whose
// bottom middle is the tile itself (draw it at the tile's corner minus (size/2,
// size)). Null for flat terrain. Draw these row by row, top to bottom.
const QPixmap& terrainFeature(const QString& terrainId, const QColor& base, int size, int variant);
// Coasts: where a water tile touches land, the land reaches into it with a wavy,
// sandy, foamy edge. Directions 0-3 are the sides (left, top, right, bottom), 4-7
// the corners (top-left, top-right, bottom-right, bottom-left); `mask` has bit d set
// when the land is in direction d. shoreUnder is the foam and beach for a mask;
// shorePiece is the land's shape from one direction (fill it with the land's tile).
const QPixmap& shoreUnder(int mask, int variant, int size);
QPainterPath shorePiece(int direction, int variant, int size);
// Which of two neighbouring terrains spreads over the other (higher wins).
int terrainLayer(const QString& terrainId);

// Map zoom: tile sizes in pixels, smallest first, and the default.
inline constexpr int kZoomSteps[] = {16, 24, 32, 48, 64, 96};
inline constexpr int kDefaultTile = 48;
int nextZoom(int tile, int direction);  // the next step up (+1) or down (-1)

// A map icon drawn once at a large master size (with a dark outline so it reads on
// any terrain), then scaled down smoothly to `size` pixels and cached under `key`.
const QPixmap& sprite(const QString& key, int size, const std::function<void(QPainter&, const QRectF&)>& draw);
// The cached sprites of the map objects (size = the tile; a town covers 2x2 tiles).
const QPixmap& townSprite(const QColor& owner, int size);
// What a town looks like from outside (TownArt.cpp): its race's stone and roofs,
// how big it is (number of buildings) and its walls (0 palisade, 1 stone, 2 citadel).
struct TownLook {
    QColor owner;
    QString race;
    int walls = 0;
    int size = 0;
};
void drawTownIcon(QPainter& p, const QRectF& r, const TownLook& look);
const QPixmap& townSprite(const TownLook& look, int size);
// The picture on the town screen: one figure per building, walls in front.
struct SceneBuilding {
    enum class Kind { Hall, MageGuild, Dwelling, Tavern, Market, Granary, Watchtower, Workshop, Other, Shipyard, Walls };
    Kind kind = Kind::Other;
    int level = 0;   // dwellings: unit rank (taller when trained), mage guild: its level, hall: 0 village, 1 town, 2 city
    QString unit;    // dwellings: the unit
};
void drawTownScene(QPainter& p, const QRectF& r, const TownLook& look, const QList<SceneBuilding>& buildings);
const QPixmap& mineSprite(const QString& resource, const QColor& resourceColor, const QColor& owner, int size);
const QPixmap& dwellingSprite(const QColor& owner, int size);
const QPixmap& dwellingSprite(const QColor& owner, const QString& unitId, int size);  // with the unit on a sign
const QPixmap& heroSprite(const QColor& owner, bool inBoat, int size);
const QPixmap& monsterSprite(int size);
const QPixmap& chestSprite(const QColor& resource, int size);
const QPixmap& artifactSprite(int size);
// A resource's colour and a small icon for it (coin, logs, stone, gem, orb).
QColor resourceColor(const QString& id);
const QPixmap& resourceIcon(const QString& id, int size);
// A hero's portrait (PortraitArt.cpp): headgear by class, in the owner's colours;
// `seed` (the hero id) varies the face a little.
void drawPortrait(QPainter& p, const QRectF& r, const QString& classId, const QColor& owner, int seed);
const QPixmap& portrait(const QString& classId, const QColor& owner, int seed, int size);
// Small icons for buttons and tabs: "build", "buy", "wagon", "end", "spell",
// "market", "defend", "wait", "auto", "retreat", "catapult", "town", "army", "tavern".
QIcon buttonIcon(const QString& name);
// Mouse pointers for the adventure map.
enum class Pointer { Normal, Move, MoveLater, Attack, Enter, Select, Blocked, MoveAttack, Shoot };
QPixmap pointerImage(Pointer kind);

void drawTown(QPainter& p, const QRectF& r, const QColor& owner);
void drawMine(QPainter& p, const QRectF& r, const QColor& resource, const QColor& owner);
// Special mines and resource symbols (IconArt.cpp).
void drawSawmill(QPainter& p, const QRectF& r, const QColor& owner);
void drawQuarry(QPainter& p, const QRectF& r, const QColor& owner);
void drawCrystalCave(QPainter& p, const QRectF& r, const QColor& crystal, const QColor& owner);
void drawResourceIcon(QPainter& p, const QRectF& r, const QString& id, const QColor& c);
// An artifact's own picture, by where it is worn and its name (sword, crown, boots...).
void drawArtifactIcon(QPainter& p, const QRectF& r, const QString& id, const QString& slot);
const QPixmap& artifactIcon(const QString& id, const QString& slot, int size);
// A spell's picture: a medallion in its school's colour with what it does (SpellArt.cpp).
void drawSpellIcon(QPainter& p, const QRectF& r, const QString& id, const QString& school);
const QPixmap& spellIcon(const QString& id, const QString& school, int size);
void drawDwelling(QPainter& p, const QRectF& r, const QColor& owner);
void drawHero(QPainter& p, const QRectF& r, const QColor& owner, bool selected, bool inBoat = false);
void drawHeroRider(QPainter& p, const QRectF& r, const QColor& owner);  // the knight on horseback (UnitArt.cpp)
void drawMonster(QPainter& p, const QRectF& r);
void drawChest(QPainter& p, const QRectF& r, const QColor& resource);
void drawArtifact(QPainter& p, const QRectF& r);

// A picture of the unit (UnitArt.cpp; chosen by unit id, rank 0 regular .. 2 elite).
void drawUnitIcon(QPainter& p, const QRectF& r, const QString& unitId, int rank);
const QPixmap& unitIcon(const QString& unitId, int rank, int size);

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
