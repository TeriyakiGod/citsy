# Host API

The host implements `citsy::Host` — a C++ analogue of the `bitsy` global object from the [Bitsy System API](https://make.bitsy.org/docs/technical/system/). The engine calls into the host for **input** and **time**; the host reads **memory blocks** and **audio state** that the engine owns.

Headers: [`include/citsy/host.hpp`](https://github.com/TeriyakiGod/citsy/blob/main/include/citsy/host.hpp), [`include/citsy/types.hpp`](https://github.com/TeriyakiGod/citsy/blob/main/include/citsy/types.hpp).

## Constants

| Name | Value | Meaning |
|---|---|---|
| `kTileSize` | 8 | Pixels per tile edge |
| `kMapSize` | 16 | Room width/height in tiles |
| `kVideoSize` | 128 | Main framebuffer edge in pixels (16 × 8) |

| Enumerator | Meaning |
|---|---|
| `GraphicsMode::Video` | Direct per-pixel framebuffer |
| `GraphicsMode::Map` | Tilemap mode (normal gameplay) |
| `TextMode::HiRez` | Textbox at 2× pixel scale |
| `TextMode::LoRez` | Textbox at 4× pixel scale |

## Buttons

| Code | Action |
|---|---|
| `Up` | Move avatar / menu up |
| `Down` | Move avatar / menu down |
| `Left` | Move avatar / menu left |
| `Right` | Move avatar / menu right |
| `Ok` | Interact, advance dialog |
| `Menu` | Pause / restart (host-defined) |

The host normalizes keyboard, gamepad, and touch into these six logical buttons.

## Memory blocks

The engine maintains fixed logical buffers. The host reads them after each update; the engine may resize the textbox buffer when dialog layout changes.

| Block | Size (typical) | Purpose |
|---|---|---|
| `Video` | 128 × 128 | Per-pixel color indices in `GraphicsMode::Video` |
| `Textbox` | w × h (dynamic) | Dialog text rendered as color indices |
| `Map1` | 16 × 16 | Primary tilemap (tile IDs) |
| `Map2` | 16 × 16 | Overlay tilemap (sprites / items) |
| `Sound1`, `Sound2` | — | Channel frequency, volume, pulse, duration |

Color indices refer to the active palette passed into `present()`. Hosts should not assume a fixed 3-color palette — extended palettes (`COL n`) are supported. While a dialog is open the engine also installs true black, white, and rainbow hues at `kTextboxBlack`, `kTextboxWhite`, and `kTextboxRainbow0` so textbox pixels can be decoded independently of the room palette.

## Interface

```cpp
class Host {
public:
    virtual ~Host() = default;

    virtual void on_engine_ready() {}

    virtual double delta_time_ms() const = 0;
    virtual bool button(Button code) const = 0;

    virtual void log(std::string_view message) {}

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
```

| Method | Direction | Purpose |
|---|---|---|
| `delta_time_ms()` | host → engine | Frame delta in milliseconds |
| `button(Button)` | host → engine | Logical button held-state |
| `on_engine_ready()` | engine → host | Once after `Engine::start()` |
| `log(message)` | engine → host | Optional diagnostic sink |
| `present(...)` | engine → host | Full frame state after each `update()` |

### `TextboxView`

| Field | Meaning |
|---|---|
| `visible` | Whether the dialog box should be drawn |
| `x`, `y` | Top-left in 128×128 Bitsy units |
| `width`, `height` | Pixel size of `pixels` |
| `pixels` | Color indices; empty when not visible |

### `SoundChannel`

| Field | Meaning |
|---|---|
| `active` | Host should play this channel |
| `duration_ms` | Remaining note / blip time |
| `frequency_hz` | Square-wave frequency |
| `volume` | 0.0 – 1.0 |
| `pulse` | Duty cycle: `Eighth`, `Quarter`, or `Half` |

The engine never generates PCM. The host synthesizes two pulse waves (or ignores them).

## Graphics modes

- **`GraphicsMode::Map`** — Default gameplay. Compose the room from tile IDs in `map1`, then sprites and items from `map2`.
- **`GraphicsMode::Video`** — Transitions and effects. Read individual pixel color indices from `video`.

Switch on the `gfx_mode` argument to `present()`. The unused buffer is still passed; ignore it.

## Palette updates

When a game or transition changes colors, the engine updates its internal palette and passes the full RGB table to `present()`. Index 0 is background, 1 is tile, 2 is sprite; further indices are extended palette colors.

## Backends

| Backend | Status | Use case |
|---|---|---|
| **MockHost** | Implemented | Unit tests; records `PresentSnapshot` per frame |
| **32blit** | Implemented | Companion [citsy-32blit](https://github.com/TeriyakiGod/citsy-32blit) player |

MockHost is header-only (`backends/mock/mock_host.hpp`). It stores every `present()` call so tests can assert on buffer sizes, palette colors, and graphics mode without a display. See [Testing](testing.md#mockhost).

### Implementing `present()`

- **32blit** — Paletted blit of the 128×128 index buffer; square-wave audio on `channels[]`. Implemented in [citsy-32blit](https://github.com/TeriyakiGod/citsy-32blit).
- **MockHost** — Record snapshots; no window.

!!! warning "Keep platform code out of core"

    The `citsy` library must not link 32blit, SDL, OpenGL, or any audio/window library. Display hosts are separate projects.
