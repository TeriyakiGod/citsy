# citsy — Agent Instructions

Headless C++ reimplementation of the [Bitsy](https://bitsy.org) game engine. Inspired by [adamledoux/bitsy](https://codeberg.org/adamledoux/bitsy); not affiliated with the official project.

Read `README.md` for full design docs and `docs/` for detailed guides on [architecture](docs/architecture.md), [tools & build](docs/tools.md), and [testing](docs/testing.md). This file tells agents **how to work on the codebase**.

---

## Mission

Build a **Bitsy-compatible game engine** in modern C++ that:

1. Parses and simulates `.bitsy` game data
2. Produces logical framebuffers and audio parameters (no GPU/window code)
3. Exposes a pluggable `Host` interface for platform backends (32blit player, MockHost, custom)

---

## Hard boundaries

These are non-negotiable. Violating them is a design bug.

| Rule | Detail |
|---|---|
| **Core is headless** | `citsy` library must not link against 32blit, OpenGL, SDL, or any audio/window library |
| **No editor** | Do not build authoring tools, HTML export, or a Bitsy clone UI |
| **No JS runtime** | Reimplement engine logic in C++; do not embed Duktape/V8 to run the reference JS |
| **Engine never draws** | Core writes memory blocks; only backends call platform APIs in `Host::present()` |
| **Display hosts are separate** | MockHost lives in `backends/mock/`. The 32blit player is a separate repo (`citsy-32blit`). |

When adding a dependency, ask: *does this belong in core or in a backend?* If it opens a window or plays sound, it is a backend concern.

---

## Architecture

```
Application  →  Host backend (32blit / mock)  →  citsy core  →  .bitsy data
```

**Update loop** (each frame):

1. Host provides `delta_time_ms()` and `button()` state
2. `Engine::update(host)` runs simulation (movement, dialog, transitions, sound params)
3. Engine fills memory blocks; host receives them via `present()`

### Memory blocks (engine-owned)

| Block | Size | Notes |
|---|---|---|
| Video | 128×128 | Color indices; used in `GfxVideo` mode |
| Map1 / Map2 | 16×16 | Tile IDs; normal gameplay in `GfxMap` mode |
| Textbox | dynamic | Dialog glyph indices |
| Tile cache | 8×8 each | Pixel patterns for tile IDs |
| Sound1 / Sound2 | — | Frequency, volume, pulse, duration (host plays audio) |

Constants: `kTileSize=8`, `kMapSize=16`, `kVideoSize=128`.

Buttons: `Up`, `Down`, `Left`, `Right`, `Ok`, `Menu` — host normalizes keyboard/gamepad/touch.

---

## Directory layout

```
include/citsy/     Public API (engine.hpp, host.hpp, types.hpp)
src/parser/        .bitsy lexer/parser
src/model/         Game, Room, Tile, Sprite, Item, Dialogue, Variable, …
src/engine/        Simulation, collisions, room transitions
src/dialog/        Script interpreter ({var}, conditionals, actions)
src/render/        Logical compositor → memory blocks (no GPU)
src/font/          .bitsyfont rendering into textbox buffer
src/sound/         Square-wave channel parameter generation
src/transition/    Fade/wipe effects (video mode)
backends/mock/     Test double for unit tests
tests/unit/        Core tests, no GPU
tests/data/        Sample .bitsy fixtures
examples/minimal/  MockHost demo
```

Place new code in the matching module. Do not flatten everything into one directory.

---

## C++ conventions

- **Standard**: C++20 minimum (`std::span`, `std::optional`, `std::variant`, `std::string_view`)
- **Ownership**: RAII; no raw owning pointers in public headers
- **API surface**: Public types live in `include/citsy/`; implementation details stay in `src/`
- **Naming**: `snake_case` functions/variables, `PascalCase` types, `kConstant` or `constexpr` for engine constants
- **Errors**: Prefer `std::expected` or explicit result types for parse/load failures; avoid exceptions for control flow in hot paths
- **Tests**: Core logic must be testable with `MockHost`; no window required in CI

Match existing style in each file before introducing new patterns.

---

## Host interface

Backends implement `citsy::Host`:

```cpp
class Host {
    virtual double delta_time_ms() const = 0;
    virtual bool button(Button code) const = 0;
    virtual void present(/* gfx_mode, palette, buffers, sound */) = 0;
};
```

- Engine **calls** host for time and input
- Engine **passes** render/audio state to host via `present()` after each update
- `MockHost` records buffer snapshots for assertions

Do not add rendering helpers to the core library "for convenience." Put them in the relevant backend.

---

## Bitsy compatibility

Primary interchange format: **`.bitsy` text files** from the Bitsy editor.

Key segments: `PAL`, `TIL`, `SPR`, `ITM`, `ROOM`, `DLG`, `VAR`, `END`, `EXT`.

When implementing parser or simulation behavior, cross-check against:

- [Bitsy System API](https://make.bitsy.org/docs/technical/system/) — host layer contract
- [bitsy-parser](https://docs.rs/bitsy-parser) — Rust reference for file format
- [bitsybox](https://github.com/le-doux/bitsybox) — desktop system layer (SDL; historical reference only)

Avatar sprite id is always `A`. Rooms are 16×16 tiles. Support comma-separated room format and extended palettes (`COL n`).

Implement compatibility **incrementally** with fixture tests in `tests/data/`.

---

## What not to build (unless explicitly asked)

- Bitsy editor or level design UI
- HTML/itch.io export pipeline
- Extra desktop toolkits (the 32blit SDL host already covers desktop)
- Full feature parity in a single PR — follow the roadmap phases

---

## Roadmap (work in order)

1. **Phase 0** — Parser, `Game` model, `Host` + `MockHost`, unit tests
2. **Phase 1** — Movement, collision, room render, sprites/items, exits, linear dialog
3. **Phase 2** — Variables, conditional dialog, inventory, endings
4. **Phase 3** — Animation, transitions, fonts, sound params, RTL
5. **Phase 4** — 32blit player (companion `citsy-32blit` repo)

Check README roadmap checkboxes when completing milestones.

---

## Build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build
```

The 32blit player is a sibling project (`citsy-32blit`), not an in-tree CMake option.

Requirements: CMake 3.20+, C++20 compiler (GCC 11+, Clang 14+, MSVC 19.29+). See `docs/tools.md` for CMake options and `docs/testing.md` for test commands and MockHost usage.

---

## Agent checklist (before finishing a task)

- [ ] Code lives in the correct module (`src/` vs `backends/mock`; display hosts stay out of core)
- [ ] Core library has zero platform/media dependencies
- [ ] Public API changes reflected in `include/citsy/`
- [ ] Behavior covered by unit tests where practical
- [ ] `.bitsy` fixtures added for new format or simulation features
- [ ] README/AGENTS.md updated only when architecture or conventions change
