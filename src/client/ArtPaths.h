#pragma once

// Where every picture lives in the art folder (data/art), shared by the client, which
// only loads pictures, and the art generator (src/artgen, kot_artexport), which draws
// them. A path is without ".png"; a picture with the owner's colour in it has a second
// file "<path>.tint.png" (see art::ownerSprite). Square pictures are kMasterSize big.

#include <QColor>
#include <QList>
#include <QPointF>
#include <QRectF>
#include <QString>

namespace kot::client::art {

inline constexpr int kMasterSize = 512;       // square pictures (units, icons, tiles, ...)
inline constexpr int kFeatureMasterSize = 1024;  // trees, hills, peaks: 2x2 tiles

// Every picture is also saved at half and a quarter of its size ("<path>@2.png",
// "<path>@4.png", the same for its tint): the client loads the smallest one that is big
// enough (a quarter decodes ~16 times faster than the master).
inline const int kSmallerCopies[] = {2, 4};
inline constexpr int kMinCopySide = 48;  // no copies of pictures smaller than this
inline QString copyPath(const QString& path, int divisor) { return QStringLiteral("%1@%2").arg(path).arg(divisor); }

namespace path {
inline QString unit(const QString& key, int rank) { return QStringLiteral("unit/%1/%2").arg(key).arg(rank); }
inline QString dwelling() { return QStringLiteral("dwelling"); }
// A dwelling with an empty sign: the client puts the unit's picture on it (dwellingSignRect).
inline QString dwellingSign() { return QStringLiteral("dwelling_sign"); }
inline QString town() { return QStringLiteral("town"); }
// The town on the map in two layers: its body by stage (0 village .. 3 city), and its
// walls (0-3, with the gate) drawn in front.
inline QString townBody(const QString& race, int stage) { return QStringLiteral("town/%1/body%2").arg(race).arg(stage); }
inline QString townWalls(const QString& race, int walls) { return QStringLiteral("town/%1/walls%2").arg(race).arg(walls); }
// Mines with their own look (sawmill, quarry, crystal cave); the others are the
// generic mine with the resource's icon (mineIconRect).
inline QString mine(const QString& resource) { return QStringLiteral("mine/") + resource; }
inline QString genericMine() { return QStringLiteral("mine/generic"); }
inline QString hero(const QString& heroClass, bool boat) { return QStringLiteral("hero/%1/%2").arg(heroClass, boat ? "boat" : "foot"); }
inline QString monster() { return QStringLiteral("monster"); }
// One chest; a treasure of one resource shows that resource's icon on it (chestIconRect).
inline QString chest() { return QStringLiteral("chest"); }
inline QString artifactOnMap() { return QStringLiteral("artifact"); }
inline QString artifact(const QString& key) { return QStringLiteral("artifact/") + key; }
// A site; one with something for the hero has the shared glow behind it.
inline QString site(const QString& site) { return QStringLiteral("site/") + site; }
inline QString siteGlow() { return QStringLiteral("site/glow"); }
inline QString spell(const QString& key) { return QStringLiteral("spell/") + key; }
inline QString resource(const QString& key) { return QStringLiteral("res/") + key; }
inline QString button(const QString& name) { return QStringLiteral("button/") + name; }
inline QString pointer(const QString& name) { return QStringLiteral("pointer/") + name; }
inline QString portrait(const QString& heroClass, int face) { return QStringLiteral("portrait/%1/%2").arg(heroClass).arg(face); }
inline QString terrainTile(const QString& terrain, int variant) { return QStringLiteral("terrain/%1/tile%2").arg(terrain).arg(variant); }
// Where two terrains meet: a white band from one side, coloured by the client with the
// neighbour's ground colour (groundColor).
inline QString terrainEdge(int side) { return QStringLiteral("terrain/edge%1").arg(side); }
inline QString decoration(const QString& terrain, int variant) { return QStringLiteral("terrain/%1/decor%2").arg(terrain).arg(variant); }
inline QString feature(const QString& terrain, int variant) { return QStringLiteral("terrain/%1/feature%2").arg(terrain).arg(variant); }
inline QString fog() { return QStringLiteral("terrain/fog"); }
inline QString shoreUnder(int mask, int variant) { return QStringLiteral("shore/under_%1_%2").arg(mask).arg(variant); }
inline QString shoreLand(int direction, int variant) { return QStringLiteral("shore/land_%1_%2").arg(direction).arg(variant); }
inline QString sceneBackdrop(const QString& race, bool water) { return QStringLiteral("townscene/%1/backdrop%2").arg(race, water ? "_water" : ""); }
inline QString sceneBuilding(const QString& race, const QString& name) { return QStringLiteral("townscene/%1/%2").arg(race, name); }
inline QString sceneWalls(const QString& race, int walls) { return QStringLiteral("townscene/%1/walls%2").arg(race).arg(walls); }
inline QString sceneBoat() { return QStringLiteral("townscene/boat"); }
inline QString sceneSign() { return QStringLiteral("townscene/sign"); }  // a dwelling's sign (the unit goes on it)
inline QString arrow(bool up) { return up ? QStringLiteral("ui/arrow_up") : QStringLiteral("ui/arrow_down"); }
}  // namespace path

// The number of looks of a few things (variants chosen by position or hero id).
inline constexpr int kTileVariants = 4;
inline constexpr int kShoreVariants = 2;  // coasts: fewer looks are plenty
inline constexpr int kDecorationVariants = 6;
inline constexpr int kPortraitFaces = 3;
inline constexpr int kMaxWalls = 3;
inline constexpr int kMaxTownStage = 3;

// The pointer kinds and their file names.
enum class Pointer { Normal, Move, MoveLater, Attack, Enter, Select, Blocked, MoveAttack, Shoot };
inline const QList<std::pair<Pointer, QString>>& pointerNames() {
    static const QList<std::pair<Pointer, QString>> names = {
        {Pointer::Normal, "normal"}, {Pointer::Move, "move"},     {Pointer::MoveLater, "move_later"},
        {Pointer::Attack, "attack"}, {Pointer::Enter, "enter"},   {Pointer::Select, "select"},
        {Pointer::Blocked, "blocked"}, {Pointer::MoveAttack, "move_attack"}, {Pointer::Shoot, "shoot"}};
    return names;
}
inline constexpr int kPointerSize = 32;  // shown size; the file is 4x

// The button icons there are (buttonIcon names).
inline const QStringList& buttonNames() {
    static const QStringList names = {"army", "auto", "build", "buy", "catapult", "defend", "end", "retreat", "spell", "tavern", "town",
                                      "wagon", "wait"};
    return names;
}
// Places to visit on the map.
inline const QStringList& siteNames() {
    static const QStringList names = {"shrine", "magic_well", "obelisk", "arena", "learning_stone", "quest_hut"};
    return names;
}

// Coasts: land in direction d (0-3 sides left, top, right, bottom; 4-7 corners
// top-left, top-right, bottom-right, bottom-left) sets bit d. A corner next to a side
// with land adds nothing, so it is dropped: 47 different shapes.
inline int canonicalShoreMask(int mask) {
    static const int sidesOfCorner[4][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 0}};
    for (int c = 0; c < 4; ++c)
        if (mask & ((1 << sidesOfCorner[c][0]) | (1 << sidesOfCorner[c][1]))) mask &= ~(1 << (4 + c));
    return mask;
}
inline QList<int> canonicalShoreMasks() {
    QList<int> masks;
    for (int m = 1; m < 256; ++m)
        if (canonicalShoreMask(m) == m) masks << m;
    return masks;
}

