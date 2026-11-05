#pragma once

// Where every picture lives in the art folder (data/art), shared by the client, which
// only loads pictures, and the art generator (src/artgen, kot_artexport), which draws
// them. A path is without ".png"; a picture with the owner's colour in it has a second
// file "<path>.tint.png" (see art::ownerSprite). Square pictures are kMasterSize big.

#include <QColor>
#include <QList>
#include <QString>

namespace kot::client::art {

inline constexpr int kMasterSize = 512;       // square pictures (units, icons, tiles, ...)
inline constexpr int kFeatureMasterSize = 1024;  // trees, hills, peaks: 2x2 tiles

namespace path {
inline QString unit(const QString& key, int rank) { return QStringLiteral("unit/%1/%2").arg(key).arg(rank); }
inline QString dwelling() { return QStringLiteral("dwelling"); }
inline QString dwelling(const QString& unit) { return QStringLiteral("dwelling/") + unit; }
inline QString town() { return QStringLiteral("town"); }
// The town on the map: race, walls (0-3) and stage (0 village .. 3 city).
inline QString town(const QString& race, int walls, int stage) { return QStringLiteral("town/%1/%2_%3").arg(race).arg(walls).arg(stage); }
inline QString mine(const QString& resource) { return QStringLiteral("mine/") + resource; }
inline QString hero(const QString& heroClass, bool boat) { return QStringLiteral("hero/%1/%2").arg(heroClass, boat ? "boat" : "foot"); }
inline QString monster() { return QStringLiteral("monster"); }
inline QString chest(const QString& resource) { return QStringLiteral("chest/") + (resource.isEmpty() ? QStringLiteral("treasure") : resource); }
inline QString artifactOnMap() { return QStringLiteral("artifact"); }
inline QString artifact(const QString& key) { return QStringLiteral("artifact/") + key; }
inline QString site(const QString& site, bool glowing) { return QStringLiteral("site/%1_%2").arg(site).arg(glowing ? 1 : 0); }
inline QString spell(const QString& key) { return QStringLiteral("spell/") + key; }
inline QString resource(const QString& key) { return QStringLiteral("res/") + key; }
inline QString button(const QString& name) { return QStringLiteral("button/") + name; }
inline QString pointer(const QString& name) { return QStringLiteral("pointer/") + name; }
inline QString portrait(const QString& heroClass, int face) { return QStringLiteral("portrait/%1/%2").arg(heroClass).arg(face); }
inline QString terrainTile(const QString& terrain, int variant) { return QStringLiteral("terrain/%1/tile%2").arg(terrain).arg(variant); }
inline QString terrainEdge(const QString& terrain, int side) { return QStringLiteral("terrain/%1/edge%2").arg(terrain).arg(side); }
inline QString foamEdge(int side) { return QStringLiteral("terrain/foam%1").arg(side); }
inline QString decoration(const QString& terrain, int variant) { return QStringLiteral("terrain/%1/decor%2").arg(terrain).arg(variant); }
inline QString feature(const QString& terrain, int variant) { return QStringLiteral("terrain/%1/feature%2").arg(terrain).arg(variant); }
inline QString fog() { return QStringLiteral("terrain/fog"); }
inline QString shoreUnder(int mask, int variant) { return QStringLiteral("shore/under_%1_%2").arg(mask).arg(variant); }
inline QString shoreLand(int direction, int variant) { return QStringLiteral("shore/land_%1_%2").arg(direction).arg(variant); }
inline QString sceneBackdrop(const QString& race, bool water) { return QStringLiteral("townscene/%1/backdrop%2").arg(race, water ? "_water" : ""); }
inline QString sceneBuilding(const QString& race, const QString& name) { return QStringLiteral("townscene/%1/%2").arg(race, name); }
inline QString sceneWalls(const QString& race, int walls) { return QStringLiteral("townscene/%1/walls%2").arg(race).arg(walls); }
inline QString sceneBoat(const QString& race) { return QStringLiteral("townscene/%1/boat").arg(race); }
inline QString arrow(bool up) { return up ? QStringLiteral("ui/arrow_up") : QStringLiteral("ui/arrow_down"); }
}  // namespace path

// The number of looks of a few things (variants chosen by position or hero id).
inline constexpr int kTileVariants = 4;
inline constexpr int kDecorationVariants = 6;
inline constexpr int kPortraitFaces = 6;
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
    static const QStringList names = {"shrine", "magic_well", "obelisk", "arena", "learning_stone"};
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
    case SceneBuilding::Kind::Dwelling:  // with a sign for its unit, when it has one
        return QStringLiteral("dwelling%1%2").arg(std::clamp(b.level, 0, 2)).arg(b.unit.isEmpty() ? "" : "_sign");
    case SceneBuilding::Kind::Tavern: return QStringLiteral("tavern");
    case SceneBuilding::Kind::Market: return QStringLiteral("market");
    case SceneBuilding::Kind::Granary: return QStringLiteral("granary");
    case SceneBuilding::Kind::Watchtower: return QStringLiteral("watchtower");
    case SceneBuilding::Kind::Workshop: return QStringLiteral("workshop_") + b.unit;
    default: return QStringLiteral("other");
    }
}
// Where a dwelling's sign (with its unit) is, relative to the building's base middle,
// in units: centre x, centre y, radius.
inline double dwellingHeight(int level) { return 0.6 + 0.18 * level; }

}  // namespace kot::client::art
