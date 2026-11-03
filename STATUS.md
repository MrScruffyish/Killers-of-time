# STATUS.md

> Update at the end of every session. Upload together with PROJECT.md at the
> start of the next one.

**Last updated:** 2026-11-03 (session 18, Claude Code, playtest round)
**Current milestone:** first versions of every part of the game are in (M0-M10); version 0.3.0. Now: the user playtests the real client while the feel, the graphics and the rules are improved from his notes
**Next session should do:** go on with the user's playtest notes. This round so far: races dwarves/orcs/ashborn/tidefolk (7), sieges with castles, hot-seat, roads, scenarios and campaigns plus a random maker, 14 more unit upgrades, town/city hall, granary, coasts and terrain features, a softer UI style, growth-aware balancing (`kot_balance weeks`). Next: the user's notes. Later: race abilities of their own (user agreed 7 races are enough for now), balance (sylvan weakest, tidefolk strong over 3 weeks, elves out-range humans). Not wanted (user): day/night, seasons; animation for now

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
  - [x] Marketplace (building effect `market`: +5% trade rate per town, max 95%); buy-all with trade (client buttons and the AI use it)
- [x] Tests: `tst_trade`, `tst_dwellings`, `tst_automation` (18 test programs in total); `tst_towns`, `tst_armyrules`, split/merge cases in `tst_unitstack`, building loading cases in `tst_content` (15 test programs in total)

## M4: Networking and lobby
- [x] Framed JSON protocol, versioned handshake (`docs/protocol.md`; `net/Message`, `MessageFramer`, `Connection`)
- [x] Server lobby (`server/Lobby`, pure logic): slots human/AI/open/closed, race, team, colour, ready, host, map settings, start rules; `GameServer` runs it over TCP (`kot_server_lib`, `kot_server` finds `data/` itself or `--data`)
- [x] Client side: `net/LobbyClient` (used by the temporary client window, which logs the lobby)
- [x] Tests: `tst_framer`, `tst_lobby`, `tst_datadir`, `tst_network` (real server and clients on localhost, protocol violations); `tst_gamesetup`, `tst_playerview`, `tst_session` and a full game over sockets in `tst_network`; 26 test programs in total
- [x] Start the game: `core/rules/GameSetup::createGame` (map from the lobby settings, spread-out start towns, a tavern hero with the class starting army, starting resources, neutral mines and map dwellings, everyone initialised). AI players are the passive AI (they just end their turn).
- [x] Commands over the network (`server/GameSession`): 14 commands (see docs/protocol.md), only on your turn, own things only; `command_result` to the sender and a fresh filtered `game_state` to everyone; `core/model/PlayerView` filters by team (unexplored tiles masked, enemies only in sight and without secrets)
- [x] Client: real connect and lobby screens
- [x] Disconnect during a game: the player becomes passive AI and the game goes on; reconnecting takes the player back (`join_game`)

## Order of work (breadth first, agreed in session 16)
Get every part of the game working in a thin, simple form first, so there is something to try
and react to; add depth afterwards.
1. [x] Playable client slice (this section, M5)
2. [x] Basic battles (`core/battle/AutoBattle`): attack enemy heroes, defended towns and neutral monsters by moving onto them; auto-resolved rounds (initiative order, most dangerous target, ranged shots, one retaliation per round, attack-defense damage formula, hero Attack/Defense), survivors heal (D18), loser army and hero removed, garrison only emptied, XP to the winner; neutral monster armies placed at game start; result in the log and a battle box in the client. Tests: `tst_battle` (29 test programs). Later: hero HP/morale/luck traits, defender notification, monster guards next to mines
3. [x] Simple AI player (`ai/SimpleAi`, profile "simple", the lobby default): plays through the same commands as humans (`IAiPlayer::nextCommand`, run by `GameSession::playAiTurn`, max 200 commands per turn); level-up picks, build then buy, hire heroes, heroes take the garrison along, go for visible mines/dwellings/towns/weaker armies, else explore; own seeded RNG, partly random. Only uses what its team sees (`AiView`). Tests: `tst_ai` (30 test programs). Later: spend its gold, defend towns, difficulty bonuses
4. [x] Game end (`core/rules/Victory`): a player with no towns and no heroes is out (derived, not stored) and skipped in the turn order; last team standing wins; the view has `eliminated` per player and `gameOver`/`winnerTeam`; the server refuses commands with `game_over`; the client announces defeat/victory. Tests: `tst_victory` (31 test programs). Other victory conditions: see M9
5. [x] Save and load a running game on the server (`save_game`/`list_saves`/`load_game`, `--saves <dir>`), take over a free human player (`join_game`; reconnect after leaving, or after a load). Client: Save game button, Load game in the lobby (host), player choice when joining a running game. Tests: two cases in `tst_network`. Later: passwords/tokens, AI RNG state in saves, autosave
6. [~] Then depth: hex battles (M6), spells (M7), races and balance (M8), AI (M9), polish, tools and art (M10): first versions of all done, see below

