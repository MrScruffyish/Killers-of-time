# STATUS.md

> Update at the end of every session. Upload together with PROJECT.md at the
> start of the next one.

**Last updated:** 2026-11-01 (session 7)
**Current milestone:** M1 in progress (M0 done and verified on your machine)
**Next session should do:** M1 slice C: core model (Map/Tile, Player/Team, Hero with traits and levels, Army, Town/Mine) and GameState save/load

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
- [~] JSON schemas/docs in `docs/` (done: `docs/data-format.md` for resources, terrain, traits, units; todo: buildings, spells, races, heroes)
- [x] `ContentRegistry` loader with validation and helpful errors (`JsonReader`, strict, collects all errors)
- [~] Trait system: trait definitions with `group` and `pickable` loaded (done); base/growth per hero class and trait value tables (todo, with Hero in slice C)
- [x] Stack/HP model (`core/model/UnitStack`: pooled totalHp, count derived, damage elimination one by one, heal, add units, flat+percent hero HP bonus via `effectiveMaxHp`; see PROJECT.md 5.6.1) with unit tests (`tst_unitstack`)
- [ ] Core model: `Map`, `Tile`, `Player`, `Team`, `Hero`, `Army`, `UnitStack`, `Town`, `Mine`
- [~] Sample data: 9 resources, 12 terrain types, 13 traits, 5 units done; race + buildings todo
- [ ] Save/load of `GameState` to JSON
- [~] Unit tests for loading (done: `tst_jsonreader`, `tst_content`); (de)serialization todo

## M2: Map and adventure rules (single machine, no network yet)
- [ ] Map generation (simple random/noise-based) and/or hand-made test map
- [ ] Pathfinding (A*) with terrain costs, impassable tiles, boat rules
- [ ] Hero Movement Points trait -> daily movement points; small penalty from slowest unit; daily reset
- [ ] Fog of war (unexplored / remembered / visible) per team
- [ ] Mines and resource piles: capture, daily income
- [ ] Day/turn cycle, resources stockpile
- [ ] Tests for movement, vision, income

## M3: Towns, buildings, recruiting
- [ ] Building graph: prerequisites, exclusive choices, costs, one build per day
- [ ] Dwellings, weekly unit growth, recruit into garrison
- [ ] Unit upgrades (base -> upgraded)
- [ ] Hero tavern: recruit new heroes
- [ ] Army management: split/merge/transfer between hero and garrison
- [ ] Tests for build rules and dependency/exclusion logic

## M4: Networking and lobby
- [ ] Framed JSON protocol, versioned handshake (`docs/protocol.md`)
- [ ] Server session: lobby, slots (human/AI, race, team, color), start game
- [ ] Command validation -> state change -> fog-filtered `StateDelta` broadcast
- [ ] Turn order and `EndTurn`
- [ ] Client: connect screen, lobby screen
- [ ] Disconnect/reconnect handling (basic)

## M5: Playable adventure map client
- [ ] Map view (QGraphicsView): terrain tiles, objects, fog rendering, scrolling/zoom
- [ ] Hero selection, path preview, move
- [ ] Resource bar, day counter, end-turn button
- [ ] Town screen (buildings, recruit)
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
| 2026-11-01 | Qt 6 / C++20 / CMake; server-authoritative client-server; AI as server-side players; data-driven JSON content (see PROJECT.md) |

## Open questions for the user (answer when convenient)
- (none right now)

## Known issues / notes
- (none yet)

## Session log
- **Session 1:** Wrote PROJECT.md and STATUS.md. No code yet.
- **Session 7:** M1 slice B: JsonReader, content defs, ContentRegistry, sample data, docs/data-format.md, tests. Delta zips now include COMMIT_MSG.txt.
- **Session 6:** M0 confirmed working by user. Added UnitStack + HpBonus (M1 slice A) with 14 unit tests.
- **Session 5:** Wrote and verified the M0 skeleton (CMake, kot_core/net/ai, kot_server, kot_client, tests tst_rng and tst_strongid). Recorded stack/HP model.
- **Session 4:** Traits grouped (Battle/Magic/Explore); added Luck, Attack Range; Hit Points confirmed.
- **Session 3:** Detailed traits (Spell Recovery, Magic, Hit Points), split speed into Movement Points / Battle Speed, level-up rules. Qt 6.4.2 confirmed (Ubuntu).
- **Session 2:** Recorded decisions D1, D2, D5, D7, D11, D12, D13; detailed hero traits, spell points and speed rules in PROJECT.md.
