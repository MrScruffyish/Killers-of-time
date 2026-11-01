# STATUS.md

> Update at the end of every session. Upload together with PROJECT.md at the
> start of the next one.

**Last updated:** 2026-11-01 (session 14)
**Current milestone:** M4 in progress (slice A: protocol and lobby done); M0 verified on your machine, everything since verified in sandbox
**Next session should do:** M4 slice B: start the game on the server (create GameState from the lobby: generate map from the settings, starting town + hero per player, AI players), game commands over the network (move_hero, build, recruit, trade, the one-click actions, end_turn), fog-filtered state sent to each player

## Legend
`[ ]` todo  `[~]` in progress  `[x]` done  `[?]` needs a decision from the user

---

## M0: Skeleton and tooling
- [x] Top-level CMake project (Qt 6, C++20, Ninja), `src/` + `tests/` layout; must open and build in Qt Creator on Ubuntu
- [x] Libraries/targets: `core`, `net`, `ai`; executables: `server`, `client`
- [x] Qt Test + CTest wiring with one trivial test
- [x] `.clang-format`, `.gitignore`, `README.md` (build instructions)
- [x] Logging helper, strong-ID types, seeded RNG wrapper in `core/util`
- [x] Client opens an empty window; server starts and listens (no logic yet)

> M0 verified: builds and runs on the user's Qt Creator (Ubuntu, Qt 6.4.2).

## M1: Data model and content loading
- [x] JSON schemas/docs in `docs/`: `docs/data-format.md` (resources, terrain, traits, units, hero classes, progression), `docs/save-format.md`. Buildings/spells/races docs come with M3/M7/M8.
- [x] `ContentRegistry` loader with validation and helpful errors (`JsonReader`, strict, collects all errors)
- [x] Trait system: trait definitions (`group`, `pickable`), hero classes with base values, per-pick increments and auto-growth. Tiered bonus tables (battle speed, attack range, movement) come with M2/M6.
- [x] Stack/HP model (`core/model/UnitStack`: pooled totalHp, count derived, damage elimination one by one, heal, add units, flat+percent hero HP bonus via `effectiveMaxHp`; see PROJECT.md 5.6.1) with unit tests (`tst_unitstack`)
- [x] Core model: `GameMap`, `Player`, team ids, `Hero`, `Army` (max 7 stacks), `UnitStack`, `Town`, `Mine`, `GameState` (ids unique across kinds, turn order, seeded RNG)
- [x] Hero rules (`core/rules/HeroRules`): create hero, experience and levels (xp curve in `progression.json`), one pending trait pick per level, auto max-SP growth, daily spell point regeneration
- [x] Sample data: 9 resources, 12 terrain types, 13 traits, 5 units, 2 hero classes (knight, wizard), 10 levels. Race and building data move to M3/M8.
- [x] Save/load of `GameState` to JSON (validated against content, exact RNG state preserved, atomic file save)
- [x] Unit tests: `tst_jsonreader`, `tst_content`, `tst_army`, `tst_herorules`, `tst_gamestate` (8 test programs in total)

## M2: Map and adventure rules (single machine, no network yet)
- [x] Map generation (`core/rules/MapGenerator`): deterministic from a seed, land in the middle and sea all around, beaches, forests, swamps, deserts, hills, snow, mountains, rivers; highlands as shares of the map
- [x] Pathfinding (`core/rules/Pathfinding`): A* and reachable-tiles search, 8 directions (diagonal x1.41), terrain costs, impassable terrain, Foot/Boat modes, blocked tiles (goal exempt), per-mode cost grid. Boarding/disembarking comes with shipyards (M3).
- [x] Hero movement (`core/rules/Movement`): daily points from Movement Points trait + base, small capped penalty for the slowest unit (`balance.json`), `refreshHeroMovement`, `moveHero` (moves as far as points allow). Objects block the way and cannot be entered yet; visiting them is slice B.
- [x] Fog of war (`core/rules/Vision`): per team, unexplored / explored (remembered) / visible; heroes see their Scouting Radius, owned towns and mines also give vision, teammates share; explored memory is saved. Terrain does not block vision yet.
- [x] Mines: capture by visiting (neutral or other team), daily income. Towns: visiting captures empty ones, defended ones refuse until battles exist. Resource piles/treasures move to M8 together with monsters and guards.
- [x] Day/turn cycle (`core/rules/TurnCycle`): `beginGame`, `endTurn`, `startTurn` (income from day 2: 500 gold per town + mine output, spell point regen, movement refresh, exploration update), sequential turns, new day after the last player
- [x] Tests: `tst_pathfinding`, `tst_movement`, `tst_mapgen`, `tst_vision`, `tst_turncycle` (13 test programs in total)