## M5: Playable adventure map client
- [x] Client structure (`kot_client_lib` + `kot_client`): `MainWindow` with connect page, lobby page, game page; "Host a game on this computer" starts `kot_server` from the same folder
- [x] Lobby screen: slots, host controls (open/AI/closed, AI difficulty), race, team, colour, ready, map size and seed, start
- [x] Map view (`MapView`): placeholder tiles from the terrain colours, black = never explored, darkened = remembered, shapes for hero (circle, yellow ring when selected), town (square with T), mine (diamond, resource colour), dwelling (triangle); scrollbars, Ctrl+wheel zoom
- [x] Hero selection (click hero or drop-down), click a tile to move (the server moves him as far as his movement allows)
- [x] Status bar (day, whose turn, resources), end turn, log of what happened (and server refusals)
- [x] Town dialog (right-click a town on the map, or double-click in the list): buildings with the reason when unavailable and "Build (trade first)" when only resources are missing, recruit with count, buy all here, tavern with hire (trade first if needed)
- [x] Toolbar one-click actions: "Build, then buy units" (with trade), "Buy all units", "Send dwelling units to closest town"; level-up trait choice
- [x] Tests: `tst_viewmodel`, `tst_clientsmoke` (the real windows against a real server, offscreen, clicks included; set `KOT_SCREENSHOT_DIR` to save screenshots); 28 test programs in total. Screenshots in `docs/screenshots/`.
- [x] Path preview (white dots today, red later) and today's range tint, worked out on the client (`MovePlanner`, same pathfinding as the server)
- [x] Minimap (click or drag to look there; frame shows the view)
- [x] Towns that need a building choice are marked in the town list
- [x] Town dialog Armies tab: move units (some or all) between the garrison and a visiting hero
- [x] Tooltips (terrain, towns, mines, dwellings, treasures, artifacts, armies, steps today); keyboard: E end turn, N next hero, C centre, T town. Later: better layout, real art
- [x] Object memory on the client: towns, mines, dwellings and treasures stay as last seen while out of sight. Later: keep it on the server (lost on reconnect now)

## M6: Battle engine (core, then UI)
- [x] Decision D2: hex grid
- [x] Battle state (`core/battle/Battle`, 15x11 hex field, odd-r), initiative order, move/melee/ranged, one retaliation per round, damage formula, round limit draw; `BattleAi` (shoot, attack the most dangerous reachable enemy, else close in); the auto-resolve is the battle AI on both sides; `applyBattleResult` writes back. Tests: `tst_hexbattle`
- [x] Hero trait bonuses in battle: attack, defense, hit points (+% max HP), morale (extra action / falter), luck (double damage). Later: attack range, battle speed tier tables
- [x] Unit data: `battleSpeed` (hexes), `mapSpeed`, `range` (for ranged units)
- [x] Neutral monster guards trigger battles
- [x] Experience after victory -> hero levels (auto max-SP growth + choose one pickable trait per level gained)
- [x] Battle UI in the client (`BattleView`: hexes, move by click, attack/shoot by clicking an enemy, Defend, Auto); interactive battles when a connected human takes part, human vs human live over the network, AI turns pause for a human defender; battle results to both sides (`lastBattle`). Tests: session cases, GUI smoke test
- [x] Deterministic battle replay test (seed + actions)
- [x] Unit abilities (flying, no_retaliation, double_attack, undead), rocks, wait, retreat (attacker)
- [x] Town walls (building effect `defense`: Fort +3, Citadel +3 more)
- [x] Unit veterancy: stacks gain experience in battle, 0-5 stars with small bonuses (morale, initiative, battle speed, attack, defense; data), averaged when merging; shown as stars. Tests: `tst_veterancy` (38 test programs)
- [x] Retreat for either side with a hero (not a town's defenders); surrender to another player (pay `surrenderCostPercent` of the surviving army's price to the winner, keep hero and army); area spells (`area`: Frost Ring, Meteor Shower, Plague Cloud)

