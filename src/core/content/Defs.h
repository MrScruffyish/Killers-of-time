#pragma once

#include <QMap>
#include <QString>
#include <QStringList>

namespace kot::core {

// resource id -> amount, e.g. {"gold": 100, "wood": 2}. Ordered for determinism.
using ResourceAmounts = QMap<QString, int>;

struct ResourceDef {
    QString id;
    QString name;
    bool isCurrency = false;  // exactly one resource (gold) is the currency
};

// Movement cost is in hundredths of a movement point: 100 = one standard tile.
struct TerrainDef {
    QString id;
    QString name;
    bool walkable = true;      // can be entered on foot
    bool sailable = false;     // can be entered by boat
    int moveCost = 100;
    bool blocksVision = false;
    QString color;             // "#RRGGBB", used for placeholder art
};

enum class TraitGroup { Battle, Magic, Explore };

QString traitGroupName(TraitGroup group);

struct TraitDef {
    QString id;
    QString name;
    TraitGroup group = TraitGroup::Battle;
    bool pickable = true;  // can the player choose it at level-up?
    QString description;
};

struct UnitDef {
    QString id;
    QString name;
    QString race;
    int tier = 1;
    int attack = 0;
    int defense = 0;
    int damageMin = 0;
    int damageMax = 0;
    int maxHp = 1;
    int battleSpeed = 1;   // hexes per move in battle
    int mapSpeed = 1;      // speed class on the adventure map
    int initiative = 1;    // turn order in battle
    int range = 0;         // 0 = melee only, else range in hexes
    int shots = 0;         // ammunition for ranged units
    int size = 1;          // 1 or 2 hexes
    int weeklyGrowth = 0;
    int spellPoints = 0;   // the unit's own spell point pool (0 = cannot cast)
    ResourceAmounts cost;
    QStringList abilities;  // e.g. "flying"; validated once abilities exist
    QString upgradesTo;     // unit id of the upgraded version, or empty
};

}  // namespace kot::core