## M3: Towns, buildings, recruiting
- [x] Building graph (`buildings/<id>.json`, `core/rules/TownRules`): costs, `requires`, `excludes` (either way round), one build per town per day, race check, owner check, `whyCannotBuild` for the UI; `income` building effects feed the daily income. Sample: 6 humans buildings, with a forced choice (Eagle Roost or Snail Pen, placeholder units).
- [x] Dwellings and recruiting: a new dwelling starts with one week of units, growth every 8th/15th/... day, `recruit` into the garrison or a hero army standing in the town, cost paid, room checked first
- [x] Unit upgrades: a building with `upgradeOf` replaces the base dwelling; the stock is kept and the upgraded unit is recruited (validated against `upgradesTo` in the unit data)
- [x] Hero tavern (`core/rules/Tavern`, `heroes.json`, building effect `tavern`): offers 2 heroes per tavern town (drawn with the game RNG, redrawn weekly and when a tavern is built, never a hero already in the game, different between towns in one draw), `hireHero` costs 2500 gold (optionally trading spare resources), one hero in a town at a time, max 8 heroes per player, hired heroes remember their template so dead ones can return. Tests: `tst_tavern` (19 test programs in total).
- [x] Army management (`core/rules/ArmyRules::moveUnits`): move part or all of a stack between armies on the same tile; merge into existing stacks, max 7; wounds stay with the rest when splitting. `UnitStack::split/merge`.
- [x] Convenience actions (design principle "avoid micromanagement", PROJECT.md 5.14):
  - [x] Trade (`core/rules/Trade`): resource gold values in `resources.json`, fee `tradeEfficiencyPercent`; plain `trade`; `planTradeFor` sells spare resources (other resources first, most valuable spare stock first, gold last) and never what the cost itself needs; `buildBuildingWithTrade` ("buy, but first trade")
  - [x] Map dwellings (`MapDwelling`): grow weekly, captured by visiting, `recruitAtDwelling` for a hero standing there; `sendDwellingUnitsToTown` ("send all units to this town") and `sendDwellingUnitsToClosestTown` (global); sent units are recruitable in the town (used before the dwelling stock)
  - [x] `buyAllUnits` (one town or all; BestFirst = highest tier first (default) or MostUnits = cheapest first)
  - [x] `autoBuild` (best affordable building without a choice; buildings that would shut out a rival are flagged in `needsChoice`; optional trade) and `buildThenBuyUnits`
  - [ ] Later: a Market building could improve the trade efficiency; buy-all with trade; AI uses these actions too
- [x] Tests: `tst_trade`, `tst_dwellings`, `tst_automation` (18 test programs in total); `tst_towns`, `tst_armyrules`, split/merge cases in `tst_unitstack`, building loading cases in `tst_content` (15 test programs in total)

## M4: Networking and lobby
- [x] Framed JSON protocol, versioned handshake (`docs/protocol.md`; `net/Message`, `MessageFramer`, `Connection`)
- [x] Server lobby (`server/Lobby`, pure logic): slots human/AI/open/closed, race, team, colour, ready, host, map settings, start rules; `GameServer` runs it over TCP (`kot_server_lib`, `kot_server` finds `data/` itself or `--data`)
- [x] Client side: `net/LobbyClient` (used by the temporary client window, which logs the lobby)
- [x] Tests: `tst_framer`, `tst_lobby`, `tst_datadir`, `tst_network` (real server and clients on localhost, protocol violations); 23 test programs in total
- [ ] Start the game: GameState from the lobby (map from settings, start town + hero per player, AI hosts)
- [ ] Commands -> validation -> state change -> fog-filtered updates broadcast; EndTurn and turn order over the network
- [ ] Client: real connect and lobby screens (M5 has the map)
- [ ] Disconnect/reconnect handling during a game

