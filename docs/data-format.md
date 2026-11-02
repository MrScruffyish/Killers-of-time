# Data file format

Game content lives in `data/` as JSON and is loaded by
`kot::core::ContentRegistry` (`src/core/content/`). Loading is strict: every
problem is reported with file name and field, and a failed load leaves the
registry empty.

General rules:
- **Ids** are lowercase: start with a letter, then letters, digits, `_` (e.g. `battle_snail`).
- **Unknown fields are errors** (catches typos like `atack`). Fields starting with `_`
  are ignored, so use `"_comment": "..."` for notes.
- Integers must be whole numbers within the documented range.

## resources.json
```json
{ "resources": [ { "id": "gold", "name": "Gold", "currency": true }, ... ] }
```
Exactly one resource must have `"currency": true` (its value is always 1).
`value` (optional, default 100) is the worth in gold, used when trading resources.

## terrain.json
```json
{ "terrain": [ { "id": "grass", "name": "Grass", "moveCost": 100, "color": "#5fa04e" }, ... ] }
```
| Field | Type | Default | Notes |
|---|---|---|---|
| id, name | string | required | |
| color | `#RRGGBB` | required | placeholder art |
| walkable | bool | true | enterable on foot |
| sailable | bool | false | enterable by boat |
| moveCost | int 1..10000 | 100 | 100 = one standard tile (road 70, forest 150) |
| blocksVision | bool | false | |

Mountains = `walkable: false, sailable: false`. Rivers/sea = `walkable: false, sailable: true`.

## traits.json
```json
{ "traits": [ { "id": "luck", "name": "Luck", "group": "battle", "pickable": true } ] }
```
`group`: `battle`, `magic` or `explore`. `pickable`: can be chosen at level-up
(`false` for traits that grow automatically). `description` is optional.
Trait value tables and hero-class growth come in a later slice.

## units/<id>.json (one file per unit; the id must equal the file name)
| Field | Type | Default | Notes |
|---|---|---|---|
| id, name | string | required | |
| race | id | required | race files do not exist yet |
| tier | int 1..7 | required | |
| attack, defense | int 0..1000 | required | |
| damageMin, damageMax | int | required | min <= max |
| maxHp | int 1..100000 | required | |
| battleSpeed | int 1..100 | required | hexes per move in battle |
| mapSpeed | int 1..100 | = battleSpeed | adventure map speed class |
| initiative | int 1..100 | = battleSpeed | turn order in battle |
| range, shots | int | 0, 0 | ranged unit: range > 0 and shots >= 1 |
| size | 1 or 2 | 1 | hexes occupied |
| weeklyGrowth | int | 0 | |
| spellPoints | int | 0 | unit's own spell point pool |
| cost | object | {} | resource id -> amount; ids must exist |
| abilities | string list | [] | e.g. `"flying"` (not yet validated) |
| upgradesTo | id | none | must be another existing unit |

## hero_classes.json
```json
{ "heroClasses": [ { "id": "knight", "name": "Knight", "race": "humans",
    "baseTraits": { "attack": 3, "spell_points_max": 10 },
    "pickIncrement": { "attack": 2 },
    "autoGrowth": { "spell_points_max": 4 } } ] }
```
| Field | Notes |
|---|---|
| baseTraits | trait id -> starting value (0..1000); missing traits start at 0 |
| pickIncrement | trait id -> added by one level-up pick (1..100); missing = 1; trait must be pickable |
| autoGrowth | trait id -> added automatically on every level-up (e.g. `spell_points_max`) |
| startingArmy | unit id -> count: what the hero of this class starts a new game with |

All trait ids must exist in `traits.json`.

## progression.json
```json
{ "xpThresholds": [1000, 2000, 3200] }
```
`xpThresholds[i]` is the total experience needed to reach level `i + 2` (level 1 starts at 0).
Must be strictly ascending. Max level = number of thresholds + 1.

## balance.json
Tunable numbers for the rules.
```json
{ "vision": { "townRadius": 5, "mineRadius": 2 }, "economy": { "townDailyGold": 500 },
  "movement": { "base": 1000, "perTraitLevel": 100, "minimum": 300,
    "slowPenalties": [ { "mapSpeedAtMost": 1, "penalty": 300 }, ... ] } }
```
Daily movement of an army = `base + perTraitLevel * (hero Movement Points trait)`, minus the
penalty of the first `slowPenalties` step whose `mapSpeedAtMost` is >= the slowest unit's
`mapSpeed` (steps must ascend), but never below `minimum`. All values are in hundredths of a
movement point (100 = one grass tile).

`vision.townRadius` / `mineRadius`: how many tiles an owned town / mine reveals to its team
(heroes use their Scouting Radius trait). `economy.townDailyGold`: gold a town yields per day.
`economy.tradeEfficiencyPercent` (optional, default 80): share of the fair value you get when
trading resources (the rest is the trading fee).