// What a town looks like from outside: its race's stone and roofs, how big it is
// (number of buildings) and its walls (0 palisade, 1 stone, 2 citadel).
struct TownLook {
    QColor owner;
    QString race;
    int walls = 0;
    int size = 0;
};
inline int townStage(int size) { return std::clamp(size / 3, 0, kMaxTownStage); }

// The town screen: a backdrop for the race (with water when there is a shipyard), one
// figure per building on plots left and right of the middle, the walls in front.
struct SceneBuilding {
    enum class Kind { Hall, MageGuild, Dwelling, Tavern, Market, Granary, Watchtower, Workshop, Other, Shipyard, Walls };
    Kind kind = Kind::Other;
    int level = 0;   // dwellings: unit rank (taller when trained), mage guild: its level, hall: 0 village, 1 town, 2 city
    QString unit;    // dwellings: the unit; workshops: the resource
};
// The scene's pictures are drawn for this size (and kSceneScale times bigger in the
// files); the backdrop, walls and boat are stretched to the shown size.
inline constexpr double kSceneWidth = 900, kSceneHeight = 170;
inline constexpr int kSceneScale = 3;
// A building figure: drawn in units of kSceneUnit * height, on a canvas
// kSceneCanvasW x kSceneCanvasH units whose point (kSceneAnchorX, kSceneAnchorY) is
// the middle of the building's base line.
inline constexpr double kSceneUnit = 0.42, kSceneBase = 0.84;
inline constexpr double kSceneCanvasW = 2.8, kSceneCanvasH = 3.4, kSceneAnchorX = 1.4, kSceneAnchorY = 3.1;
inline constexpr int kMaxMageGuild = 5;
// The file name of a building figure ("hall2", "dwelling1", "workshop_wood", ...).
inline QString sceneBuildingName(const SceneBuilding& b) {
    switch (b.kind) {
    case SceneBuilding::Kind::Hall: return QStringLiteral("hall%1").arg(std::clamp(b.level, 0, 2));
    case SceneBuilding::Kind::MageGuild: return QStringLiteral("mageguild%1").arg(std::clamp(b.level, 1, kMaxMageGuild));
    case SceneBuilding::Kind::Dwelling: return QStringLiteral("dwelling%1").arg(std::clamp(b.level, 0, 2));  // its sign: sceneSign
    case SceneBuilding::Kind::Tavern: return QStringLiteral("tavern");
    case SceneBuilding::Kind::Market: return QStringLiteral("market");
    case SceneBuilding::Kind::Granary: return QStringLiteral("granary");
    case SceneBuilding::Kind::Watchtower: return QStringLiteral("watchtower");
    case SceneBuilding::Kind::Workshop: return QStringLiteral("workshop");  // its resource: an icon on the heap
    default: return QStringLiteral("other");
    }
}
// Where a dwelling's sign (with its unit) is, relative to the building's base middle,
// in units: centre x, centre y, radius.
inline double dwellingHeight(int level) { return 0.6 + 0.18 * level; }
// On the town screen (units, relative to the building's base middle): a dwelling's sign
// (centre and radius; the unit's picture fills its inner square) and a workshop's heap
// (the resource's icon, a square).
inline QPointF sceneSignCentre(int level) { return {0.0, -dwellingHeight(level) - 0.1}; }
inline constexpr double kSceneSignRadius = 0.275;
inline QRectF sceneHeapRect() { return {0.1, -0.42, 0.38, 0.38}; }

// Where a detail goes on a square picture `size` big (map dwellings, mines, chests): the
// generator draws the base in the middle 88% (renderMaster), so these are fractions of
// that inner square.
inline QRectF innerRect(double size, double x, double y, double w, double h) {
    const double m = size * 0.06, s = size * 0.88;
    return {m + x * s, m + y * s, w * s, h * s};
}
inline QRectF dwellingSignRect(double size) { return innerRect(size, 0.52 + 0.48 * 0.08, 0.48 * 0.06, 0.48 * 0.84, 0.48 * 0.88); }
inline QRectF mineIconRect(double size) { return innerRect(size, 0.58, -0.02, 0.42, 0.42); }
inline QRectF chestIconRect(double size) { return innerRect(size, 0.56, -0.02, 0.4, 0.4); }
// The resources whose mine has a look of its own.
inline bool hasOwnMine(const QString& resource) { return resource == "wood" || resource == "stone" || resource == "gems"; }

// The colour a terrain's ground has (and spreads to its neighbours with): the data's
// colour, but rocky earth under mountains.
inline QColor groundColor(const QString& terrain, const QColor& base) { return terrain == "mountains" ? QColor("#8e8a6c") : base; }

}  // namespace kot::client::art
