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
kot_balance weeks [--weeks n] [--budget gold] [--runs n] [--csv]
kot_balance replay <save.json> <attackerArmy> <defenderArmy> [--runs n]
```
- Power is a rough fighting value: HP x average damage x (1 + 5% per attack + defense
  point), weighted up for ranged (more for a longer range, less with few shots), flying,
  double attack and no retaliation.
- Costs count other resources at their trade `value` from `resources.json`, so those
  values are part of balancing too.
- `units` marks units whose power per gold is more than 1.5x away from the median of
  their tier.
- Duels: two armies of equal cost (the budget grows so each side has at least 6 units)
  fight with the real battle rules and the battle AI, without heroes, half of the battles
  with each side attacking. Pairs where one side wins more than 75% are marked.
- Weeks: growth counts. Each race fights with what a fully built town grows in `--weeks`
  (the best version of every dwelling, the race's `growthPercent`); with `--budget` it has
  only that much gold a week (times its `incomePercent`) and buys the best units first,
  as a player would. Prints the armies, every pair of races and each race's average wins.
  Equal-gold duels miss a race that is cheap but grows slowly; this does not.
- Replay: a battle out of a saved game (army ids from the save; a town's garrison army
  for a siege), fought `--runs` times by the battle AI for both sides with the heroes,
  walls and towers. Prints how often the attacker wins and the average losses. For
  playtest questions like "should this battle have been lost?".

## kot_balance_gui: tweaking

`kot_balance_gui [--data dir]`: a table of all units where the numbers (tier, attack,
defense, damage, HP, speed, initiative, growth, gold cost) can be edited; power, cost and
the race summary update at once, the Resources tab edits the trade values, and "Run
duels" plays the equal-cost duels for a tier. "Save" writes the changed numbers into
`data/units/<id>.json` and `resources.json` (other fields are kept) and reloads, so the
game's own data checks run again.

## kot_aiplay: computer players against each other

```
kot_aiplay [--games 6] [--days 56] [--players 3] [--size 64] [--map continent|islands]
           [--profiles simple,aggressive,...] [--races humans,undead,...] [--seed 1] [--views]
```
Plays whole games of AI players only on random maps (races rotate over the seats from
game to game) and prints, averaged over the games at the end of each week, what each
seat has: heroes, towns, army strength (all its armies, garrisons too), gold and heroes
lost so far. Then wins, eliminations, days spent without a hero, and how heroes were
lost (attacking a town, monsters or a hero, attacked, or fled). For judging AI changes:
run it before and after. It also prints the time spent in the AI and rules, and a
`fingerprint` of each game's end state (equal fingerprints = the same game; a check that a
refactoring did not change how the game plays). `--save <file>` saves the last game's end. `--views`
builds every player's view after each turn, as the server sends it after every command,
and prints its cost: build and JSON time, size and compressed size (for profiling the server).

## kot_artexport: the art as PNG files

```
kot_artexport [--data dir] [--out dir]     (default: <data>/art)
kot_artexport --timing draw|load
```
The client never draws pictures: it only loads PNG files from `data/art` (client/Art.h) and
shows a magenta "?" placeholder (with a warning naming the file) for a missing one. The
pictures are drawn in code by the art generator (`src/artgen`, library `kot_artgen`, used only
by this tool and tst_art), which this tool runs: every picture the content can need (units by
rank, dwellings, towns per race/walls/stage, mines, chests, resources, heroes on foot and in
boats, portraits (6 faces per class), artifacts, spells, map sites, buttons, mouse pointers,
terrain tiles, edges, decorations, trees/hills/peaks, fog, coasts, and the town screen: a
backdrop per race, one figure per building kind, walls, boat). Paths are in
`src/client/ArtPaths.h` (e.g. `unit/militia/0.png`, `terrain/grass/tile2.png`). Masters are
512x512 (trees/hills/peaks 1024, the town screen 2700x510 and building figures 600x728,
pointers 128); the client scales them smoothly to the size shown. Every picture is also saved at
half and a quarter of its size (`<path>@2.png`, `<path>@4.png`, tints `<path>@2.tint.png`): the
client loads the smallest copy at least as big as it shows the picture (a quarter decodes ~16x
faster than the master), so a replaced picture needs its copies too (run the export, or scale it). About 1750 files, 72 MB,
90 s. Pictures with the owner's colour are two files:
`<path>.png` (drawn with black as the owner colour) and `<path>.tint.png` (how much of the
owner colour each pixel takes); the client puts them together as base + tint * colour, so
any player colour works. A better picture can replace a file as long as it keeps that rule.
tst_art checks that `data/art` holds exactly the files the export writes.
Run it again after changing the art code or the content (new units, spells, ...). `--timing`
only measures drawing or loading.

## kot_saveview: a save in readable form

```
kot_saveview [--data dir] [--compact] <save.json>
```
Saves store content ids (units, buildings, resources, ...) as numbers: positions in the
content lists, which only mean something with the same game data (`contentChecksum`). This
prints the save as indented JSON with the names from the data files instead
(`"resource": "gold"`, `"resources": {"gold": 750}`), the same form scenarios are stored in.
It needs the data the save was made with; with other data it says so.

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

## kot_viewbench: how fast the adventure map is drawn

```
kot_viewbench [--data dir] [--size 128] [--frames 200] [--tile px] [--gl] [--shots dir]
```
Draws one 128x128 game (whole map explored) in a 1280x800 window two ways: the client's
`MapView`, and the same map as a `QGraphicsView` scene (one item per tile, edge, coast piece,
decoration, tree group and map object). Per zoom step (16, 48, 96 px) it times scrolling
over the map and a still map with a walking hero (ms per frame: the change, the paint and
the hand-over to the window system). `--gl` also draws the scene with OpenGL (needs a real
screen), `--shots` saves a picture of each view to check they show the same thing.
