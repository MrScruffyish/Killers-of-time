# STATUS.md

> Update at the end of every session. Upload together with PROJECT.md at the
> start of the next one.

**Last updated:** 2026-11-02 (session 17, first Claude Code session)
**Current milestone:** thin playable slice done (connect -> lobby -> map -> move -> town -> end turn, all with placeholder graphics). Now: close the game loop breadth-first (see "Order of work"); M0 verified on your machine, everything since verified in sandbox
**Next session should do:** the thin game loop is complete. Start on depth (step 6): user picks the order; suggestion: hex battles (M6) or a playtest round first

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
- [x] Tests: `tst_framer`, `tst_lobby`, `tst_datadir`, `tst_network` (real server and clients on localhost, protocol violations); `tst_gamesetup`, `tst_playerview`, `tst_session` and a full game over sockets in `tst_network`; 26 test programs in total
- [x] Start the game: `core/rules/GameSetup::createGame` (map from the lobby settings, spread-out start towns, a tavern hero with the class starting army, starting resources, neutral mines and map dwellings, everyone initialised). AI players are the passive AI (they just end their turn).
- [x] Commands over the network (`server/GameSession`): 14 commands (see docs/protocol.md), only on your turn, own things only; `command_result` to the sender and a fresh filtered `game_state` to everyone; `core/model/PlayerView` filters by team (unexplored tiles masked, enemies only in sight and without secrets)
- [ ] Client: real connect and lobby screens (M5 has the map)
- [x] Disconnect during a game: the player becomes passive AI and the game goes on; reconnecting takes the player back (`join_game`)

## Order of work (breadth first, agreed in session 16)
Get every part of the game working in a thin, simple form first, so there is something to try
and react to; add depth afterwards.
1. [x] Playable client slice (this section, M5)
2. [x] Basic battles (`core/battle/AutoBattle`): attack enemy heroes, defended towns and neutral monsters by moving onto them; auto-resolved rounds (initiative order, most dangerous target, ranged shots, one retaliation per round, attack-defense damage formula, hero Attack/Defense), survivors heal (D18), loser army and hero removed, garrison only emptied, XP to the winner; neutral monster armies placed at game start; result in the log and a battle box in the client. Tests: `tst_battle` (29 test programs). Later: hero HP/morale/luck traits, defender notification, monster guards next to mines
3. [x] Simple AI player (`ai/SimpleAi`, profile "simple", the lobby default): plays through the same commands as humans (`IAiPlayer::nextCommand`, run by `GameSession::playAiTurn`, max 200 commands per turn); level-up picks, build then buy, hire heroes, heroes take the garrison along, go for visible mines/dwellings/towns/weaker armies, else explore; own seeded RNG, partly random. Only uses what its team sees (`AiView`). Tests: `tst_ai` (30 test programs). Later: spend its gold, defend towns, difficulty bonuses
4. [x] Game end (`core/rules/Victory`): a player with no towns and no heroes is out (derived, not stored) and skipped in the turn order; last team standing wins; the view has `eliminated` per player and `gameOver`/`winnerTeam`; the server refuses commands with `game_over`; the client announces defeat/victory. Tests: `tst_victory` (31 test programs). Later: other victory conditions, a lost player watching on
5. [x] Save and load a running game on the server (`save_game`/`list_saves`/`load_game`, `--saves <dir>`), take over a free human player (`join_game`; reconnect after leaving, or after a load). Client: Save game button, Load game in the lobby (host), player choice when joining a running game. Tests: two cases in `tst_network`. Later: passwords/tokens, AI RNG state in saves, autosave
6. [ ] Then depth: hex tactical battles with UI (M6), spells (M7), more races and balance (M8), better AI (M9), polish and art (M10)

