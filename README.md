# Killers of Time

Turn-based fantasy war game, Qt 6 / C++20, client-server. See `PROJECT.md` for
the design and `STATUS.md` for progress.

## Requirements (Ubuntu 24.04)

```
sudo apt install build-essential cmake ninja-build qt6-base-dev qt6-base-dev-tools libgl1-mesa-dev
sudo apt install qt6-multimedia-dev graphviz   # optional: sound effects, kot_graph PDFs
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

### Server settings
`kot_server` reads `~/.config/KillersOfTime/server.json` when it exists (or the file given
with `--config <file>`), strict JSON, every field optional:
```json
{ "port": 45454, "data": "/path/to/data", "saves": "/path/to/saves", "savesMaxMB": 500, "resume": true }
```
The command line overrides it: `--port`, `--data`, `--saves`, `--saves-max-mb`, `--resume`.
`resume`: at start the server goes on with the newest saved game and waits for the players
(autosaves happen every day, so a restarted server loses at most the current day).

### Playing at different times
The server keeps the game; nobody needs to be connected. When it is your turn, connect, play,
end your turn and leave: the game waits at the turn of a player who is away (no computer plays
for anyone), and a battle against someone who is away is fought by the computer for them.
Your client makes a secret key for each server (kept in its settings); the server keeps only
a hash of it with your seat, so you, and nobody else, get the seat back when you connect again. Anyone may save; only the host
may replace an existing save. A player who is out of the game may still join and watch.
`savesMaxMB` is the most space the save folder may use (0 or missing: no limit); when a
save would go over it, the server first removes the oldest saved games (never campaign
progress) until it fits. Saves go to `~/.local/share/kot_server/saves` by default.

## Build for playing

The Debug build above is for development and tests; it plays slowly. For playing, use an
optimized build that still has debug info (so crash reports show function names and lines):

```
cmake -S . -B build-play -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo   # once
cmake --build build-play                                            # after every change
./build-play/bin/kot_client
```

While playing at the same time, `nice -n 19 cmake --build build-play -j2` builds without making
the game sluggish. Saves (`~/.local/share/kot_server/saves`), settings and the crash log are
shared by all builds.

## Playing

Build everything, then run `kot_client` (from `<build dir>/bin`, so it finds `kot_server` and the
`data/` folder). Click "Host a game on this computer", then "Play against the computer" (or set
up the slots and Start; the host needs no Ready, other humans do).
Other players join with the server address. Right-click your town on the map to open it.
`KOT_DATA_DIR=/path/to/data` tells the programs where the game data is if it is not found.

- **Settings** (window and dialog sizes, last server and name, map zoom, sound, view choices) are
  kept in `~/.config/KillersOfTime/client.conf`; delete it to start fresh.
- **Sound**: the checkbox on the start screen or the Game menu. Off closes the audio output;
  `kot_client --no-sound` (or `KOT_NO_SOUND=1`) never opens it.
- **Crashes**: the client prints a backtrace and appends it to
  `~/.local/share/KillersOfTime/crash.log`; please send that file. Use `build-play` or a Debug build (not
  `build-release`) so it shows function names and lines; for a full stack, `coredumpctl gdb kot_client` and then `bt`.

## Layout

```
src/core    game data model and rules (no GUI, no networking)
src/net     protocol shared by client and server
src/ai      AI players
src/server  headless server executable
src/client  Qt Widgets client executable (loads the PNG art, never draws it)
src/artgen  the placeholder art drawn in code (kot_artexport writes data/art from it)
tests/      Qt Test unit tests
src/tools   content tools (kot_graph, kot_balance, kot_mapgen, kot_aiplay, ...)
data/       game content (JSON) and art (data/art, PNG)
docs/       protocol, data, save format and tools docs
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
tweaking), `kot_aiplay` (computer players against each other, to judge the AI), `kot_mapgen`
(random scenarios) and `kot_editor` (map, scenario and campaign making); see `docs/tools.md`.
