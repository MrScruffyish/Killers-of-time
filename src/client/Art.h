#pragma once

#include <QColor>
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
// Where two terrains meet: a soft band of the neighbour's colour reaching into a
// tile from one side (0 left, 1 top, 2 right, 3 bottom), slightly wavy; `foam`
// draws a light surf line instead (water next to land).
const QPixmap& terrainEdge(const QColor& colour, int side, int size, bool foam);
// The unexplored world: dark drifting clouds, a seamless texture to tile with a
// brush (align its origin with the map so it scrolls along).
const QPixmap& fogTexture();
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
    enum class Kind { Hall, MageGuild, Dwelling, Tavern, Market, Other, Shipyard, Walls };
    Kind kind = Kind::Other;
    int level = 0;   // dwellings: unit rank (taller when trained), mage guild: its level
    QString unit;    // dwellings: the unit
};
void drawTownScene(QPainter& p, const QRectF& r, const TownLook& look, const QList<SceneBuilding>& buildings);
const QPixmap& mineSprite(const QString& resource, const QColor& resourceColor, const QColor& owner, int size);
const QPixmap& dwellingSprite(const QColor& owner, int size);
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
// Mouse pointers for the adventure map.
enum class Pointer { Normal, Move, MoveLater, Attack, Enter, Select, Blocked, MoveAttack, Shoot };
QPixmap pointerImage(Pointer kind);

void drawTown(QPainter& p, const QRectF& r, const QColor& owner);
void drawMine(QPainter& p, const QRectF& r, const QColor& resource, const QColor& owner);
void drawDwelling(QPainter& p, const QRectF& r, const QColor& owner);
void drawHero(QPainter& p, const QRectF& r, const QColor& owner, bool selected, bool inBoat = false);
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

// The look of the whole client (a dark wood-and-gold style sheet).
QString styleSheet();

}  // namespace art
}  // namespace kot::client