## M5: Playable adventure map client
- [x] Client structure (`kot_client_lib` + `kot_client`): `MainWindow` with connect page, lobby page, game page; "Host a game on this computer" starts `kot_server` from the same folder
- [x] Lobby screen: slots, host controls (open/AI/closed, AI difficulty), race, team, colour, ready, map size and seed, start
- [x] Map view (`MapView`): placeholder tiles from the terrain colours, black = never explored, darkened = remembered, shapes for hero (circle, yellow ring when selected), town (square with T), mine (diamond, resource colour), dwelling (triangle); scrollbars, Ctrl+wheel zoom
- [x] Hero selection (click hero or drop-down), click a tile to move (the server moves him as far as his movement allows)
- [x] Status bar (day, whose turn, resources), end turn, log of what happened (and server refusals)
- [x] Town dialog (right-click a town on the map, or double-click in the list): buildings with the reason when unavailable and "Build (trade first)" when only resources are missing, recruit with count, buy all here, tavern with hire (trade first if needed)
- [x] Toolbar one-click actions: "Build, then buy units" (with trade), "Buy all units", "Send dwelling units to closest town"; level-up trait choice
- [x] Tests: `tst_viewmodel`, `tst_clientsmoke` (the real windows against a real server, offscreen, clicks included; set `KOT_SCREENSHOT_DIR` to save screenshots); 28 test programs in total. Screenshots in `docs/screenshots/`.
- [ ] Path preview and movement range overlay
- [ ] Minimap
- [ ] Flag towns that need a building choice (the server already reports them in `auto_build`)
- [ ] Hover info (tile, object tooltips), keyboard shortcuts, better layout, real art later
- [ ] Enemy object memory (objects out of sight disappear from the map for now)

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
| 2026-11-01 | Work breadth-first (user suggestion): thin version of every part of the game first, depth later. Client is Qt Widgets with a custom scroll-area map; the server computes what can be built/recruited and sends the reasons, the client only displays them. Lobby: unchanged settings no longer reset ready flags (found by the GUI test) |
| 2026-11-01 | Game over the network: the full filtered view is sent after every change (no deltas yet; fine for now); players of the same team see each other in full, resources are private; a player who leaves is played by the passive AI; starting resources and hero class starting armies are data; bug found by tests: all players are now initialised at game start, not only the first |
| 2026-11-01 | Protocol v1: 4-byte big-endian length + UTF-8 JSON object with "type" and optional "seq"; lobby_state broadcast after every change; 64-bit seed sent as text; lobby needs 1 human, 2 occupied slots, 2 teams, all humans ready; Qt keyword `slots` cannot be a member name (C++ `slotList`, JSON `slots`) |
| 2026-11-01 | Design principle: avoid micromanagement (trade, send units to town, buy all, auto-build, build then buy). Buy-all default strategy is quality first (highest tier), cheapest-first as option. A building is a "choice" if it would permanently exclude a rival building of the town's race; those are never auto-built, only flagged. Trade works anywhere for now (a Market building may improve rates later) |
| 2026-11-01 | Buildings: JSON key `requires` is `requiredBuildings` in C++ (reserved word); dwelling stock is keyed by the root building of an upgrade chain; weekly growth on days 8, 15, ... for every town; stacks keep base max HP |
| 2026-11-01 | Visiting: a hero arriving on a mine/town of a neutral or other-team owner captures it (defended towns refuse until M6); objects block walking through but can be the target; fog memory is per team and saved; income starts on day 2; vision balance (town 5, mine 2) and town gold (500) are in balance.json |
| 2026-11-01 | Movement: 8 directions, diagonal costs 1.41x; a step needs its full cost in remaining points; daily points = base + perTraitLevel x Movement Points trait - small slowest-unit penalty, min clamp (`balance.json`). Heroes store `movementPoints`. Generated maps always have a sea border |
| 2026-11-01 | Save format v1: JSON with terrain palette, ids unique across all object kinds, RNG state saved (seed as text); loading validates everything and leaves the target untouched on failure. Level-up: max SP grows but current SP is not refilled. Stacks in armies store base max HP; hero HP bonus applies when a battle starts |
| 2026-11-01 | Qt 6 / C++20 / CMake; server-authoritative client-server; AI as server-side players; data-driven JSON content (see PROJECT.md) |

## Open questions for the user (answer when convenient)
- (none right now)

## Code style
- [x] Naming stays camelCase for now (user, session 17). Was: the code currently uses Qt-style camelCase (functions/variables), PascalCase types, `m_` members. Do you want snake_case instead? A scripted conversion in one separate commit is possible (types stay PascalCase; Qt overrides such as `paintEvent` must keep their names). JSON field names (camelCase) could be converted too.

## Known issues / notes
- (none yet)

## Session log
- **Session 1:** Wrote PROJECT.md and STATUS.md. No code yet.
- **Session 17:** Moved to Claude Code (CLAUDE.md added). Basic auto-resolved battles, neutral monsters, battle reports over the network and in the client. Simple AI player. Game end (last team standing). Save/load on the server and reconnect.
- **Session 16:** Client slice (connect, lobby, map, town dialog, one-click actions), view parser, GUI smoke test with screenshots, lobby ready-flag fix, roadmap reordered breadth-first.
- **Session 15:** M4 slice B: game creation from the lobby, filtered player views, game commands and turns over the network, tests with real sockets, played a game from an external script. Starting resources/army in data.
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