## M7: Magic
- [x] Effect primitive system and spell data format (`spells/<id>.json`; damage, heal, modifyStat, movement, reveal; strength = amount + perPower x Spell Power); 8 sample spells
- [x] Hero spellbook (`Hero::spells`, saved), class `startingSpells`, mage guild buildings (`mageGuild` effect, Mage Guild 2 / Mage Tower 3) teach spells on arrival or at turn start. Later: race access to schools
- [x] Magic trait level requirement per spell (`requiredMagic`)
- [x] Battle spells (hero casts once per round, not using the stack's turn; stat changes count down by round; AI heroes cast damage spells); adventure-map spells (`cast_spell`; Swift March, Far Sight; the simple AI casts movement spells)
- [x] Unit-cast spells (unit `spells` + `spellPoints`, full each battle, tier = power; Wraith Curse, Treant Entangle, Unicorn Cure; AI uses them; "Stack's spell" button)
- [x] Spell Power scaling of effects
- [x] Spell UI: spells in the hero panel, Cast spell on the map, Cast spell in battle (choose spell, click target). Tests: `tst_spells` (33 test programs)

## M8: Races and balance
- [x] Race modifier system (`races/<id>.json`: growthPercent, incomePercent, terrainCostPercent, spellSchools), buildings without a race for every race, race names in the lobby. Tests: `tst_races` (34 test programs)
- [x] 3 races: Humans, Undead (hordes, swamps, poor towns; dark/earth), Sylvan (archers, forests, slow growth; nature/water/air); 4 tiers each with an upgrade, 2 hero classes and 6 heroes each; new spells Curse, Death Touch, Entangle, Regrowth. Later: more races, distinct abilities, balance
- [x] Neutral monsters (wandering, guarding mines and artifacts), treasure piles, artifacts (`artifacts.json`, trait bonuses, picked up by walking on them). Tests: `tst_mapobjects` (35 test programs). Later: artifact slots/trading, chests with a choice

## M9: AI
- [x] AI player host on the server (same command interface as humans)
- [x] Battle AI (basic: most dangerous target, ranged first, damage spells)
- [x] Strategic AI v1 (`SimpleAi`): scored targets (value / distance: towns, mines, artifacts, treasures, dwellings, weaker armies, exploring), avoids stronger armies, defends threatened towns, collects reinforcements, hires more heroes over time, class-fitting level-ups, sends dwelling units home, movement spells. It now conquers an idle human within three weeks
- [x] Difficulty: overall level in the lobby (easy/normal/hard/expert: human starting resources, monster size, AI income and growth) plus the per-AI slot level; numbers in balance.json; saved. AI profiles: simple, aggressive, economic, explorer (personality weights). Later: hero stat bonuses
- [x] Teams in mixed human/AI games, victory conditions: conquest always, plus per scenario take a town, collect gold, most towns after N days (editor settings, goal shown in the game)

## M10: Polish and extras
- [x] Save/load game over server, reconnect
- [x] Scenarios (`data/scenarios/<id>.json`, save format inside; chosen in the lobby; players mapped in slot order) and `kot_mapgen` for random ones; two sample scenarios. Tests: `tst_scenario` (39 test programs)
- [x] Map editor `kot_editor` (terrain brush, towns, mines, dwellings, treasures, artifacts, monsters, starting heroes, player slots; random map start; saves checked scenarios)
- [x] Campaigns (`data/campaigns`, chapters = scenarios, progress in the saves folder, best heroes carried over with their army); sample "The First War". Tests: `tst_campaign` (40 test programs)
- [x] Better placeholder art: textured terrain tiles, soft fog edges, drawn icons (castle, mines, huts, chests, artifacts, hero banners, monsters), textured battle hexes with rocks and unit tokens, a dark wood-and-gold style sheet
- [x] Sound effects generated in code (click, steps, clash, spell, coins, bell, drums, fanfare, defeat) with Qt Multimedia when installed; on/off on the start screen and in the Game menu
- [ ] Real art, 2.5D projection decision
- [x] Balance tools: `kot_balance` (units, races, equal-cost duels), `kot_balance_gui` (edit and see), first balance pass with them (bone dragon, archers, snail, wraith, eagle, treant, costs)
- [x] Packaging: install rules, desktop entries, icon, CPack DEB/TGZ (`cpack -G DEB`)
- [x] Random maps have neutral towns with garrisons to conquer
- [x] Races 6 and 7: Ashborn (imps, hellhounds, flame witches, horned brutes, drakes, pit lord; fire and dark magic, sulphur vents) and Tidefolk (deckhands, harpooners, tide priestesses, tritons, sea serpents, kraken; water and nature, pearl divers); hero classes hellknight, pyromancer, captain, tidecaller; neutral salamander, sea witch, frost giant; fire school (fire bolt, battle fury, ash cloud, fireball, inferno) and tidal wave, stampede, soul reap, gust, war cry. Quick balance pass with kot_balance (races within ~7% power per gold; kraken, axe master, minotaur, warg rider, casters' prices). 7 races, 91 units, 89 buildings, 34 spells
- [x] Graphics, big round (user: larger images, more detail): sprites drawn at 512 px (terrain 256) with light, shade and soft shadows; every unit redrawn with faces, hands, armour, mail, feathers, proper horses, dragons, beasts, ghosts; battle units stand as big figures on bases in their side's colour, defenders facing left; a continuous battle ground with soft hex lines, decorations and real trees; standing walls, gate and towers in sieges; detailed map hero, boat, mine, chest, artifact and cottage; finer terrain. More buildings: lumber yard, stone quarry, watchtower (new `vision` effect), alchemist's lab, treasury, and a resource building per race (paper mill, mercury vats, orchard, coal pits, crystal delve). 117 buildings
- [x] Graphics, icons: every resource drawn in detail (coins with a crown, logs with rings, cut stone, a grain sheaf, a cut gem, sulphur crystals, coal with an ember, a mercury flask, a sealed scroll); button icons at 128 px with shading; sawmill, terraced quarry with a crane and glowing crystal cave; town sprites and the town screen with stone courses, round shaded towers, tiled roofs, clouds and grass (`src/client/IconArt.cpp`)
- [x] Smarter battle AI: the hero weighs every spell and target (area damage counts every stack hit; blessings, curses and heals count too), melee stacks hold back for a few rounds when their side out-shoots the enemy (not when storming walls). Against the old AI with race armies and heroes it wins 61%. Ash Cloud fixed (it got weaker with spell power). Minimap: detailed terrain, zooms to the explored part. Town picture: town/city hall, granary, castle. A few more map decorations (fallen logs, mushroom rings, standing stones, dead trees, bones, snowy pines)
- [x] Balance with growth: `kot_balance weeks` (race vs race with their weeks of growth and a weekly gold budget). First pass from it: race growth/income (sylvan +25%, dwarves -10% growth and no income bonus, tidefolk -5%, ashborn no income bonus, undead -10% gold), dwarf units +10-25% price, human knights -10%. Still open: sylvan weakest (~20% wins), tidefolk strong over 3 weeks
- [x] More town upgrades: 14 upgraded units (each race's top unit, archangel, halberdier, ghost dragon, wood nymph, centaur chief, elder shaman, rune elder, tide oracle...) with upgrade dwellings (tier-5 ones need a Castle); Town Hall -> City Hall (+250/+500 gold), Castle (third wall level, a fourth siege tower), Granary (new `growth` building effect, +20% weekly units). Tier-5 prices evened out with kot_balance duels. 105 units, 107 buildings
- [x] Map: wavy coasts with beaches and foam, rivers blending into lakes, trees/hills/peaks drawn over their tiles, soft sight shade, grid on/off (Game menu)
- [x] Random scenario and campaign maker in the lobby ("Make a map...": size, land or islands, players and races, water, mountains, rivers, roads, monster strength, goal, seed; campaigns: chapters that grow, race, carried heroes). Core `ScenarioGen`, protocol `generate_map`/`content_lists`; made maps live in the server's saves folder
- [x] Boats: Shipyard (coastal towns), buy a boat, sail on water, land next to the shore (ends the day's movement); hull drawn under the hero. Tests: `tst_boats` (41 test programs)
- [x] Island maps (lobby map type; sea channels, one island per player, starts near the shore) and the AI uses boats when its island is done
- [x] Weekly events (`events.json`: resources for everyone, growth changes, extra units; announced in the client)
- [x] Hero screen (H): experience bar, traits by group with + buttons for level-up picks, army with stats and stars, spellbook and artifacts with descriptions
- [x] Routes: a click plots the route (dots, ring at the goal), double click or Enter walks it; a move that runs out of movement keeps its destination for the next day (saved; the AI uses it too)
- [x] Zone of control: monsters and enemy heroes on the map attack a hero who comes within 2 tiles (shown in orange; the planned path marks where); new Scouting trait: +vision, +movement, smaller zone (balance.json "scouting")
- [x] End turn button glows when no hero can move; town build tab: buildable first, "only what I can build", What it does, bigger Close
- [x] Map look: zoom in fixed steps (16-96 px, Ctrl+wheel, + and -, Game menu; remembered), icons drawn once big with an outline and scaled down, rider heroes, castles, resource icons on mines, "few/pack/horde" plates on monsters, town names, faint grid at big zoom
- [x] Towns cover 2x2 tiles (gate = the town's tile, bottom left; the other three are walls); right click or Shift+click opens your town
- [x] Mouse pointers: footsteps (white today, red later), crossed swords (attack or ambush), gate (enter), hand (select), blocked; a hover line under the map says what is there
- [x] Game menu (Save game, icon size, sound) instead of the Save button
- [x] Save versions: files carry the game version (now 0.2.0) and save time; save format 2 (2x2 towns) converts format 1 saves on load; newer or broken saves are refused with the reason; the lobby's Load opens a list (newest first: name, saved, day, players, map, version, can load?)
- [x] Side panel: hero portrait, bars for movement / spell points / experience, traits, the army in 10 slots (tooltips with unit stats; click opens the hero screen); resource bar with icons, amounts and +N per day (view: `income`, heroes' `movementMax`); armies hold up to 10 stacks
- [x] Autosave at the start of every day (`autosave_dayNNNN`, the lobby's Autosaves setting says how many are kept, default 5, 0 = off); each campaign chapter is saved when it starts
- [ ] Playtesting
- [x] Battle map: ranges are real (unit `range` in hexes + the hero's Attack Range; out of range cannot be shot, the AI walks into range), hovering a shooter shows its range (red for enemies, blue for yours), bow badges with the range on ranged tokens, a stats panel (hovered or acting stack), keys D (defend) and W (wait)
- [x] Unit ranks: regular / advanced / elite from the upgradesTo chain (separate units, never merge); humans: Archer (range 5) -> Advanced Archer (6, Archery School) -> Elite Archer (7, Archery Academy); elf archers 6, marksmen 8
- [x] Unit pictures drawn in code (one per unit kind: spearmen, archers by rank, elves, skeletons, zombie, wraith, bone dragon, eagle, snail, treant, unicorn, woodsman) on battle tokens, army slots and map monsters; rank chevrons; neutral monsters violet in battle
- [x] Lobby: the host needs no Ready; a host alone in a new lobby gets one AI opponent by default; "Play against the computer" adds one if needed and starts
- [x] Client settings file (~/.config/KillersOfTime/client.conf): window/dialog geometry, last server and name, zoom, sound, view choices
- [x] Sound: one audio output with an own mixer instead of QSoundEffect (fixes a PulseAudio abort when effects overlapped, e.g. archers shooting twice); sound off closes the output; --no-sound; crash backtraces to ~/.local/share/KillersOfTime/crash.log
- [x] Market window (M, Game menu): give one resource for another at your trade rate (view: `tradePercent`), preview before trading; M no longer toggles sound
- [x] Map: centred on your hero at the start (the centre is kept until the window has its size); scrolls when the mouse is near the edge
- [x] Town: "Give all units to <hero>" (G) on the Recruit tab, Armies tab's Move everything uses one command (`move_units` with `all`); one hero per town (already a rule, now tested); `move_units` accepts all 10 stack slots
- [x] Spotting: a moving hero stops when a hostile army comes into sight that his team could not see when he set out (`spotted`; the route is kept, Enter goes on), so a zone of control never attacks from out of sight (playtest: a mine guard at the edge of vision)
- [x] Battle result window: emblem (victory laurel and swords / broken sword / grey), both sides' losses as unit pictures, experience, level up, heroes fallen or fled
- [x] Towns look like what they are: map castles by race (stone and roofs), size (more houses, taller keep, a spire) and walls (palisade, stone walls with two towers, citadel with four); the town screen has a picture of the town with one figure per building (units on the dwellings' signs, mage tower by level, market, boat at the shipyard, walls in front)
- [x] Battle pointer: footsteps (move), crossed swords (attack from here), sword with footsteps (move, then attack: the hex to strike from is outlined in gold, chosen by the side of the enemy the mouse is on), bow (shoot), blocked
- [x] Map: soft edges where terrains meet (the higher layer spreads over the lower), surf along coasts
- [x] Battles are fought on the ground of the defender's tile (battle `terrain`): grass, earth, sand, snow, swamp; trees as obstacles in forests, pools in swamps, rocks elsewhere
- [x] Animation: heroes walk their route tile by tile on the map; in battle stacks slide to their new hex, losses float up as red -N, wiped-out stacks fade
- [x] Hero portraits by class (helmets, wizard hat, hoods, leaf crown; undead with green eyes) in the hero panel, hero screen and tavern
- [x] Minimap: bigger, gold frame, markers for towns (keeps), mines, monsters and heroes; the unexplored world is dark drifting clouds on the map
- [x] Map objects: sawmill (wood), quarry (stone), crystal cave (gems), mines for the rest; dwellings show their unit on a sign
- [x] Battle actions shown: arrows fly, attackers lunge and the target flashes, spells sparkle (colour by effect), morale/falter/lucky words; the server now also sends what the computer's stacks did
- [x] Version 0.3.0 from CMakeLists.txt, plus the git commit (generated BuildInfo.h): `--version` on client and server, in the crash log, in the lobby (a warning when client and server are different builds)
- [x] Lobby: the host has no Ready box ("host"); town: the Recruit tab only shows when something can be recruited
- [x] Battle auto-play: A or the Auto-play button lets the battle AI move your stacks one by one (watchable, stops with A); "Finish automatically" still plays it out at once (`battle_action` `ai`)
- [x] Level-ups are loud: fanfare and a Level up window with the portrait and "Choose now"; a gold "Level N! Choose a trait" button; End turn asks first when a pick is waiting
- [x] T cycles through your towns (the map looks there), O opens one; a click in the town list centres the map; treasures stay on screen until the walking hero reaches them; auto-play is off at the start of every battle; buildings ruled out by a rival are not listed
- [x] Two lobby options: Hidden map (on: unexplored land is black; off: the whole map with towns, mines and dwellings is known from the start) and Fog of war (on: explored land out of sight is shaded and hides armies; off: explored land stays in sight); saved
- [x] Weak monsters offer to join: neutral armies at least `joinStrengthRatio` (5) times weaker than the hero's army that fit into it stop him with an offer (pay / fight / leave them); joining costs `joinCostPercent` (50) of their recruit price in gold; computer players pay when they can, else fight
- [x] Town sieges: walls by the town's defense buildings (Fort, Citadel) with a gate, rubble when a segment falls; towers shoot the most dangerous attacker each round; defending shooters do more damage; the attacker can pay for a catapult for one battle (gone afterwards; computer attackers buy one when they can). `tst_siege`
- [x] Hot-seat: lobby slot kind "Human on this computer" (named by the host, always ready); one connection plays several players, the server acts for the one whose turn it is (or whose stack acts in a battle); the client covers the game with a "Pass to <name>" screen between them; map memory per player
- [x] More content: 36 units (each race up to tier 5: humans swordsman/crusader, monk, cavalier, angel; sylvan dryad, centaur, ancient treant, green dragon; undead ghoul, lich, spectre, vampire, dread knight; neutral wolf, harpy, troll, ogre), 40 buildings (their dwellings, Mage Academy level 4), 21 spells (ice bolt, bless, rock fall, barkskin, thorn storm, weakness, drain life, chain lightning, wind step), pictures for all
- [x] AI: saves up for a boat when its island is done (and buys missing wood), lands only on real islands it has not left before
- [x] Graphics: map decorations (flowers, bushes, stones, cacti, reeds, mushrooms, sea rocks), health bars on hurt battle stacks, a title picture on the start screen (castle at dusk, version), a big Host button, a Day/Week banner at the start of each day
- [x] Drawn icons on the side panel, battle buttons and town tabs; the battle field is centred in a wooden frame
- [x] Lobby: one Start button (adds a computer opponent when there is nobody to play against); Home or the button next to the hero list centres the map on the hero
- [x] Spell book: casting opens cards with a school rune, cost and description (not castable ones greyed); build list: built buildings in one line, "built today" said once, narrower columns
- [x] Routes go around visible enemies' zones of control when there is a way (server and planner alike), even if longer; hold and drag the map to move it
- [x] Two new races: Dwarves (hills and snow, earth and water magic: warriors, axe throwers, golems, rune priests, bear riders, earth titan) and Orcs (steppe, air and dark magic: goblins, wolf riders, orc warriors, shamans, cyclopes, behemoth), each with upgrade paths, dwellings, 2 hero classes and 4 tavern heroes; new upgrades for the old races (forest warden, silver unicorn, paladin, priest, vampire lord, arch lich); neutral monsters giant spider, fire elemental, basilisk, minotaur, hydra; 14 new artifacts (24); pictures, town looks and portraits for all. Content: 5 races, 67 units, 66 buildings, 24 spells
- [x] Random maps: roads join every town to its nearest neighbours (bridges over rivers), mountain ranges with passes, more rivers on bigger maps
- [x] AI: a hero who walks into his town later in the turn takes the garrison (took nothing before and came back every day); fetches the garrison only when it at least doubles his army; computer heroes do not stop on sighting (that is for humans)
- [x] Battle AI picks the best blow: kills times danger (shooters count more) minus the counter-blow; town: the Recruit tab stays while a hero can still take the garrison
- [~] Balance: a quick pass on prices (crusader, swordsman, cavalier, vampire, ogre, troll, snail, ancient treant dearer; green dragon cheaper): power per 1000 gold now 3.2-3.5k for every race. More later with kot_balance_gui

---

## Decisions log
| Date | Decision |
|---|---|
| 2026-11-01 | Sequential turns (D1); hex battle grid (D2); spell-point magic (D5); placeholder art (D7); title "Killers of Time" (D11); Ubuntu + Qt Creator; one trait boost per level-up (D13) |
| 2026-11-01 | Added traits: Spell Recovery, Magic (gates spells), Hit Points; split Speed into Movement Points + Battle Speed (D14); units have own speeds; slowest unit only slightly lowers army map movement; auto max-SP + one picked trait per level (D13) |
| 2026-11-01 | Added Luck and Attack Range traits; Hit Points confirmed; traits grouped Battle / Magic / Explore (D16, D17) |
| 2026-11-03 | No 2-hex units, every unit takes one hex |
| 2026-11-01 | Stacks with pooled HP; hero HP bonus flat + percent (D16); survivors heal fully after battle for now (D18) |
| 2026-11-01 | Terrain uses `walkable`/`sailable`/`moveCost` (100 = one tile) instead of `passable`/`requiresBoat`; data files are strict (unknown fields are errors, `_` keys are comments); unit id must equal file name; exactly one currency resource |
| 2026-11-01 | Work breadth-first (user suggestion): thin version of every part of the game first, depth later. Client is Qt Widgets with a custom scroll-area map; the server computes what can be built/recruited and sends the reasons, the client only displays them. Lobby: unchanged settings no longer reset ready flags (found by the GUI test) |
| 2026-11-01 | Game over the network: the full filtered view is sent after every change (no deltas yet; fine for now); players of the same team see each other in full, resources are private; a player who leaves is played by the passive AI; starting resources and hero class starting armies are data; bug found by tests: all players are now initialised at game start, not only the first |
| 2026-11-01 | Protocol v1: 4-byte big-endian length + UTF-8 JSON object with "type" and optional "seq"; lobby_state broadcast after every change; 64-bit seed sent as text; lobby needs 1 human, 2 occupied slots, 2 teams, all humans ready; Qt keyword `slots` cannot be a member name (C++ `slotList`, JSON `slots`) |
| 2026-11-01 | Design principle: avoid micromanagement (trade, send units to town, buy all, auto-build, build then buy). Buy-all default strategy is quality first (highest tier), cheapest-first as option. A building is a "choice" if it would permanently exclude a rival building of the town's race; those are never auto-built, only flagged. Trade works anywhere for now (a Market building may improve rates later) |
| 2026-11-01 | Buildings: JSON key `requires` is `requiredBuildings` in C++ (reserved word); dwelling stock is keyed by the root building of an upgrade chain; weekly growth on days 8, 15, ... for every town; stacks keep base max HP |
| 2026-11-01 | Visiting: a hero arriving on a mine/town of a neutral or other-team owner captures it (defended towns refuse until M6); objects block walking through but can be the target; fog memory is per team and saved; income starts on day 2; vision balance (town 5, mine 2) and town gold (500) are in balance.json |
| 2026-11-01 | Movement: 8 directions, diagonal costs 1.41x; a step needs its full cost in remaining points; daily points = base + perTraitLevel x Movement Points trait - small slowest-unit penalty, min clamp (`balance.json`). Heroes store `movementPoints`. Generated maps always have a sea border |
| 2026-11-01 | Save format v1: JSON with terrain palette, ids unique across all object kinds, RNG state saved (seed as text); loading validates everything and leaves the target untouched on failure. Level-up: max SP grows but current SP is not refilled. Stacks in armies store base max HP; hero HP bonus applies when a battle starts |
| 2026-11-02 | Unit veterancy (user idea): 0-5 stars from experience per unit, small cumulative bonuses, averaged on merge. Resource trade values count in balancing (kot_balance). |
| 2026-11-01 | Qt 6 / C++20 / CMake; server-authoritative client-server; AI as server-side players; data-driven JSON content (see PROJECT.md) |

## Open questions for the user (answer when convenient)
- (none right now)

## Code style
- [x] Naming stays camelCase for now (user, session 17). Was: the code currently uses Qt-style camelCase (functions/variables), PascalCase types, `m_` members. Do you want snake_case instead? A scripted conversion in one separate commit is possible (types stay PascalCase; Qt overrides such as `paintEvent` must keep their names). JSON field names (camelCase) could be converted too.

## Known issues / notes
- (none yet)

## Session log
- **Session 1:** Wrote PROJECT.md and STATUS.md. No code yet.
- **Session 18:** Playtest round with the user: upkeep removed; routes (click plots, double click/Enter walks, destination remembered); zone of control and the Scouting trait; spotting stops a hero when an unseen enemy appears; 2x2 towns; zoom steps and sprite art; pointers; Game menu; versioned saves with conversion and a load list; autosaves and campaign chapter saves; hero panel, resource bar, 10 army slots; battle ranges, unit ranks (regular/advanced/elite), battle stats panel, D/W keys, battle pointers; unit pictures; market (M); settings file; sound through one output (PulseAudio crash fixed), crash log; battle result window; towns drawn by race, size and walls, town picture (42 test programs).
- **Session 17:** Moved to Claude Code (CLAUDE.md added). Long autonomous run: battle depth, map content, AI, difficulty, client polish, tools (kot_graph, kot_balance(_gui), kot_mapgen, kot_editor), scenarios, campaigns, veterancy, art, sound, packaging, hero screen, weekly events, boats and islands (41 test programs). Basic auto-resolved battles, neutral monsters, battle reports over the network and in the client. Simple AI player. Game end (last team standing). Save/load on the server and reconnect. Hex battles (simple) with a battle screen. Spells (simple). Three races.
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