## M5: Playable adventure map client
- [ ] Map view (QGraphicsView): terrain tiles, objects, fog rendering, scrolling/zoom
- [ ] Hero selection, path preview, move
- [ ] Resource bar, day counter, end-turn button
- [ ] Town screen (buildings, recruit) with the one-click actions: "Buy (trade first if needed)", "Send all units to this town", "Buy all units", "Auto-build"
- [ ] Global toolbar buttons: "Send all units to closest town", "Buy all units", "Build, then buy units" (flags towns that need a building choice)
- [ ] Minimap

## M6: Battle engine (core, then UI)
- [x] Decision D2: hex grid
- [ ] Battle state, initiative order, move/melee/ranged, retaliation, damage formula
- [ ] Hero trait bonuses (attack, defense, hit points, morale, luck, attack range, battle speed tier tables) applied to army
- [ ] Unit data: `battleSpeed` (hexes), `mapSpeed`, `range` (for ranged units)
- [ ] Neutral monster guards trigger battles
- [ ] Experience after victory -> hero levels (auto max-SP growth + choose one pickable trait per level gained)
- [ ] Battle UI in the client; human-vs-human live battle over network
- [ ] Deterministic battle replay test (seed + actions)

## M7: Magic
- [ ] Effect primitive system and spell data format
- [ ] Hero spellbook, spell points (max, current, daily regen via Spell Recovery trait), mage guild levels, race access to schools
- [ ] Magic trait level requirement per spell
- [ ] Battle spells; adventure-map spells
- [ ] Unit-cast spells (limited; unit SP refilled fully between battles)
- [ ] Spell Power scaling of effects
- [ ] Spell UI

## M8: Races and balance
- [ ] Race modifier system (bonuses/weaknesses as data)
- [ ] 3 complete races (full unit tiers, building trees, schools)
- [ ] Neutral monsters, treasures, artifacts (basic)

## M9: AI
- [ ] AI player host on the server (same command interface as humans)
- [ ] Battle AI (basic: attack nearest/weakest, use ranged, cast spells)
- [ ] Strategic AI v1: explore, collect, capture mines, build, recruit, attack weak targets
- [ ] AI profiles and difficulty bonuses (income, growth, hero stats)
- [ ] Teams in mixed human/AI games, victory conditions

## M10: Polish and extras
- [ ] Save/load game over server, reconnect
- [ ] Map editor
- [ ] Better art, sound, 2.5D projection decision
- [ ] Balance pass, playtesting, packaging/installers

---

## Decisions log
| Date | Decision |
|---|---|
| 2026-11-01 | Sequential turns (D1); hex battle grid (D2); spell-point magic (D5); placeholder art (D7); title "Killers of Time" (D11); Ubuntu + Qt Creator; one trait boost per level-up (D13) |
| 2026-11-01 | Added traits: Spell Recovery, Magic (gates spells), Hit Points; split Speed into Movement Points + Battle Speed (D14); units have own speeds; slowest unit only slightly lowers army map movement; auto max-SP + one picked trait per level (D13) |
| 2026-11-01 | Added Luck and Attack Range traits; Hit Points confirmed; traits grouped Battle / Magic / Explore (D16, D17) |
| 2026-11-01 | Stacks with pooled HP; hero HP bonus flat + percent (D16); upkeep later, affects morale (D9); survivors heal fully after battle for now (D18) |
| 2026-11-01 | Terrain uses `walkable`/`sailable`/`moveCost` (100 = one tile) instead of `passable`/`requiresBoat`; data files are strict (unknown fields are errors, `_` keys are comments); unit id must equal file name; exactly one currency resource |
| 2026-11-01 | Protocol v1: 4-byte big-endian length + UTF-8 JSON object with "type" and optional "seq"; lobby_state broadcast after every change; 64-bit seed sent as text; lobby needs 1 human, 2 occupied slots, 2 teams, all humans ready; Qt keyword `slots` cannot be a member name (C++ `slotList`, JSON `slots`) |
| 2026-11-01 | Design principle: avoid micromanagement (trade, send units to town, buy all, auto-build, build then buy). Buy-all default strategy is quality first (highest tier), cheapest-first as option. A building is a "choice" if it would permanently exclude a rival building of the town's race; those are never auto-built, only flagged. Trade works anywhere for now (a Market building may improve rates later) |
| 2026-11-01 | Buildings: JSON key `requires` is `requiredBuildings` in C++ (reserved word); dwelling stock is keyed by the root building of an upgrade chain; weekly growth on days 8, 15, ... for every town; stacks keep base max HP |
| 2026-11-01 | Visiting: a hero arriving on a mine/town of a neutral or other-team owner captures it (defended towns refuse until M6); objects block walking through but can be the target; fog memory is per team and saved; income starts on day 2; vision balance (town 5, mine 2) and town gold (500) are in balance.json |
| 2026-11-01 | Movement: 8 directions, diagonal costs 1.41x; a step needs its full cost in remaining points; daily points = base + perTraitLevel x Movement Points trait - small slowest-unit penalty, min clamp (`balance.json`). Heroes store `movementPoints`. Generated maps always have a sea border |
| 2026-11-01 | Save format v1: JSON with terrain palette, ids unique across all object kinds, RNG state saved (seed as text); loading validates everything and leaves the target untouched on failure. Level-up: max SP grows but current SP is not refilled. Stacks in armies store base max HP; hero HP bonus applies when a battle starts |
| 2026-11-01 | Qt 6 / C++20 / CMake; server-authoritative client-server; AI as server-side players; data-driven JSON content (see PROJECT.md) |

