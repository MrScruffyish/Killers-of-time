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
