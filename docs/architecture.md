# Architecture

citsy is a headless C++ reimplementation of the [Bitsy](https://bitsy.org) game engine. The core library parses `.bitsy` game data, simulates the world, and produces logical framebuffers and audio parameters. A separate **Host** backend turns those buffers into pixels and sound.

---

## Layer diagram

```
┌─────────────────────────────────────────────────────────────┐
│                     Your application                        │
│  (main loop, window, scaling, file picker, etc.)            │
└──────────────────────────┬──────────────────────────────────┘
                           │ implements
                           ▼
┌─────────────────────────────────────────────────────────────┐
│                      Host backend                           │
│  raylib · 32blit · MockHost · custom platform               │
│  - poll input & time        - play square-wave audio        │
│  - blit buffers to display  - optional logging              │
└──────────────────────────┬──────────────────────────────────┘
                           │ citsy::Host interface
                           ▼
┌─────────────────────────────────────────────────────────────┐
│                      citsy core (library)                   │
│  Engine · Parser · Game model · Dialog VM · Render · Font · Sound · Transition │
│  - simulation step          - memory blocks (video/map/text)│
│  - script execution         - palette & tile cache          │
└──────────────────────────┬──────────────────────────────────┘
                           │ reads
                           ▼
┌─────────────────────────────────────────────────────────────┐
│                   .bitsy game data                          │
└─────────────────────────────────────────────────────────────┘
```

The engine **never** calls platform APIs (no windowing, GPU, or audio libraries in the core). Backends live under `backends/` and are optional CMake targets.

---

## Hard boundaries

| Rule | Detail |
|---|---|
| Core is headless | The `citsy` library must not link raylib, SDL, OpenGL, or any audio/window library |
| Engine never draws | Core writes memory blocks; only backends call platform APIs in `Host::present()` |
| Backends are optional | Separate CMake targets, off by default (except MockHost, which is header-only) |
| No JS runtime | Engine logic is native C++; `.bitsy` files are parsed directly |
| No editor | Author games with [bitsy.org](https://bitsy.org); citsy is runtime only |

---

## Source layout

```
citsy/
├── include/citsy/          # Public API
│   ├── engine.hpp          # Engine class, ParseError
│   ├── host.hpp            # Host interface
│   ├── types.hpp           # Color, Button, SoundChannel, constants
│   └── version.hpp
├── src/
│   ├── parser/             # .bitsy lexer / parser  →  Game
│   ├── model/              # Game, Room, Tile, Sprite, Item, …
│   ├── engine/             # Simulation loop, movement, collision, exits
│   ├── dialog/             # Script interpreter (variables, lists, {print*}, {ava}…)
│   ├── render/             # Logical compositor → map1 / map2 / video
│   ├── font/               # .bitsyfont + built-in ascii_small
│   ├── sound/              # Blip / tune → SoundChannel params
│   └── transition/         # Fade / wave / tunnel / slide
├── backends/
│   ├── mock/               # MockHost test double (header-only)
│   ├── raylib/             # (planned) Reference desktop player
│   └── 32blit/             # (planned) Handheld backend
├── tests/
│   ├── unit/               # Catch2 unit tests
│   └── data/               # Sample .bitsy fixtures
└── examples/
    └── minimal/            # Load a game with MockHost
```

### Public vs internal API

| Location | Visibility | Purpose |
|---|---|---|
| `include/citsy/` | Public | Stable consumer-facing headers |
| `src/model/`, `src/parser/`, `src/dialog/`, `src/render/` | Internal | Game data, parser, linear dialog, compositor; tests may include directly |
| `backends/mock/` | Backend | Test double; not linked into the core library |

Embedders depend only on `include/citsy/`. Internal headers use paths like `"src/model/game.hpp"` and are not installed as part of the public ABI.

---

## Core modules

### Parser (`src/parser/`)

Reads plain-text `.bitsy` files and builds an in-memory `Game` model. Handles version headers, palettes, tiles, sprites, items, rooms, dialogues, variables, and endings. Throws `ParseError` (with optional line number) on malformed input.

The internal entry point is `citsy::parse(std::string_view)`. `Engine` constructors call this internally.

### Model (`src/model/`)

Strongly typed C++ structures mirroring Bitsy entities:

| Type | Source segment | Notes |
|---|---|---|
| `Palette` | `PAL` | Named color sets; index 0 = background, 1 = tile, 2 = sprite |
| `Tile` | `TIL` | 8×8 pixel art, wall flag, animation frames, `COL n` |
| `Sprite` | `SPR` | Animated character; avatar is always id `A` |
| `Item` | `ITM` | Collectible object |
| `Room` | `ROOM` | 16×16 tile grid, items, exits, endings, palette |
| `Dialogue` | `DLG` | Script text; interpreter evaluates variables, lists, and actions |
| `Variable` | `VAR` | Global number or string state |
| `Ending` | `END` | End-game message |
| `Tune` | `TUNE` | Looping two-channel melody |
| `Blip` | `BLIP` | One-shot sound effect |

`Game` aggregates these in hash maps keyed by entity id and provides helpers like `avatar()` and `start_room_id()`.

### Engine (`src/engine/`)

The `Engine` class owns:

1. A parsed `Game` (via pimpl)
2. Runtime state (current room, avatar position, open dialog, remaining room items)
3. Memory blocks the host reads each frame

**Lifecycle:**

```cpp
citsy::Engine engine = citsy::Engine::from_file("game.bitsy");
MyHost host;

engine.start(host);          // init runtime, call host.on_engine_ready()
while (engine.is_running()) {
    engine.update(host);     // simulate one step, call host.present()
}
```

**Phase 3 behavior:** `update()` also advances flipbook animation (400 ms), plays exit transitions in `GraphicsMode::Video`, renders dialog glyphs (including RTL), and fills `sound1` / `sound2` from blips and room tunes.

### Dialog (`src/dialog/`)

Phase 1 extracts linear text pages from `DLG` source (`extract_dialog_pages`). The script interpreter (`DialogVM`) evaluates `{print}`, assignments, conditionals, lists, inventory, `{ava}`/`{pal}`/`{tune}`/`{blip}`, `{exit}`, `{end}`, `{lock}`, `{printSprite}`/`{printTile}`/`{printItem}`, and text effects `{wvy}`/`{shk}`/`{rbw}`/`{clr}`.

### Render (`src/render/`)

`compose_room()` fills:

- `map1` — background tile codes for the current room
- `map2` — items, non-avatar sprites, then the avatar
- `video` — 128×128 colour indices (tiles opaque, sprites/items transparent)

The host still receives `GraphicsMode::Map` during gameplay and `GraphicsMode::Video` during transitions. Animated drawings use the current flipbook frame (400 ms). `BGC` / transparent backgrounds are honoured when compositing.

### Font, sound, and transitions

| Module | Role |
|---|---|
| `font/` | Parse `.bitsyfont` (variable-width glyphs) and blit into the textbox; ships a built-in `ascii_small` 6×8 font. `TEXT_DIRECTION RTL` reverses line layout. |
| `sound/` | Turns `BLIP` / `TUNE` data into `SoundChannel` frequency, volume, pulse, and duration. Hosts play the square waves. |
| `transition/` | `fade_w`, `fade_b`, `wave`, `tunnel`, `slide_u`/`d`/`l`/`r` written into the 128×128 video buffer. |

---

## Update loop

Each frame follows the [Bitsy System API](https://make.bitsy.org/docs/technical/system/) contract:

```
┌──────────┐    delta_time_ms(), button()    ┌──────────┐
│   Host   │ ──────────────────────────────► │  Engine  │
│          │                                 │          │
│          │ ◄────────────────────────────── │          │
└──────────┘   present(buffers, palette,     └──────────┘
                sound, gfx_mode, …)
```

1. Host reports elapsed time and button state.
2. Engine runs one simulation step (movement, dialog, transitions, sound).
3. Engine writes memory blocks.
4. Host reads blocks in `present()` and draws/plays audio.

---

## Memory blocks

Fixed logical resolution; the host scales output as desired.

| Block | Size | Purpose |
|---|---|---|
| `video` | 128 × 128 | Per-pixel color indices (`GraphicsMode::Video`) |
| `map1` | 16 × 16 | Background tile IDs (`GraphicsMode::Map`) |
| `map2` | 16 × 16 | Sprite/item overlay tile IDs |
| `textbox` | dynamic | Dialog glyph color indices |
| `tile[n]` | 8 × 8 each | Cached tile pixel patterns (future) |
| `sound1`, `sound2` | — | Frequency, volume, pulse, duration |

Constants (in `types.hpp`):

| Name | Value |
|---|---|
| `kTileSize` | 8 px |
| `kMapSize` | 16 tiles |
| `kVideoSize` | 128 px |

Color indices refer to the active palette passed to `present()`. Extended palettes (`COL n`) are supported in parsed data.

---

## Host interface

Backends implement `citsy::Host` (`include/citsy/host.hpp`):

| Method | Direction | Purpose |
|---|---|---|
| `delta_time_ms()` | host → engine | Frame delta in milliseconds |
| `button(Button)` | host → engine | Logical button state (Up/Down/Left/Right/Ok/Menu) |
| `on_engine_ready()` | engine → host | Called once after `start()` |
| `log(message)` | engine → host | Optional diagnostic sink |
| `present(...)` | engine → host | Full frame state after each `update()` |

### Backend implementations

| Backend | Status | Use case |
|---|---|---|
| **MockHost** | Implemented | Unit tests; records `PresentSnapshot` per frame |
| **raylib** | Planned | Reference desktop player with window and audio |
| **32blit** | Planned | Handheld device backend |

MockHost is header-only (`backends/mock/mock_host.hpp`). It stores every `present()` call in `snapshots` so tests can assert on buffer sizes, palette colors, and graphics mode without a display.

---

## Data flow example

Loading and stepping a game:

```
game.bitsy  ──►  parse()  ──►  Game model
                                  │
                                  ▼
                            Engine::Impl
                            (buffers + state)
                                  │
                     update(host) │ do_present()
                                  ▼
                            Host::present()
                            (palette, video, map1, map2, …)
```

For a working code sample, see `examples/minimal/main.cpp`.

---

## Compatibility model

citsy targets import of standard `.bitsy` files from the Bitsy editor. Compatibility is incremental — each feature gets fixture tests in `tests/data/`. Cross-check behavior against:

- [Bitsy System API](https://make.bitsy.org/docs/technical/system/)
- [bitsy-parser](https://docs.rs/bitsy-parser) (Rust reference)
- [bitsybox](https://github.com/le-doux/bitsybox) (historical SDL reference)

See the [roadmap](../README.md#roadmap) for current phase status.
