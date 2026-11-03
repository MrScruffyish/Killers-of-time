#pragma once

#include <QList>
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
    int value = 100;          // worth in gold (the currency itself is 1); used for trading
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

// A hero class gives a hero its starting trait values and how traits grow.
//   baseTraits:    trait id -> starting value (missing = 0)
//   pickIncrement: trait id -> how much one level-up pick adds (missing = 1)
//   autoGrowth:    trait id -> added automatically on every level-up
//                  (e.g. spell_points_max)
//   startingArmy:  units the hero of this class starts a new game with
struct HeroClassDef {
    QString id;
    QString name;
    QString race;
    QMap<QString, int> baseTraits;
    QMap<QString, int> pickIncrement;
    QMap<QString, int> autoGrowth;
    QMap<QString, int> startingArmy;  // unit id -> count a new game hero starts with
    QStringList startingSpells;       // spells every hero of this class knows from the start
};

// Daily movement of an army: base + perTraitLevel * hero Movement Points trait,
// minus a small penalty for the slowest unit, but at least `minimum`.
struct SlowPenaltyStep {
    int mapSpeedAtMost = 0;
    int penalty = 0;
};
struct MovementBalance {
    int base = 1000;
    int perTraitLevel = 100;
    int minimum = 100;
    QList<SlowPenaltyStep> slowPenalties;  // ascending by mapSpeedAtMost
};
struct VisionBalance {
    int townRadius = 5;  // tiles a town reveals to its owner's team
    int mineRadius = 2;
};
struct EconomyBalance {
    int townDailyGold = 500;  // base income of every owned town (buildings add more later)
    int tradeEfficiencyPercent = 80;  // you get this share of the fair value when trading
    int heroCost = 2500;              // gold to hire a hero in a tavern
    int tavernOffers = 2;             // heroes a tavern offers at a time
    int maxHeroesPerPlayer = 8;
    ResourceAmounts startingResources{{"gold", 10000}};
    ResourceAmounts boatCost{{"gold", 1000}, {"wood", 10}};
};
// Auto-resolved battles (core/battle/AutoBattle.h). Percentages per point of
// attack minus defense, with caps.
struct BattleBalance {
    int maxRounds = 30;              // then the battle is a draw
    int experiencePerHp = 1;         // winner's hero XP per HP of killed enemy units
    int attackPercentPerPoint = 5;
    int maxAttackPercent = 300;
    int defensePercentPerPoint = 3;
    int maxDefensePercent = 70;
    int moralePercentPerPoint = 5;   // chance per hero Morale point: extra action (or, negative, falter)
    int luckPercentPerPoint = 5;     // chance per hero Luck point: double damage
    int hpPercentPerTraitLevel = 5;  // hero Hit Points trait: +% max HP of his units
    int obstacles = 4;               // rocks on the battle field
};
// The overall difficulty level of a game (lobby setting) and the per-AI slot level.
struct DifficultyLevel {
    int humanStartPercent = 100;  // starting resources of human players
    int monsterPercent = 100;     // size of neutral monster groups
    int aiIncomePercent = 0;      // +/- percent income for AI players
    int aiGrowthPercent = 0;      // +/- percent dwelling growth in AI towns
};
// Unit veterancy: experience per unit -> 0..5 stars, each star adds its bonus
// (cumulative). Stats: attack, defense, initiative, battleSpeed, morale.
struct VeterancyBalance {
    int xpPercent = 50;                        // per unit: killed enemy HP x percent / surviving units
    QList<int> thresholds{20, 60, 150, 350, 800};  // experience for stars 1..5
    QList<QMap<QString, int>> starBonuses{{{"morale", 1}}, {{"initiative", 1}}, {{"battleSpeed", 1}},
                                          {{"attack", 1}}, {{"defense", 1}}};
};
// The Scouting trait and zones of control. A hostile army on the map (a monster
// or an enemy hero, not in a town) attacks a hero who comes within
// zoneOfControlRadius tiles of it, minus one tile per levelsPerZoneStep levels of
// Scouting, never below zoneOfControlMinRadius. Scouting also widens vision and
// adds movement.
struct ScoutingBalance {
    int visionPerLevel = 1;
    int movementPercentPerLevel = 5;
    int zoneOfControlRadius = 2;
    int zoneOfControlMinRadius = 1;
    int levelsPerZoneStep = 2;
};
struct Balance {
    MovementBalance movement;
    ScoutingBalance scouting;
    VisionBalance vision;
    EconomyBalance economy;
    BattleBalance battle;
    VeterancyBalance veterancy;
    QMap<QString, DifficultyLevel> difficulty;  // easy, normal, hard, expert
    QMap<QString, DifficultyLevel> aiSlot;      // easy, normal, hard: added for that AI
    const DifficultyLevel level(const QString& name) const { return difficulty.value(name); }
};

