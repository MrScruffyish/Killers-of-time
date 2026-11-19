# Killers of Time

Turn-based fantasy war game, Qt 6 / C++20, client-server. See `PROJECT.md` for
the design and `TODO.md` for open work.

## Requirements (Ubuntu 24.04)

```
sudo apt install build-essential cmake ninja-build qt6-base-dev qt6-base-dev-tools libgl1-mesa-dev
sudo apt install qt6-multimedia-dev graphviz   # optional: sound effects, kot_graph PDFs
sudo apt install openssl                       # optional: TLS certificates for the server
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

The `Makefile` wraps the same builds. Instead of the commands above you can run:

```
make            # EVERYTHING: Debug build + art refresh + play and Release builds
make build      # Debug build in build/ only
make art        # Debug build + refresh data/art if inputs changed
make test       # Debug build + art refresh, then ctest (output also in last_test)
make play       # RelWithDebInfo build in build-play/ only
make release    # Release build in build-release/ only
make package    # Release build, then make the .deb in build-release/
make install    # install an existing Release build (sudo make install for /usr/local)
make clean      # remove build/, build-play/ and build-release/
```

`make` (the default) builds **everything** and refreshes the generated art when a content
JSON, drawing, `ArtPaths.h` or the exporter changed. Each `make XXX` builds only that one
thing: `make build` is the Debug build in `build/` (the one the tests use), `make play` the
optimized RelWithDebInfo build in `build-play/`, `make release` the Release build in
`build-release/`. `make test` does the Debug build + art refresh, then runs `ctest`; it stops
at the first failing test (`ctest --stop-on-failure`) and copies everything it prints to both
the console and a `last_test` file in the project root. Debug is unoptimized and plays slowly,
so use the play build for real games. An art refresh rewrites `data/art` only; your pictures in
`data/custom_art` are never touched.

`make play` keeps debug info in the optimized build, so crash reports still show function names
and lines. It does not build or run the tests.

Neither one starts the game — they only build it. Run the client from the matching directory
yourself: `./build/bin/kot_client` or `./build-play/bin/kot_client`. After a code change, run
the same command again; it only recompiles what changed.

`JOBS=N` limits the number of parallel compiler jobs.

Log filtering example: `QT_LOGGING_RULES="kot.*.debug=true" ./build/bin/kot_server`

**Logs when something goes wrong.** Qt log messages go to the terminal (stderr); turn on the
verbose ones with `QT_LOGGING_RULES="kot.*.debug=true"` and keep a copy with `... 2>&1 | tee
play.log`. On a crash (SIGSEGV/SIGABRT/SIGFPE/SIGILL/SIGBUS) the client also appends a
backtrace to `~/.local/share/KillersOfTime/crash.log`, shared by all builds. Keep the `play`
build for playing: it runs at full speed but still has the debug symbols that make those
backtraces name functions and lines.

### Server settings
`kot_server` reads `~/.config/KillersOfTime/server.json` when it exists (or the file given
with `--config <file>`), strict JSON, every field optional:
```json
{ "port": 45454, "data": "/path/to/data", "saves": "/path/to/saves", "savesMaxMB": 500, "resume": true,
  "tls": false, "tlsCert": "/path/to/cert.pem", "tlsKey": "/path/to/key.pem",
  "allowedGameTokens": ["<64 hex digits>", "<another>"] }
```
The command line overrides it: `--port`, `--data`, `--saves`, `--saves-max-mb`, `--resume`,
`--tls`, `--tls-cert`, `--tls-key`, `--allow-token <token>` (repeatable; its tokens are
allowed in addition to those in the file).
`resume`: at start the server goes on with the newest saved game and waits for the players
(autosaves happen every day, so a restarted server loses at most the current day).
`allowedGameTokens`: only let in clients with one of these game tokens (a client shows its
token in the Game menu); empty or missing lets anyone in. Keep the config private: a token
lets its player in and gets his seats back. The tokens may be typed with spaces and dashes.
`tls`: encrypt every connection (start the client with "Encrypt the connection (TLS)" too).
The first time, the server makes a self-signed certificate and key in
`~/.local/share/kot_server/tls` with `openssl` (needs `openssl` once); `tlsCert`/`tlsKey`
use your own PEM files instead. The certificate is not verified by clients (it is
self-signed), so it only hides the traffic; the game token proves who a player is.

### Playing at different times
The server keeps the game; nobody needs to be connected. When it is your turn, connect, play,
end your turn and leave: the game waits at the turn of a player who is away (no computer plays
for anyone), and a battle against someone who is away is fought by the computer for them.
Your client makes a game token for each server (a long random secret, kept in its settings); the server keeps only
a hash of it with your seat, so you, and nobody else, get the seat back when you connect again.
To play your seats from another computer, open the Game menu there (`Game token...`) on the first
computer, copy the token, and enter it on the start screen of the other one (or keep it to yourself
like a password). Anyone may save; only the host
may replace an existing save. A player who is out of the game may still join and watch.
`savesMaxMB` is the most space the save folder may use (0 or missing: no limit); when a
save would go over it, the server first removes the oldest saved games (never campaign
progress) until it fits. Saves go to `~/.local/share/kot_server/saves` by default.

## Build for playing

The Debug build above is for development and tests; it plays slowly. For playing, use an
optimized build that still has debug info (so crash reports show function names and lines).
`make play` does the two cmake commands below for you, in `build-play/`, leaving the Debug
build and the tests alone; it does not start the game — run the client on the last line:

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
`data/` folder). On the start screen, enter your name and pick a way to play:
**Solo game** (you against the computer), **Hot seat** (several players take turns at this
computer), **Network game** (join the server under the button: address, port and game token)
or **Load game** (start a local server and pick a saved game in the lobby).
Both local games start `kot_server` on this computer. In the lobby you can change the slots,
map and rules and then Start; the host needs no Ready, other humans do.
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
src/editor  the map / scenario editor
data/       game content (JSON) and art (data/art generated, data/custom_art hand-made, PNG)
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
