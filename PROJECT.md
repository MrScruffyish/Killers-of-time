# PROJECT.md: Turn-based Fantasy War Game (working title: "Killers of Time")

> Living document. Update it whenever a decision is made. Upload this file and
> STATUS.md at the start of every new Claude session.

## 1. Vision

A turn-based fantasy strategy game in the spirit of the classic hero-and-town
games. A server hosts a game; any mix of human clients and AI players join,
alone or in teams. Players explore a hidden 2D map, capture towns and mines,
build up towns, recruit armies led by heroes, cast magic, and fight tactical
turn-based battles.

## 2. Tech stack and constraints

| Item | Decision |
|---|---|
| Language | C++20 |
| UI / framework | Qt 6 (6.4+ is enough; Ubuntu apt or Qt online installer) |
| Build | CMake (+ Ninja), one top-level `CMakeLists.txt` |
| Networking | Qt Network (`QTcpServer` / `QTcpSocket`), own message protocol |
| Serialization | JSON (`QJsonDocument`) for data files, saves and (initially) network messages; binary later if needed |
| Tests | Qt Test (`QtTest`), run through CTest |
| Game content | Data-driven JSON files (races, units, buildings, spells, terrain, resources, monsters). Content is not hardcoded. |
| Platforms | Primary dev target: **Ubuntu Linux with Qt Creator** (CMake kit). Keep code portable (Windows/macOS later). |
| Client UI (initial) | Qt Widgets with `QGraphicsView`/`QGraphicsScene` for map and battle. All rendering is isolated in the client so it can be replaced (e.g. Qt Quick or 2.5D) later. |

## 3. Architecture overview

```
+--------------------+        TCP         +--------------------+
|   Client (Qt GUI)  | <----------------> |   Server (headless)|
|  - map / battle UI |  commands / events |  - authoritative   |
|  - town UI         |                    |    game state      |
|  - local view only |                    |  - rules engine    |
+--------------------+                    |  - AI players      |
                                          +--------------------+
            both link against:  core  (rules, data model, no GUI)
```

Key principles:

