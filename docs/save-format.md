# Save game format (version 1)

`GameState::toJson()` / `GameState::fromJson()` (`src/core/model/GameStateJson.cpp`).
Files are written compactly and atomically (`QSaveFile`). Loading checks the
whole file against the game content and reports every problem; on failure the
target `GameState` is left unchanged.

Top level fields: `formatVersion`, `day`, `nextId`, `currentIndex`, `map`, `rng`,
`explored`, `turnOrder`, `players`, `heroes`, `armies`, `towns`, `mapDwellings`, `mines`.

- **Ids** are unique across all object kinds (players, heroes, armies, towns, mines).
  Team ids come from the same counter but are shared by the players of a team.
- **map**: `width`, `height`, `terrainPalette` (list of terrain ids) and `tiles`
  (row-major list of palette indices, `width * height` entries).
- **explored**: per team id, base64 of one byte per tile (1 = explored), `width * height` bytes.
- **rng**: `seed` as a string (64-bit numbers do not fit a JSON number) and the exact
  generator `state`, so a loaded game continues with the same random numbers.
- **players**: id, name, team, controller (`human`/`ai`), optional aiProfile, race, color, resources.
- **heroes**: id, name, class, owner, level, experience, traits, spellPoints,
  pendingTraitPicks, movementPoints, army, optional template (tavern hero id). Position lives in the hero's army.
- **armies**: id, optional owner/hero (absent = neutral / none), x, y, stacks.
  A stack is `{unit, maxHp, totalHp}`; the unit count is derived from `totalHp`.
- **towns**: id, name, race, optional owner, x, y (the gate; a town covers x..x+1, y-1..y, the other three tiles are walls), buildings, optional garrison army,
  lastBuildDay, recruitPool (dwelling chain root building id -> units waiting),
  importedUnits (unit id -> units sent here from map dwellings), tavernOffers (hero ids).
- **mapDwellings**: id, unit, optional owner, x, y, available (units waiting).
- **mines**: id, resource, dailyAmount, optional owner, x, y.

Cross-checks on load: owners are players, positions are on the map, unit/trait/
terrain/resource/class ids exist, a hero and his army point at each other, no
duplicate ids, at most 7 stacks per army.

Bump `kSaveFormatVersion` when the format changes incompatibly, and add a conversion
from the previous format in `GameStateJson.cpp` when one is possible (a save of a newer
format is always refused with a message). History: 1 first format; 2 towns cover 2x2
tiles (converting a v1 save moves whatever stands on the new walls aside).
Save files (not scenarios) also have `gameVersion` (the game that saved it) and `savedAt`
(ISO date and time); both are shown in the load list.

Heroes may have `spells` (list of spell ids, the spellbook; optional, validated against the content).
Heroes may have `artifacts` (artifact ids; their bonuses are already in `traits`).
`mapObjects` (optional): `id`, `kind` (`treasure`/`artifact`), `x`, `y`, `resources` or `artifact`.
The save has `difficulty` (the overall level); players may have `incomePercent` and `growthPercent` (difficulty bonuses).
Army stacks may have `experience` (average per unit, veterancy).
`victory` (optional; default conquest): `type` `town` (with `town`: whoever owns it wins), `gold` (`amount`: first to that much gold), `days` (`amount`: after that day the team with the most towns wins, none on a tie). Last team standing always wins too.
`weekEvent` (optional): this week's event id.
Heroes may have `inBoat: true` (sailing) and `destination: [x, y]` (the target of a move that ran out of movement; cleared on arrival).
