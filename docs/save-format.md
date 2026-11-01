# Save game format (version 1)

`GameState::toJson()` / `GameState::fromJson()` (`src/core/model/GameStateJson.cpp`).
Files are written compactly and atomically (`QSaveFile`). Loading checks the
whole file against the game content and reports every problem; on failure the
target `GameState` is left unchanged.

Top level fields: `formatVersion`, `day`, `nextId`, `currentIndex`, `map`, `rng`,
`turnOrder`, `players`, `heroes`, `armies`, `towns`, `mines`.

- **Ids** are unique across all object kinds (players, heroes, armies, towns, mines).
  Team ids come from the same counter but are shared by the players of a team.
- **map**: `width`, `height`, `terrainPalette` (list of terrain ids) and `tiles`
  (row-major list of palette indices, `width * height` entries).
- **rng**: `seed` as a string (64-bit numbers do not fit a JSON number) and the exact
  generator `state`, so a loaded game continues with the same random numbers.
- **players**: id, name, team, controller (`human`/`ai`), optional aiProfile, race, color, resources.
- **heroes**: id, name, class, owner, level, experience, traits, spellPoints,
  pendingTraitPicks, army. Position lives in the hero's army.
- **armies**: id, optional owner/hero (absent = neutral / none), x, y, stacks.
  A stack is `{unit, maxHp, totalHp}`; the unit count is derived from `totalHp`.
- **towns**: id, name, race, optional owner, x, y, buildings, optional garrison army.
- **mines**: id, resource, dailyAmount, optional owner, x, y.

Cross-checks on load: owners are players, positions are on the map, unit/trait/
terrain/resource/class ids exist, a hero and his army point at each other, no
duplicate ids, at most 7 stacks per army.

Bump `kSaveFormatVersion` when the format changes incompatibly.
