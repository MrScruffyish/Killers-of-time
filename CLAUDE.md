# CLAUDE.md

Turn-based fantasy strategy game "Killers of Time": Qt 6.4 / C++20 / CMake, server-authoritative
client-server. Design and decisions: `PROJECT.md`. Open work: `TODO.md` (keep it updated).

## Build and test
Do not compile or run tests yourself. Add or update the tests with each change and mark the
verification **MANUAL** in `TODO.md`; the user builds and runs them.
```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug   # once
cmake --build build && ctest --test-dir build --output-on-failure
```
GUI test `tst_clientsmoke` runs offscreen. The user runs the real GUI on Ubuntu/Qt Creator;
ask them to try things on screen when that matters (they are happy to do tasks).

## Layout
`src/core` (model, rules, content; QtCore only, no GUI/net) · `src/net` (protocol) ·
`src/ai` · `src/server` (lobby, GameSession) · `src/client` (Qt Widgets; loads PNG art only) · `src/artgen` (art drawn in code, for kot_artexport) · `src/tools` (content tools, `docs/tools.md`) · `tests/` (Qt Test,
one `tst_*.cpp` per area, registered in `tests/CMakeLists.txt`) · `data/` (strict JSON content) ·
`docs/` (protocol, data and save formats).

## Working rules
- **Breadth first:** thin, simple version of every game part first; depth later.
- Naming stays Qt style: `PascalCase` types, `camelCase` functions/vars, `m_` members, `kConst`.
  Data ids `snake_case`, JSON fields `camelCase`.
- Rules live in `core/rules` with unit tests; UI only renders state and sends commands.
- All randomness through the game state's `Rng`. Unknown JSON fields are errors.
- Update `docs/*.md` when the protocol, data or save format changes.
- Keep `TODO.md` current: numbered TODO/TBD/DONE lists; move finished, committed items to DONE.
- One slice per commit ("M6a: ...") after the user's build and tests pass; commit only when the user says so.
