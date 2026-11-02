# Content tools

Command line tools for people making content. They live in `src/tools`, are built
with everything else and end up in `<build dir>/bin`. All of them find `data/` on their
own (like the server) or take `--data <dir>`.

## kot_graph: structure graphs and checks

```
kot_graph buildings [--race humans] [-o buildings.pdf]
kot_graph units     [--race undead] [-o units.png]
kot_graph spells    [-o spells.svg]
kot_graph check
```
- `buildings`: one box per race plus the buildings every race can build. Black arrows:
  "needed for" (the build order), blue: upgrades, red dashed: either / or (rival
  buildings), green dotted: the units a dwelling gives.
- `units`: units by race and tier with their numbers and abilities; blue arrows are
  upgrades.
- `spells`: spells by school, and which race's mage guilds teach them.
- The output format follows the file name: `.dot` / `.gv` write the Graphviz text, any
  other suffix (`.pdf`, `.png`, `.svg`) runs Graphviz `dot` (`sudo apt install graphviz`).
  Without `-o` the dot text goes to standard output, e.g. `kot_graph units | xdot -`.
- `check` reports loops in building requirements and unit upgrades, and buildings that can
  never be built (they need things of another race, or two things that exclude each
  other). Exit code 1 when it finds something. The same checks run whenever the game
  loads its data, so broken data never reaches a game.

## kot_balance: balance numbers

```
kot_balance units [--race id] [--csv]          power and cost of every unit, outliers marked
kot_balance races [--csv]                      a week of each race's dwellings: power and cost
kot_balance duel <unitA> <unitB> [--gold n] [--runs n]
kot_balance duels [--tier n] [--gold n] [--runs n] [--csv]
```
- Power is a rough fighting value: HP x average damage x (1 + 5% per attack + defense
  point), weighted up for ranged, flying, double attack and no retaliation.
- Costs count other resources at their trade `value` from `resources.json`, so those
  values are part of balancing too.
- `units` marks units whose power per gold is more than 1.5x away from the median of
  their tier.
- Duels: two armies of equal cost (the budget grows so each side has at least 6 units)
  fight with the real battle rules and the battle AI, without heroes, half of the battles
  with each side attacking. Pairs where one side wins more than 75% are marked.

## kot_balance_gui: tweaking

`kot_balance_gui [--data dir]`: a table of all units where the numbers (tier, attack,
defense, damage, HP, speed, initiative, growth, gold cost) can be edited; power, cost and
the race summary update at once, the Resources tab edits the trade values, and "Run
duels" plays the equal-cost duels for a tier. "Save" writes the changed numbers into
`data/units/<id>.json` and `resources.json` (other fields are kept) and reloads, so the
game's own data checks run again.

## kot_mapgen: random scenarios

```
kot_mapgen --id two_rivers --name "Two Rivers" --races humans,undead [--size 48x48] [--seed 7] [--description text] [-o file]
```
Makes a random map with everything on it (the same generator as a random game) for one
player per race and writes `data/scenarios/<id>.json`. Open it in `kot_editor` to change it.

## kot_editor: the map / scenario editor

`kot_editor [--data dir] [scenario.json]`. Start from a blank map (New) or a random one
(Random map: size, seed, one race per player), then:
- Terrain: pick a terrain and a brush size, click or drag.
- Town, Mine, Dwelling, Treasure, Artifact, Monsters, Hero (start): pick the tool and its
  options on the right (owner, race, resource, amount / count, unit, artifact, hero class)
  and click a tile. Placing on an object replaces it; Erase removes it.
- Player slots: add one per player (in turn order) with its race; each needs a town or a
  hero. Heroes get their class's starting army.
- Victory: defeat everyone (always), or also take a chosen town, collect an amount of gold,
  or have the most towns after N days.
- Save writes `data/scenarios/<id>.json` and checks it the way the game reads it. The lobby
  offers it as a map from then on.

`KOT_SCREENSHOT=file.png kot_editor ...` saves a picture of the window and quits.
