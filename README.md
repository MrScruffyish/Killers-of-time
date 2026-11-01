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

./build/bin/kot_server            # default port 45454, or: --port 5000
./build/bin/kot_client            # in another terminal; click Connect
```

Log filtering example: `QT_LOGGING_RULES="kot.*.debug=true" ./build/bin/kot_server`

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