1. **Server is authoritative.** Clients send *commands* ("move hero 3 to
   (x,y)"). The server validates them, applies them, and broadcasts *events*
   (state changes). Clients never decide outcomes.
2. **Fog of war is enforced on the server.** Each client only receives
   information its player/team can currently see or has discovered.
   Hidden information is never sent.
3. **`core` has no GUI and no networking.** It contains the data model and
   rules, so it can be unit tested, used by the server, and used by AI.
   Single-player = local server process (or in-process server) + client.
4. **AI players are server-side "players" that use the same command interface
   as humans.** The AI sees only what a human in the same position would see
   (unless a difficulty bonus says otherwise). There is no AI-only API.
5. **Determinism.** All randomness goes through a seeded RNG owned by the
   game state (battles included), so battles and games can be replayed and
   debugged from (seed + command log).
6. **Data-driven content.** Adding a race, unit, building or spell means
   editing JSON, not C++ (new *effect types* may still need C++).
7. **Players are abstract.** A `Player` has: id, team id, controller type
   (Human / AI(profile) / Observer), race, color, difficulty modifiers.
   Teams are just an id grouping. Any mix is allowed (free-for-all, 2v2,
   humans vs AI, etc.).

## 4. Modules / repository layout

```
/
|-- CMakeLists.txt
|-- PROJECT.md  STATUS.md  README.md
|-- data/                   # game content, JSON
|   |-- resources.json
|   |-- terrain.json
|   |-- races/<race>.json
|   |-- units/<unit>.json
|   |-- buildings/<building>.json
|   |-- spells/<spell>.json
|   |-- heroes/ monsters/ artifacts/ maps/
|-- src/
|   |-- core/               # library: libcore (no GUI)
|   |   |-- model/          # GameState, Player, Map, Tile, Town, Mine, Hero, Army, Unit...
|   |   |-- rules/          # movement, economy, building, recruiting, exploration, victory
|   |   |-- battle/         # tactical battle engine (grid, initiative, damage, spells)
|   |   |-- magic/          # spell definitions and effect system
|   |   |-- data/           # JSON loaders, content registry
|   |   |-- util/           # RNG, ids, geometry, logging
|   |-- net/                # library: protocol, message types, (de)serialization
|   |-- server/             # executable: server, lobby, game session, AI host
|   |-- ai/                 # library: AI strategies (strategic + battle AI)
|   |-- client/             # executable: Qt GUI (lobby, map, town, battle, hero screens)
|   |-- mapeditor/          # (later) executable
|-- tests/                  # Qt Test, mirrors src/ layout
|-- docs/                   # protocol, data format docs
```

Dependency direction: `client -> net -> core`, `server -> net, ai -> core`.
`core` depends on nothing but Qt Core (QtCore only: `QString`, `QJson*`, etc.).

## 5. Game design

### 5.1 Turn structure

- The game proceeds in **days**/rounds. Within a day each player takes a turn.
- **Decided: sequential turns** (player 1, player 2, ...); teams share
  visibility.
- During their turn a player may move heroes (limited by movement points),
  act in towns, recruit, build (typically one building per town per day),
  etc., then ends the turn.
- Weekly/monthly tick events: unit growth in dwellings, optional random
  events. Details are tunable in data.
- No real-time elements anywhere.

### 5.2 Map

- Large rectangular **tile grid** (initial size target: up to 256x256).
  Rendered top-down 2D first; 2.5D later only changes the client.
- Each tile has: terrain type, optional *object* (town, mine, treasure,
  dwelling, decoration, monster guard), per-player explored/visible state.
- **Terrain** (data-driven in `terrain.json`) with properties:
  `passable`, `moveCost`, `requiresBoat`, `blocksVision` (optional), `battleBackground`.
  Initial set: grass, dirt, road (cheap), sand/desert (slow), forest (slow),
  hills (slow), mountains (impassable or very slow), swamp (slow),
  snow (slow), shallow water/river (ford or needs boat), sea (boat only),
  lava/void (impassable).
- **Boats:** buy/build at a shipyard; a hero boards at coast, moves on water,
  disembarks on land.
- **Fog of war:** unexplored (black), explored-but-not-visible (remembered
  terrain, objects shown as last seen), visible (live). Vision radius comes
  from heroes, towns, and some buildings/mines.
- **Map objects:** towns, mines, resource piles/treasure chests, artifacts,
  dwellings (neutral recruitment), shrines/spell-learning sites, teleporters,
  guard monsters. Many objects can be *guarded* by neutral monster stacks that
  must be defeated first.

### 5.3 Resources

| Resource | Role |
|---|---|
| gold | Currency; income from towns/mines; upkeep optional |
| wood | Basic building material |
| stone | Basic building material |
| grain | Food/upkeep, some buildings and units |
| gems, sulphur, coal, mercury, papyrus | Rare resources for advanced buildings, units, spells, artifacts |

Each player has a stockpile. **Mines** produce a fixed daily amount of one
resource for whoever owns them. Resource list is defined in `resources.json`
so it can be extended. (Cost = map `resource id -> amount`.)

### 5.4 Races (factions)

- A race defines: town type, building tree, unit roster (tiers 1..N with
  upgrades), starting hero classes, available **spell schools**, and
  **bonuses/weaknesses** (modifiers).
- Bonus/weakness examples (expressed as data modifiers, not code): +20%
  growth of tier-1 units; -10% stone from mines; +1 movement on forest;
  spell cost +15% for school X; cheaper wood buildings; weaker at sea.
- Races differ in magic: each race has access to a different set of schools
  and spells.
- Initial target: **3 races** to prove the system (e.g. Humans, Undead,
  Forest/Elves), then expand to 6-8. Names are placeholders.

### 5.5 Towns and buildings

- A town belongs to a race and an owner (or neutral). It contains a garrison
  and visiting hero slot, and has a set of built buildings.
- **Buildings** have: cost (resources), prerequisites (list of other
  buildings), **exclusive groups** (building A *or* B, not both), build
  time (days), and effects.
- Building effect types (extensible): `income`, `dwelling(unit, weekly
  growth)`, `unlock_upgrade(unit)`, `mage_guild(level)`, `defense`,
  `vision`, `shipyard`, `market` (trade resources), `hero_tavern`,
  `modifier`, etc.
- **Dependencies:** a building graph with prerequisites. Some branches are
  **mutually exclusive**, forcing strategic choices.
- **Upgrades:** better versions of buildings (e.g. a dwelling upgraded)
  allow recruiting the upgraded version of a unit, with better stats.
- Rule: typically one building action per town per day (tunable).

### 5.6 Units / soldiers

Unit definition fields (JSON): id, race, tier, name, attack, defense,
damage min/max, health, speed, initiative, shots/range, size (1 or 2
tiles), cost, weekly growth, abilities (flying, ranged, no-retaliation,
regeneration, etc.), `upgrades_to`, optional **spells** the unit can
cast (limited: mana-less with charges per battle, or small spell list).

- Classic fantasy roster: peasants/militia, archers, swordsmen, cavalry,
  priests/mages, beasts, dragons, undead, etc. per race.
- **Upgraded units** are strictly improved versions of their base unit.
- Neutral monsters are units too (with their own stacks and AI behavior).

#### 5.6.1 Stacks and hit points

- Identical units are grouped into a **stack** (e.g. 12 armoured eagles), shown
  in battle and in army screens as one icon with the **count**.
- Each unit type has `maxHp`. A stack keeps a **pooled HP total** (equivalent to
  "count + HP of the front unit"): `totalHp`, with `count = ceil(totalHp / maxHp)`.
- An attack produces an amount of **damage** (computed from attack, defense,
  stack count, modifiers). Damage is subtracted from the stack's pool; units
  are eliminated **one by one** until the loss is covered, and the front unit
  may remain wounded. `totalHp <= 0` -> stack eliminated.
- **Effective max HP per unit** = `(baseMaxHp + heroFlatBonus) * (1 + heroPercentBonus)`,
  rounded and at least 1. The hero's Hit Points trait supplies both bonus kinds.
  The formula order is tunable.
- Healing is capped at `count * effectiveMaxHp`; resurrection adds units back.
- This model lives in `core/battle` and is deterministic and unit tested.

### 5.7 Heroes

- **Hero traits** (all data-driven: hero class gives base values and growth per
  level; the exact numbers are tuned later). A trait has a *level/value*, and
  each trait has a `pickable` flag saying whether it can be chosen at level-up.

  Traits are **grouped** (the group is a `group` field in the trait data). Groups are used
  for the hero screen tabs, level-up choice UI, hero-class growth weights (e.g. a
  "warrior" class grows Battle traits faster, a "mage" class grows Magic traits) and
  for artifacts/skills that boost "all traits of a group".

  | Group | Trait | Effect | Level-up |
  |---|---|---|---|
  | **Battle** | Attack | bonus to attack of all army units in battle | pickable |
  | | Defense | bonus to defense of all army units in battle | pickable |
  | | Hit Points | bonus to the health of army units in battle (flat or % per stack, tunable) | pickable |
  | | Morale | extra/lost actions and similar modifiers; later also lowered by unpaid upkeep (units may refuse to attack or attack badly) | pickable |
  | | Luck | chance of bonus damage / avoiding a bad outcome (exact effect tuned) | pickable |
  | | Attack Range | bonus range (hexes) for ranged units such as archers; no effect on melee-only units | pickable |
  | | Battle Speed | small tiered bonus to unit speed in battle (see 5.7.1) | pickable |
  | **Magic** | Magic | skill level; each spell has a **required Magic level** to cast/learn; gates higher spell tiers | pickable |
  | | Spell Power | scales the *strength* of spells (damage/healing, buff size, e.g. a speed spell gives more speed, duration) | pickable |
  | | Spell Points (max) | size of the SP pool | **automatic** on every level-up |
  | | Spell Recovery | SP regained per day on the adventure map | pickable |
  | **Explore** | Movement Points | adventure-map movement per day (see 5.7.1) | pickable |
  | | Scouting radius | vision radius on the adventure map (fog of war) | pickable |

  Attack Range works like Battle Speed: a small, tiered bonus from a data table
  (`trait level -> bonus hexes`), possibly scaled per unit class.

  More "army helper" traits can be added later by adding data (leadership,
  logistics, etc.).
- **Experience -> levels.** XP comes from won battles (see 5.8). Level thresholds
  are data-driven. **On each level gained:**
  1. **Max Spell Points grow automatically** by a hero-class-defined amount.
  2. The player **chooses one pickable trait to boost** (per-level increment from
     the hero class data). If several levels are gained at once, one pick per level.
- **5.7.1 Speed model (two separate traits, each unit has its own speed):**
  - **Units have their own base speed**, defined in unit data: `battleSpeed`
    (hexes per move in battle, e.g. battle snail 1-2, armoured eagle 10-20) and
    `mapSpeed` (a class used on the adventure map; defaults derived from battleSpeed).
    Exact values are tuned relative to the battle map size.
  - **Battle Speed trait -> battle bonus.** The bonus is *small* and *tiered*: low
    trait levels give slow units no bonus at all; high levels give e.g. +3 hexes.
    Implemented as a data table `trait level -> bonus hexes`, optionally scaled by
    the unit's speed class (so a snail and an eagle can get different bonuses).
  - **Movement Points trait -> adventure map.** The army's daily movement points come
    mainly from the hero's Movement Points trait. The army's **slowest unit lowers it
    a little** (a capped penalty from a data table, not "slowest unit decides
    everything"). Minimum 1 movement point. Terrain move costs apply on top.
- Hero effect: stat bonuses apply to the army that hero leads (attack,
  defense, morale, etc.).
- Heroes can be **recruited** at the town tavern (cost gold, hero pool per
  race/random). A dead hero can be re-recruited.
- Army = hero + 0..N unit stacks (max stacks per army, e.g. 7). A garrison
  army may be hero-less, but **only an army containing a hero may move on
  the map and capture** (as specified). Possible later extension: hero-less
  caravans for transport only.
- Heroes carry **artifacts** (later milestone).
- A hero may have a **spellbook** with learned spells (limited by mage
  guild level, race, school skills).

### 5.8 Capturing towns/mines and map interaction

- An army **visits** an object. If it is unguarded/neutral or owned by
  another team, an *ownership change* occurs; if it has defenders (garrison
  or monsters), a **battle** occurs first. Winner takes it.
- Winning a battle gives **experience** (based on defeated creature
  strength) to the hero.

### 5.9 Magic

- **Spell schools** (placeholder list): Fire, Water, Earth, Air, Light,
  Dark/Necromancy, plus race-specific ones. Each race has access to a subset.
- Spells are data: id, school, level, spell-point cost, **required Magic level**, target type, effect list,
  scope (adventure map / battle / both), duration.
- Effect system composed of reusable **effect primitives** (damage, heal,
  buff/debuff stat, summon, resurrect, teleport, reveal map, move
  bonus...). Spells combine primitives -> mostly data, few new C++ per effect type.
- **Spell points (SP)** are the casting resource. Heroes have max SP
  (grows with level) and current SP.
- **Hero SP regeneration:** each day on the adventure map the hero regains SP equal to
  his **Spell Recovery** trait (plus bonuses from buildings/artifacts). Optional
  full refill at certain buildings (e.g. mage guild). Tunable in data.
- **Magic level requirement:** every spell has a required **Magic trait level**;
  a hero below it cannot cast (or learn) that spell.
- **Unit spells:** some units cast a limited set of spells using their own small
  SP pool. **Unit SP are fully restored between battles.**
- **Spell Power** scales the effect of a spell (hero's Spell Power trait for hero
  casts; a fixed value for unit casts).
- Adventure-map spells: e.g. town portal, view terrain, summon boat, etc.

### 5.10 Battles

- **Tactical, turn-based, hex-grid combat**. Attacker vs defender armies (+ town walls/defenses later).
- Initiative/speed determines unit order each round. Actions: move, melee,
  ranged attack, cast spell (hero once per round), wait, defend, retreat/surrender.
- Damage formula based on attack vs. defense, stack size, modifiers
  (hero, race, spells, terrain). Retaliation rules. Morale/luck optional.
- **Human vs human:** the battle is played turn-based between the two
  players over the network; both see it live. **AI vs human / AI vs AI /
  monsters:** the AI controls the AI side; a human defender may also
  watch. (Optional "auto-resolve" for AI/AI or on request.)
- Battle engine is deterministic and lives entirely in `core/battle`,
  driven by the server. Clients receive battle events to animate.

### 5.11 AI

- Two layers: **strategic AI** (map-level: explore, expand, collect,
  attack, build, recruit) and **battle AI** (tactical).
- Pluggable "AI profiles" (aggressive, economic, explorer...).
- **Difficulty** is applied as bonuses on the AI player: resource income
  multiplier, unit growth multiplier, hero stat bonus, starting resources,
  optional vision cheat (off by default). Configurable per player in the lobby.

### 5.12 Game setup / lobby

- Host creates a game: chooses map, number of slots; for each slot picks
  controller (human / AI + profile + difficulty), race, team, color.
- Clients connect, claim slots, game starts when ready.
- Save/load the full game state; reconnect support for dropped human players.

### 5.13 Victory conditions (initial)

- Last team standing (all towns and heroes lost). Others later (capture a
  specific town, accumulate resources, survive N days, etc.).

### 5.14 Design principle: avoid micromanagement

Repeating the same chore in every town every day is not fun, so the game offers
one-click actions on top of the basic rules. They are part of the rules engine
(`core/rules`), so the human UI and the AI use the same code.

- **Trade:** resources can be exchanged at rates from data (each resource has a
  gold value; a fee applies, `tradeEfficiencyPercent`). Anywhere a purchase fails
  for lack of a resource there is a **"trade as needed, then buy"** option: the
  missing resources are bought automatically with spare ones (other resources
  first, gold last).
- **Map dwellings** (outside towns) grow units too. A hero can visit and recruit
  there, or the player can use **"send all units to the closest town"** (global),
  or **"send all units to this town"** (in a town). Units sent to a town can be
  recruited there for the normal price.
- **Buy all units:** in one town, or in every town, buy as many units as possible.
  Strategy: *best first* (highest tier first, the default) or *most units*
  (cheapest first).
- **Auto-build:** build in each town, as long as there is no real choice to make.
  A building that would permanently exclude a rival building is a choice, so it
  is **flagged** for the player instead of being built. Option: use trade to
  afford buildings.
- **Build, then buy units:** auto-build followed by buy all units.
- The hero tavern, level-up picks and battles stay manual: those are the decisions.

## 6. Networking protocol (summary, details in docs/protocol.md)

- Framed messages: length prefix + UTF-8 JSON `{ "type": "...", "seq": n, ... }`.
- **Client -> server:** `Join`, `ClaimSlot`, `Ready`, `MoveHero`,
  `BuildBuilding`, `Recruit`, `TransferUnits`, `CastSpell`, `EndTurn`,
  `BattleAction`, `Chat`, ...
- **Server -> client:** `LobbyState`, `GameStart`, `TurnStart`,
  `StateDelta` (fog-filtered), `BattleStart`, `BattleEvent`, `BattleEnd`,
  `Error`, ...
- Protocol version number in the handshake.
- Every command is validated by the server and answered with accepted or
  rejected; deltas are the only way clients learn about state.

## 7. Coding conventions

- C++20, Qt 6, CMake. Format with `clang-format` (config in repo).
- Namespace `kot` (root), sub-namespaces per module (`kot::core`, `kot::battle`...).
- **Naming (current, Qt style):** types `PascalCase`, functions and variables `camelCase`,
  members `m_camelCase`, constants `kPascalCase`, files named after their main type.
  Data ids (units, buildings, traits, terrain) are `snake_case` strings; JSON field names
  are `camelCase` (`maxHp`, `battleSpeed`). Open question: switch the code to snake_case?
  (see STATUS.md)
- `core` uses only Qt Core types (`QString`, `QList`, `QHash`, `QJsonObject`).
- Strong ID types (`HeroId`, `TownId`, `PlayerId`, ...), not raw ints.
- No game logic in UI classes. UI calls commands and renders state.
- Every rule gets a unit test in `tests/` where practical.
- All randomness via the game's RNG; no `rand()`/`QRandomGenerator::global()` in rules.
- Each source file stays small and focused (so sessions can handle them as units).
- Public headers document intent; avoid cleverness.

## 8. Working agreement (how we collaborate, given usage limits)

1. Claude writes `PROJECT.md`/`STATUS.md` first; the repo is owned by the user.
2. Each session targets **one milestone slice** from `STATUS.md`.
3. Claude delivers complete files (not fragments) and a short "what changed,
   how to build/test" note. The user commits to git.
4. At the end of a session, Claude updates `STATUS.md` (done / next /
   decisions / known issues). The user replaces the file in the repo.
5. New session: user uploads `PROJECT.md`, `STATUS.md`, and the files being
   changed (or a `git diff`), and says "continue with <task>".
6. When the user changes code, they upload or paste the changed files so
   Claude works from the real state.
7. Compile errors: paste the full error output; Claude fixes them.
8. For direct repo access, running tests, and committing, consider **Claude
   Code** on the user's machine for the implementation phase.

## 9. Decisions and open questions

### Decided
| # | Question | Decision |
|---|---|---|
| D1 | Turn model | **Sequential** turns |
| D2 | Battle grid | **Hex grid** |
| D4 | Client UI tech | Widgets + QGraphicsView (may move to Qt Quick later) |
| D5 | Magic model | **Spell points** (SP): regenerate daily by the Spell Recovery trait, max SP grows automatically on level-up; Spell Power scales spell effect; Magic trait gates which spells can be cast; unit SP refill fully between battles |
| D7 | Art | **Placeholder** artwork (colored shapes/tiles, generated in code) |
| D11 | Title | **"Killers of Time"** (namespace `kot`) |
| D12 | Dev environment | Ubuntu Linux, Qt Creator, CMake |
| D13 | Level-up | Automatic max-SP growth + player picks one pickable trait per level gained |
| D14 | Speed | Two traits: **Movement Points** (map) and **Battle Speed** (battle, small tiered bonus per unit class); units have their own base speeds; slowest unit lowers army map movement only a little |

### Still open (defaults used until changed)
| # | Question | Default |
|---|---|---|
| D3 | Network format | JSON first, optimize later |
| D6 | Map size / tile size | Up to 256x256, 32 px tiles |
| D8 | Win conditions | Last team standing first |
| D9 | Unit upkeep (gold/grain per day) | **Later.** When added, unpaid upkeep lowers Morale; low morale can make units refuse to attack or attack badly |
| D18 | Do wounded stacks heal after a battle? | Survivors restored to full HP after battle; eliminated units stay dead (revisit when balancing) |
| D10 | Hot-seat mode on one machine | Later, via multiple local clients |
| D15 | Exact numbers (unit speeds, trait->bonus tables, slow-unit penalty, XP curve) | Tuned in data files during M2/M6 |
| D16 | Hit Points | **Decided:** every unit has HP; 0 HP = eliminated. Units fight in **stacks** (see 5.6.1). The hero's Hit Points trait gives a bonus to each unit's max HP, both flat (+N) and percentage; hero himself does not fight |
| D17 | Traits are grouped | **Decided:** groups Battle / Magic / Explore (data field `group`); Luck and Attack Range added |
