# Custom art

Put your own pictures here, using the **same relative paths and file names** as in
`../art` (for example `unit/militia/0.png`, `terrain/grass/tile2.png`). The client
checks this folder first and falls back to `../art` for anything you have not replaced.

`kot_artexport` only writes into `data/art`, so it can be run again at any time — after
content changes, a rebuild, or an update — without touching anything here.

A replacement should keep the same master size as the generated picture (see
`docs/tools.md`) and include its `@2`/`@4` copies if you want the faster-loading
versions; without them the master is used and scaled at runtime. Pictures with a
player colour need a matching `.tint.png` too. A custom master replaces the whole
family, so a generated small copy is never mixed in with a different picture.

This folder is optional and may be empty.
