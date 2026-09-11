# citsy

A headless C++ reimplementation of the [Bitsy](https://codeberg.org/adamledoux/bitsy) game engine — the little engine for little games, worlds, and stories.

citsy is **engine only**. It does not link against raylib, 32blit, or any other graphics or audio library. Instead, it exposes a small **Host System API** that a separate front-end (your renderer, your platform layer) implements. The engine owns game logic, simulation, scripting, and framebuffers; the host owns pixels on screen, input devices, and speakers.

> **Note:** This project is a clean-room engine inspired by Bitsy. It is not affiliated with Adam Le Doux or the official Bitsy project. Game data produced by the Bitsy editor (`.bitsy` files) is the intended interchange format.

---

## Table of contents

- [Why citsy?](#why-citsy)
- [Design goals](#design-goals)
- [Non-goals](#non-goals)
- [Documentation](#documentation)
- [Architecture](#architecture)
- [Host System API](#host-system-api)
- [Engine internals](#engine-internals)
- [Bitsy data model](#bitsy-data-model)
- [File format compatibility](#file-format-compatibility)
- [Project layout](#project-layout)
- [Building](#building)
- [Usage sketch](#usage-sketch)
- [Roadmap](#roadmap)
- [References](#references)

---

## Why citsy?

[Bitsy](https://bitsy.org) is a browser-based tool for making tiny tile-based adventure games. The reference implementation couples the editor, JavaScript engine, and web renderer together. [bitsybox](https://github.com/le-doux/bitsybox) shows that the engine can run on desktop by implementing a thin **system layer** (originally SDL + Duktape).

citsy takes that separation further:

| Layer | Responsibility |
|---|---|
| **citsy core** | Parse `.bitsy` data, simulate the world, run dialog scripts, produce memory buffers and audio parameters |
| **Host backend** | Map buffers to textures, poll input, play sound, present frames (raylib, 32blit, custom) |

This makes the engine embeddable in game jams, retro handhelds, test harnesses, and server-side validators without dragging in a windowing library.

---

## Design goals

- **Modern C++** (C++20 or later): strong types, `std::span`, `std::optional`, `std::variant`, RAII, no raw owning pointers in public APIs.
- **Headless core**: zero dependency on windowing, GPU, or audio libraries.
- **Bitsy-compatible semantics**: rooms, tiles, sprites, items, exits, endings, variables, and dialog scripting behave like the reference engine for supported versions.
- **Pluggable host**: a single `Host` interface mirrors the [Bitsy System API](https://make.bitsy.org/docs/technical/system/) used by bitsybox and the web runtime.
- **Deterministic simulation**: fixed logical resolution, predictable update order, suitable for replay and automated testing.
- **Testable**: core logic covered by unit tests with a mock host; no GPU required in CI.

---

## Non-goals

- **Editor or authoring tools** — use [bitsy.org](https://bitsy.org) or compatible editors to create games.
- **HTML export** — citsy produces frames and state, not a self-contained web page.
- **JavaScript embedding** — the reference engine runs on JS; citsy reimplements engine logic in native C++.
- **Full feature parity on day one** — compatibility is incremental; see [Roadmap](#roadmap).

---

## Documentation

Detailed guides live in [`docs/`](docs/README.md):

| Guide | Contents |
|---|---|
| [Architecture](docs/architecture.md) | Layering, modules, data flow, memory blocks, Host interface |
| [Tools & build](docs/tools.md) | CMake options, compilers, dependencies, build targets |
| [Testing](docs/testing.md) | Running tests, writing tests, fixtures, MockHost |
| [Bitsy data model](docs/bitsy/data-model.md) | Entity types, properties, file syntax, relationships |
| [Dialog scripting](docs/bitsy/dialog.md) | Variables, lists, inventory, endings |

For agent-oriented conventions, see [AGENTS.md](AGENTS.md).

---

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                     Your application                        │
│  (main loop, window, scaling, file picker, etc.)            │
└──────────────────────────┬──────────────────────────────────┘
                           │ implements
                           ▼
┌─────────────────────────────────────────────────────────────┐
│                      Host backend                           │
│  raylib · 32blit · headless mock · your platform            │
│  - input polling          - audio output                    │
│  - buffer → texture blit  - logging                         │
└──────────────────────────┬──────────────────────────────────┘
                           │ Host interface
                           ▼
┌─────────────────────────────────────────────────────────────┐
│                      citsy core                             │
│  Engine · Renderer (logical) · Dialog VM · Parser           │
│  - game simulation        - memory blocks (video/map/text)  │
│  - script execution       - palette & tile cache            │
└──────────────────────────┬──────────────────────────────────┘
                           │ reads
                           ▼
┌─────────────────────────────────────────────────────────────┐
│                   .bitsy game data                          │
└─────────────────────────────────────────────────────────────┘
```

### Update loop

Each frame follows the same contract as the reference engine:

1. Host reports elapsed time (`dt` in milliseconds) and current input state.
2. Engine runs one simulation step: movement, collisions, dialog advancement, transitions, sound channel updates.
3. Engine writes to shared **memory blocks** (see below).
4. Host reads those blocks and draws them (scaled as desired).

The engine never calls platform APIs directly.

---

## Host System API

The host implements `citsy::Host` — a C++ analogue of the `bitsy` global object from the [Bitsy System API](https://make.bitsy.org/docs/technical/system/). The engine calls into the host for **input** and **time**; the host reads **memory blocks** and **audio state** that the engine owns.

### Constants

| Name | Value | Meaning |
|---|---|---|
| `kTileSize` | 8 | Pixels per tile edge |
| `kMapSize` | 16 | Room width/height in tiles |
| `kVideoSize` | 128 | Main framebuffer edge in pixels (16 × 8) |
| `kGfxVideo` | 0 | Direct per-pixel framebuffer mode |
| `kGfxMap` | 1 | Tilemap mode (normal gameplay) |
| `kTxtHirez` | 0 | Textbox at 2× pixel scale |
| `kTxtLorez` | 1 | Textbox at 4× pixel scale |

### Buttons

| Code | Action |
|---|---|
| `Up` | Move avatar / menu up |
| `Down` | Move avatar / menu down |
| `Left` | Move avatar / menu left |
| `Right` | Move avatar / menu right |
| `Ok` | Interact, advance dialog |
| `Menu` | Pause / restart (host-defined) |

The host normalizes keyboard, gamepad, and touch into these six logical buttons.

### Memory blocks

The engine maintains fixed logical buffers. The host reads them after each update; the engine may resize the textbox buffer when dialog layout changes.

| Block | Size (typical) | Purpose |
|---|---|---|
| `Video` | 128 × 128 | Per-pixel color indices in `kGfxVideo` mode |
| `Textbox` | w × h (dynamic) | Dialog text rendered as color indices |
| `Map1` | 16 × 16 | Primary tilemap (tile IDs) |
| `Map2` | 16 × 16 | Overlay tilemap (sprites/items layer) |
| `Tile[n]` | 8 × 8 each | Cached tile pixel patterns |
| `Sound1`, `Sound2` | — | Channel frequency, volume, pulse, duration |

Color indices refer to the active palette (see [Palettes](#palettes)).

### Host interface (conceptual)

```cpp
namespace citsy {

struct Color { std::uint8_t r, g, b; };

struct SoundChannel {
    bool active;
    int duration_ms;
    int frequency_hz;
    float volume;      // 0.0 – 1.0
    PulseWave pulse;   // 1/8, 1/4, 1/2 duty
};

enum class GraphicsMode { Video, Map };
enum class TextMode { HiRez, LoRez };

class Host {
public:
    virtual ~Host() = default;

    // Called once at startup
    virtual void on_engine_ready() {}

    // Time & input — host → engine
    virtual double delta_time_ms() const = 0;
    virtual bool button(Button code) const = 0;

    // Optional: host logging sink
    virtual void log(std::string_view message) {}

    // Called after each engine step — host reads engine state
    virtual void present(
        GraphicsMode gfx_mode,
        TextMode txt_mode,
        std::span<const Color> palette,
        std::span<const std::uint8_t> video,    // 128×128 indices
        std::span<const std::uint8_t> map1,     // 16×16 tile IDs
        std::span<const std::uint8_t> map2,
        TextboxView textbox,                    // may be hidden
        SoundChannel sound1,
        SoundChannel sound2
    ) = 0;
};

} // namespace citsy
```

Backends implement `present()` differently:

- **raylib** — upload indices to a 128×128 `Image` / `Texture`, scale with `DrawTexturePro`, mix square-wave audio.
- **32blit** — blit directly into the handheld framebuffer at native resolution.
- **MockHost** — record buffer snapshots for unit tests; no window.

### Palette updates

When a game or transition changes colors, the engine updates its internal palette and passes the full RGB table to `present()`. Hosts should not assume a fixed 3-color palette — extended palettes (COL 3, COL 4, …) are supported in modern Bitsy data.

---

## Engine internals

Planned modules inside the core library:

| Module | Role |
|---|---|
| `parser/` | Load and serialize `.bitsy` text; validate segments |
| `model/` | `Game`, `Room`, `Tile`, `Sprite`, `Item`, `Dialogue`, `Variable`, … |
| `engine/` | Main loop, avatar movement, collision, room transitions |
| `dialog/` | Script interpreter: variables, lists, inventory, endings |
| `render/` | Logical renderer — fills video/map/textbox memory blocks (no GPU) |
| `font/` | Built-in and custom `.bitsyfont` glyph rendering into textbox buffer |
| `sound/` | Two-channel square-wave parameter generation |
| `transition/` | Room transition effects (fade, wipe, etc.) in video mode |

### Graphics modes

- **`kGfxMap`** — Default gameplay. Engine composes the room from tile IDs in `Map1`, then draws sprites and items into `Map2` or directly to output according to Bitsy rules.
- **`kGfxVideo`** — Used during transitions and special effects. Engine writes individual pixel color indices into the `Video` buffer.

The host chooses how to interpret buffers based on the `gfx_mode` argument to `present()`.

### Dialog scripting

Dialog is not just text — it is Bitsy's lightweight game logic language. The engine evaluates:

- **Text lines** and `{print …}` expressions
- **Variable assignment** — `{name = value}`, `{count = count + 1}`
- **Conditionals** — branching lists based on variable values and item counts
- **Actions** — give/take items, change rooms via exits, trigger endings

Scripts are parsed from `DLG` segments in game data. citsy implements the same expression grammar as the reference engine for supported versions.

---

## Bitsy data model

A Bitsy game is a collection of typed segments in a plain-text `.bitsy` file. See **[docs/bitsy/data-model.md](docs/bitsy/data-model.md)** for the full reference: every entity type, property, file syntax example, relationship diagram, and C++ struct mapping.

Quick summary of core entities:

| Entity | Description |
|---|---|
| **Palette (`PAL`)** | Named color sets (background, tile, sprite, plus optional extended colors) |
| **Tile (`TIL`)** | 8×8 pixel art; optional wall flag, animation frames (`>` separator), color index |
| **Sprite (`SPR`)** | Animated 8×8 character or object; avatar is always id `A` |
| **Item (`ITM`)** | Collectible 8×8 object with inventory semantics |
| **Room (`ROOM`)** | 16×16 grid of tile IDs, plus placed items, exits, and endings |
| **Exit (`EXT`)** | Warp tile: target room, position, optional dialog, transition effect |
| **Dialogue (`DLG`)** | Script attached to sprites, items, or exits |
| **Variable (`VAR`)** | Global number or string state |
| **Ending (`END`)** | End-game message (top-level) or tile trigger (room sub-key) |

---

## File format compatibility

citsy targets import of standard `.bitsy` files exported from the editor.

| Feature | Target support |
|---|---|
| `# BITSY VERSION` header | Yes |
| Comma-separated room format (`! ROOM_FORMAT`) | Yes |
| Legacy contiguous room format (single-char tile IDs) | Planned |
| `SET` vs `ROOM` (historical naming) | Planned |
| Multi-frame animation | Yes |
| Extended palettes (`COL n`) | Yes |
| Variables and dialog scripting | Yes |
| Custom fonts (`FONT` / `.bitsyfont`) | Planned |
| `TEXT_DIRECTION RTL` | Planned |
| Sound (engine generates channel params; host plays audio) | Yes |

Exact version targets will be documented as compatibility tests land in `tests/data/`.

---

## Project layout

```
citsy/
├── include/citsy/          # Public headers
│   ├── engine.hpp          # Main Engine class
│   ├── host.hpp            # Host interface
│   ├── types.hpp           # Color, Button, memory block views
│   └── version.hpp
├── src/
│   ├── parser/             # .bitsy lexer / parser
│   ├── model/              # Data structures
│   ├── engine/             # Simulation & game loop
│   ├── dialog/             # Script interpreter
│   ├── render/             # Logical framebuffer compositor
│   ├── font/
│   └── sound/
├── backends/               # Optional; not part of core library
│   ├── raylib/             # Reference desktop backend
│   ├── mock/               # Test double
│   └── 32blit/             # Handheld backend (planned)
├── tests/
│   ├── unit/
│   └── data/               # Sample .bitsy files
├── examples/
│   └── minimal/            # Load a game with MockHost
├── CMakeLists.txt
└── README.md
```

The `citsy` CMake target is a static or shared library with **no** link dependency on raylib, 32blit, or similar. Backends are separate targets that link both `citsy` and their platform library.

---

## Building

Requirements:

- CMake 3.20+
- A C++20-capable compiler (GCC 11+, Clang 14+, MSVC 19.29+)

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build
```

See [docs/tools.md](docs/tools.md) for CMake options, build targets, and dependency details. See [docs/testing.md](docs/testing.md) for test filtering, fixtures, and writing new tests.

To build with the raylib reference backend (optional):

```bash
cmake -B build -DCITSY_BUILD_RAYLIB_BACKEND=ON
cmake --build build
```

---

## Usage sketch

### Embed the engine with a custom host

```cpp
#include <citsy/engine.hpp>
#include <citsy/host.hpp>

class MyHost : public citsy::Host {
public:
    double delta_time_ms() const override { return dt_; }
    bool button(citsy::Button b) const override { return keys_[b]; }

    void present(/* ... */) override {
        // Blit video/map buffers to your display
        // Play sound1 / sound2 if active
    }

    void set_delta(double dt) { dt_ = dt; }
    void set_key(citsy::Button b, bool down) { keys_[b] = down; }

private:
    double dt_ = 0;
    bool keys_[6] = {};
};

int main() {
    auto game_data = citsy::load_file("my_game.bitsy");
    citsy::Engine engine(std::move(game_data));

    MyHost host;
    engine.start();

    while (engine.is_running()) {
        host.set_delta(16.667); // ~60 fps
        engine.update(host);
        // host.present() was called from engine.update()
    }
}
```

### Reference raylib backend (optional)

```bash
./build/examples/raylib_player path/to/game.bitsy
```

The raylib example creates a window, scales the 128×128 logical framebuffer with integer scaling, maps arrow keys and Z/Enter to Bitsy buttons, and plays square-wave audio.

---

## Roadmap

Development is staged toward practical compatibility with games made in current Bitsy versions.

- [x] **Phase 0 — Foundation**
  - [x] `.bitsy` parser and in-memory `Game` model
  - [x] `Host` interface and `MockHost`
  - [x] Unit tests against sample game files

- [x] **Phase 1 — Playable core**
  - [x] Avatar movement and wall collision
  - [x] Room rendering into map buffers
  - [x] Sprite and item drawing
  - [x] Exit transitions between rooms
  - [x] Basic dialog (linear text)

- [x] **Phase 2 — Scripting & state**
  - [x] Variables (numbers and strings)
  - [x] Conditional dialog branches
  - [x] Item give/take and inventory
  - [x] Endings

- [ ] **Phase 3 — Polish**
  - [ ] Animation timing
  - [ ] Transition effects (video mode)
  - [ ] Custom fonts
  - [ ] Sound channel output
  - [ ] RTL text direction

- [ ] **Phase 4 — Backends**
  - [ ] raylib reference player
  - [ ] 32blit backend (community contribution welcome)

Compatibility fixtures will be drawn from the [Bitsy community](https://bitsy.org) and existing open-source parsers such as [bitsy-parser](https://docs.rs/bitsy-parser).

---

## References

- [Bitsy](https://bitsy.org) — official editor and website
- [adamledoux/bitsy](https://codeberg.org/adamledoux/bitsy) — reference engine and editor source (JavaScript)
- [Bitsy System API](https://make.bitsy.org/docs/technical/system/) — host system layer specification
- [bitsybox](https://github.com/le-doux/bitsybox) — desktop runtime with SDL host implementation
- [bitsy-parser](https://docs.rs/bitsy-parser) — Rust parser useful for cross-checking file format behavior
- [Bitsy Wiki / FAQ](https://bitsy.fandom.com/wiki/FAQ) — community documentation on variables, colors, and data format

---

## License

License to be determined. Bitsy itself is open source; see the [reference repository](https://codeberg.org/adamledoux/bitsy) for its terms. Games you create with the Bitsy editor remain yours.
