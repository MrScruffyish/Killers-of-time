# CLAUDE.md

Turn-based fantasy strategy game "Killers of Time": Qt 6.4 / C++20 / CMake, server-authoritative
client-server. Design: `PROJECT.md`. Progress, roadmap and decisions: `STATUS.md` (keep it updated).

## Build and test
```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug   # once
cmake --build build && ctest --test-dir build --output-on-failure
```
GUI test `tst_clientsmoke` runs offscreen. The user runs the real GUI on Ubuntu/Qt Creator;
ask them to try things on screen when that matters (they are happy to do tasks).

## Layout
`src/core` (model, rules, content; QtCore only, no GUI/net) · `src/net` (protocol) ·
`src/ai` · `src/server` (lobby, GameSession) · `src/client` (Qt Widgets) · `src/tools` (content tools, `docs/tools.md`) · `tests/` (Qt Test,
one `tst_*.cpp` per area, registered in `tests/CMakeLists.txt`) · `data/` (strict JSON content) ·
`docs/` (protocol, data and save formats).

## Working rules
- **Breadth first:** thin, simple version of every game part first; depth later (STATUS.md "Order of work").
- Naming stays Qt style: `PascalCase` types, `camelCase` functions/vars, `m_` members, `kConst`.
  Data ids `snake_case`, JSON fields `camelCase`.
- Rules live in `core/rules` with unit tests; UI only renders state and sends commands.
- All randomness through the game state's `Rng`. Unknown JSON fields are errors.
- Update `docs/*.md` when the protocol, data or save format changes.
- One slice per commit ("M6a: ...") after build + all tests pass; commit only when the user says so.