struct BuildingEffect {
    enum class Type {
        Dwelling,  // units can be recruited here (`unit`), growing weekly
        Income,    // daily `amount` of `resource` for the owner
        Tavern,    // heroes can be hired in this town
        MageGuild, // heroes in this town learn all spells up to `amount` (the guild level)
        Defense,   // the town's defenders get +`amount` defense in battle (walls)
        Market,    // its owner trades at +`amount` percent better rates (less fee)
        Shipyard   // heroes in the town can buy a boat (coastal towns only)
    };
    Type type = Type::Income;
    QString unit;
    QString resource;
    int amount = 0;
};

// A race (faction): its bonuses and weaknesses as data (PROJECT.md 5.4).
// Units, buildings and hero classes name their race; a race without a file gets
// these defaults.
struct RaceDef {
    QString id;
    QString name;
    QString description;
    int growthPercent = 0;   // weekly growth of the race's town dwellings, +/- percent
    int incomePercent = 0;   // base gold of the race's towns, +/- percent
    QMap<QString, int> terrainCostPercent;  // terrain id -> move cost percent for the race's heroes
    QStringList spellSchools;  // schools its mage guilds teach (empty = all)
};

// A building a town of `race` can build (empty race = every race). All `requires` must be built, none
// of the `excludes` may be built (either way round), and `upgradeOf` (a dwelling
// building) must be built: this building then replaces it with the upgraded unit.
struct BuildingDef {
    QString id;
    QString name;
    QString race;
    ResourceAmounts cost;
    QStringList requiredBuildings;
    QStringList excludes;
    QString upgradeOf;
    QList<BuildingEffect> effects;

    bool fitsRace(const QString& townRace) const { return race.isEmpty() || race == townRace; }
};

// One building block of a spell (PROJECT.md 5.9). Its strength is
//   amount + perPower * caster's Spell Power.
// Battle: Damage (enemy stack), Heal (own stack, cannot revive), ModifyStat
// (`stat` attack / defense / battleSpeed changed for `rounds` rounds, negative = a curse).
// Adventure map: Movement (extra movement points, hundredths), Reveal (explores
// tiles within the strength as radius around the hero).
struct SpellEffect {
    enum class Type { Damage, Heal, ModifyStat, Movement, Reveal };
    Type type = Type::Damage;
    int amount = 0;
    int perPower = 0;
    QString stat;
    int rounds = 0;
};

struct SpellDef {
    enum class Scope { Battle, Adventure };
    enum class Target { None, Enemy, Ally };  // battle spells: whom they are cast on
    QString id;
    QString name;
    QString school;
    QString description;
    int level = 1;           // mage guild level that teaches it
    int cost = 1;            // spell points
    int requiredMagic = 1;   // hero Magic trait needed to cast it
    Scope scope = Scope::Battle;
    Target target = Target::None;
    QList<SpellEffect> effects;
};

// Found on the map; the hero who picks it up keeps it and gets its trait bonuses.
struct ArtifactDef {
    QString id;
    QString name;
    QString description;
    QMap<QString, int> traits;  // trait id -> bonus
    int value = 1;              // rough worth, 1 (minor) .. 3 (major); stronger guards
};

// Something that happens to everyone at the start of a week (events.json).
struct WeekEventDef {
    QString id;
    QString name;
    QString description;
    int weight = 10;                  // how often it is drawn, relative to the others
    ResourceAmounts resources;        // given to (or, negative, taken from) every player
    int growthPercent = 0;            // this week's dwelling growth, +/- percent
    QMap<QString, int> unitGrowth;    // unit id -> extra units in every dwelling of it
};

// A hero that can be hired in a tavern. The class decides the race.
struct HeroTemplateDef {
    QString id;
    QString name;
    QString classId;
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
    QStringList spells;    // battle spells the unit can cast with it
    ResourceAmounts cost;
    // flying (moves over other stacks and rocks), no_retaliation (its targets do not
    // strike back), double_attack (strikes / shoots twice), undead (ignores morale)
    QStringList abilities;
    bool has(const QString& ability) const { return abilities.contains(ability); }
    QString upgradesTo;     // unit id of the upgraded version, or empty
    // Computed when loading: 0 regular, 1 advanced, 2 elite (steps of upgradesTo
    // that lead to this unit). Different ranks are different units and never merge.
    int rank = 0;
};

}  // namespace kot::core