`battle` (optional, every field optional): `maxRounds` (30, then a draw), `experiencePerHp`
(winner's hero XP per HP of killed enemies), `attackPercentPerPoint` / `maxAttackPercent`
(damage bonus per point of attack above the target's defense, capped), `defensePercentPerPoint` /
`maxDefensePercent` (damage reduction per point below). Hero Attack/Defense traits add to the units.

## buildings/<id>.json (one file per building; the id must equal the file name)
```json
{ "id": "archery_range", "name": "Archery Range", "race": "humans",
  "cost": { "gold": 500, "wood": 5 }, "requires": ["barracks"], "excludes": [],
  "effects": [ { "type": "dwelling", "unit": "archer" } ] }
```
| Field | Notes |
|---|---|
| race | only towns of this race can build it |
| cost | resource id -> amount, paid when built |
| requires | buildings that must already be built |
| excludes | buildings that make this one unavailable (and vice versa): a forced choice |
| upgradeOf | a dwelling building this one replaces; the old dwelling's unit must `upgradesTo` this one's unit. The base building must be built first. |
| effects | `{"type":"tavern"}`: heroes can be hired here. `{"type":"dwelling","unit":id}`: units can be recruited, weekly growth from the unit's `weeklyGrowth`. `{"type":"income","resource":id,"amount":n}`: daily income for the owner. |

A town can build one building per day.

## heroes.json
Heroes that can be hired in a tavern. The class decides the race a hero belongs to.
```json
{ "heroes": [ { "id": "arthur", "name": "Arthur", "class": "knight" } ] }
```
`balance.json` economy: `heroCost` (gold, default 2500), `tavernOffers` (heroes offered at a time,
default 2), `maxHeroesPerPlayer` (default 8).
The building effect `{"type": "tavern"}` lets a town hire heroes.

`balance.json` economy also has `startingResources` (resource id -> amount each player starts with).

## spells/<id>.json (one file per spell; the id must equal the file name; the folder is optional)
```json
{ "id": "magic_arrow", "name": "Magic Arrow", "school": "air", "level": 1, "cost": 5, "requiredMagic": 1,
  "scope": "battle", "target": "enemy", "description": "...",
  "effects": [ { "type": "damage", "amount": 10, "perPower": 10 } ] }
```
| Field | Notes |
|---|---|
| level | the mage guild level that teaches it |
| cost | spell points |
| requiredMagic | the hero's Magic trait must be at least this (default: level) |
| scope | `battle` (needs `target`: `enemy` or `ally`) or `adventure` (no target) |
| effects | strength = `amount + perPower * Spell Power`. Battle: `damage`, `heal` (cannot revive), `modifyStat` (`stat`: attack / defense / battleSpeed, `rounds`; negative amount = a curse). Adventure: `movement` (extra movement points, hundredths), `reveal` (explores the radius around the hero). |

A hero casts one battle spell per round (it does not use the stack's turn). Heroes learn spells
in a town of their team with a `{"type": "mageGuild", "level": n}` building (all spells up to
that level, when they start their turn there or walk in). Hero classes may have
`startingSpells`.

## races/<id>.json (one file per race; the folder is optional)
```json
{ "id": "undead", "name": "Undead", "description": "...",
  "growthPercent": 25, "incomePercent": -20, "terrainCostPercent": { "swamp": 50 },
  "spellSchools": ["dark", "earth"] }
```
| Field | Notes |
|---|---|
| growthPercent | weekly growth of the race's town dwellings, +/- percent (at least 1 unit) |
| incomePercent | base gold of the race's towns (`townDailyGold`), +/- percent |
| terrainCostPercent | terrain id -> percent of the move cost, for heroes whose class has this race |
| spellSchools | schools its mage guilds teach (empty = all) |

Units, buildings and hero classes name their race. When the folder exists, every hero class's
race needs a file. A building without `race` can be built by every race (tavern, village hall,
mage guilds).

## Unit abilities (`abilities` in units/<id>.json)
`flying` (moves over stacks and rocks, lands on a free hex), `no_retaliation` (its targets do not
strike back), `double_attack` (strikes or shoots twice), `undead` (ignores morale). Unknown
abilities are errors.

## balance.json "battle" (more)
`moralePercentPerPoint` (chance per hero Morale point for an extra action; negative morale: chance
to falter), `luckPercentPerPoint` (chance per Luck point for double damage),
`hpPercentPerTraitLevel` (hero Hit Points: +% max HP for his units, in battle only), `obstacles`
(rocks on the battle field).

## artifacts.json (optional)
```json
{ "artifacts": [ { "id": "iron_sword", "name": "Iron Sword", "description": "+2 Attack",
                   "traits": { "attack": 2 }, "value": 1 } ] }
```
Artifacts lie on the map (guarded by monsters; `value` 1..3 makes the guard stronger). The hero who
walks onto one keeps it and its trait bonuses are added to his traits.

## balance.json "difficulty" and "aiSlotDifficulty"
`difficulty`: one entry per overall level (`easy`, `normal`, `hard`, `expert`, chosen in the lobby):
`humanStartPercent` (starting resources of humans), `monsterPercent` (neutral monster group size),
`aiIncomePercent` and `aiGrowthPercent` (+/- percent for AI players). `aiSlotDifficulty`: per AI
slot level (`easy`, `normal`, `hard`), its `aiIncomePercent` / `aiGrowthPercent` are added.