## Open questions for the user (answer when convenient)
- (none right now)

## Open question about code style
- [?] Naming: the code currently uses Qt-style camelCase (functions/variables), PascalCase types, `m_` members. Do you want snake_case instead? A scripted conversion in one separate commit is possible (types stay PascalCase; Qt overrides such as `paintEvent` must keep their names). JSON field names (camelCase) could be converted too.

## Known issues / notes
- (none yet)

## Session log
- **Session 1:** Wrote PROJECT.md and STATUS.md. No code yet.
- **Session 14:** M4 slice A: protocol, framing, connections, lobby logic and server, lobby client, data directory finder, protocol docs, network tests (23 test programs). Verified with an external script client.
- **Session 13:** Hero tavern slice (hero templates, tavern building, offers, hiring), naming convention written down, tests updated for the new building.
- **Session 12:** Design principle recorded; trade, map dwellings, send units to town, buy all units, auto-build, build then buy; saves extended (mapDwellings, importedUnits); 3 new test programs (18 in total).
- **Session 11:** M3: building data and rules, dwellings, recruiting, weekly growth, upgrades, army transfers, building income, town fields in saves, 2 new test programs (15 in total).
- **Session 10:** M2 slice B: fog of war, mine/town visiting and capture, turn cycle with income, explored maps in saves, 2 new test programs (13 in total).
- **Session 9:** M2 slice A: pathfinding, movement rules, balance.json, map generator, Hero.movementPoints in saves, 3 new test programs (11 in total).
- **Session 8:** M1 slice C: core model, GameState, hero rules, hero classes + progression data, save/load JSON, JsonReader arrays/objects, Rng state, 8 test programs.
- **Session 7:** M1 slice B: JsonReader, content defs, ContentRegistry, sample data, docs/data-format.md, tests. Delta zips now include COMMIT_MSG.txt.
- **Session 6:** M0 confirmed working by user. Added UnitStack + HpBonus (M1 slice A) with 14 unit tests.
- **Session 5:** Wrote and verified the M0 skeleton (CMake, kot_core/net/ai, kot_server, kot_client, tests tst_rng and tst_strongid). Recorded stack/HP model.
- **Session 4:** Traits grouped (Battle/Magic/Explore); added Luck, Attack Range; Hit Points confirmed.
- **Session 3:** Detailed traits (Spell Recovery, Magic, Hit Points), split speed into Movement Points / Battle Speed, level-up rules. Qt 6.4.2 confirmed (Ubuntu).
- **Session 2:** Recorded decisions D1, D2, D5, D7, D11, D12, D13; detailed hero traits, spell points and speed rules in PROJECT.md.
