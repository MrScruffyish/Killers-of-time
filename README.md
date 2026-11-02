# Killers of Time

Turn-based fantasy war game, Qt 6 / C++20, client-server. See `PROJECT.md` for
the design and `STATUS.md` for progress.

## Requirements (Ubuntu 24.04)

```
sudo apt install build-essential cmake ninja-build qt6-base-dev qt6-base-dev-tools libgl1-mesa-dev
```

Qt 6.4 or newer, CMake 3.21 or newer, a C++20 compiler (GCC 11+).

## Build and run with Qt Creator

1. File > Open File or Project... > select the top-level `CMakeLists.txt`.
2. Pick the kit "Desktop Qt 6.4.2 GCC 64bit" > Configure Project.
3. Build (Ctrl+B). Executables end up in `<build dir>/bin`.
4. Choose what to run with the run selector (monitor icon, bottom left):
   `kot_server` or `kot_client`. Qt Creator runs one application at a time per
   run, so the easiest way to test both: run `kot_server` from Qt Creator and
   start the client from a terminal (`<build dir>/bin/kot_client`), or the
   other way around.
5. Tests: the Tests pane (Alt+3) or `ctest` in the build directory.

## Build from the command line

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure

./build/bin/kot_server            # default port 45454, or: --port 5000; finds data/ itself, or: --data <dir>
./build/bin/kot_client            # in another terminal; click Connect
```

Log filtering example: `QT_LOGGING_RULES="kot.*.debug=true" ./build/bin/kot_server`

## Playing

Build everything, then run `kot_client` (from `<build dir>/bin`, so it finds `kot_server` and the
`data/` folder). Click "Host a game on this computer", set up the slots, tick Ready, Start.
Other players join with the server address. Right-click your town on the map to open it.
`KOT_DATA_DIR=/path/to/data` tells the programs where the game data is if it is not found.

## Layout

```
src/core    game data model and rules (no GUI, no networking)
src/net     protocol shared by client and server
src/ai      AI players
src/server  headless server executable
src/client  Qt Widgets client executable
tests/      Qt Test unit tests
data/       game content (JSON), later
docs/       protocol and data format docs, later
```

## Install and packages

```
cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-release
sudo cmake --install build-release            # /usr/local: kot_client, kot_server, tools, data, menu entries
(cd build-release && cpack -G DEB)            # killers-of-time-<version>-Linux.deb
sudo apt install ./build-release/killers-of-time-*.deb
```
Installed programs find their data in `<prefix>/share/killers-of-time/data`. "Killers of Time" and
its map editor appear in the desktop menu.

## Content tools

`kot_graph` (structure graphs and checks), `kot_balance` / `kot_balance_gui` (balance numbers,
tweaking), `kot_mapgen` (random scenarios) and `kot_editor` (map, scenario and campaign
making); see `docs/tools.md`.

## Commit messages

Each delivery from Claude includes `COMMIT_MSG.txt` (ignored by git). One-time setup,
run in the repo root:

```
git config commit.template COMMIT_MSG.txt
```

After that a plain `git commit` opens the editor pre-filled with the message
(save and close to commit). Or skip the editor with `git commit -F COMMIT_MSG.txt`.
New files must be added first: `git add -A`.
