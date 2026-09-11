# Bitsy data model

A Bitsy game is a plain-text `.bitsy` file: a sequence of typed **segments**, each defining one entity (palette, tile, room, …). citsy parses this file into an in-memory `Game` structure (`src/model/game.hpp`).

This document describes every entity type, its properties, how segments map to file syntax, and how entities relate to each other.

---

## Overview

```
Game
├── version, room_format, title
├── palettes[id]   → Palette
├── tiles[id]      → Tile
├── sprites[id]    → Sprite      (avatar is always id "A")
├── items[id]      → Item
├── rooms[id]      → Room
├── dialogues[id]  → Dialogue
├── variables[name]→ Variable
└── endings[id]    → Ending
```

All entity collections are keyed by string id (or name for variables). There is no implicit ordering — rooms, tiles, and other segments can appear in any sequence in the file.

---

## Coordinate system

| Concept | Value |
|---|---|
| Room grid | 16 × 16 tiles |
| Tile pixel size | 8 × 8 pixels |
| Logical screen | 128 × 128 pixels (16 tiles × 8 px) |
| Origin | Top-left corner |
| Tile coordinates | `x` increases right, `y` increases down |
| Range | `x` and `y` are 0–15 within a room |

Positions in file data use `x,y` notation (e.g. `4,7` means column 4, row 7).

The avatar sprite, room-placed items, exits, and endings all use tile coordinates within a room grid.

---

## Pixel art: `TileFrame`

All drawable entities (tiles, sprites, items) store pixel data as one or more **frames**.

| Property | Type | Description |
|---|---|---|
| Storage | `std::array<std::uint8_t, 64>` | Row-major: index = `y * 8 + x` |
| Pixel values | `0` or `1` | `0` = background color, `1` = drawing color |
| Frame size | 8 rows × 8 columns | Each row is exactly 8 characters in the file |

### File format

Each frame is eight lines of eight binary digits:

```
11111111
10000001
10000001
10000001
10000001
10000001
10000001
11111111
```

Multiple frames are separated by a `>` line on its own:

```
11111111
...
11111111
>
00000000
...
00000000
```

Animated tiles, sprites, and items cycle through frames every 400 ms (Bitsy 8.15).

### Color mapping

Each pixel is binary, but the **drawing color** is not always palette index 1 or 2. Entities can specify a `COL n` directive to use extended palette color index `n`. Background pixels always use palette index 0.

---

## `Game` — root aggregate

The top-level container for all parsed game data.

| Property | C++ type | Default | Description |
|---|---|---|---|
| `version` | `GameVersion` | `0.0` | Parsed from `# BITSY VERSION major.minor` header |
| `room_format` | `int` | `1` | `0` = legacy single-char rows; `1` = comma-separated |
| `title` | `string` | `""` | First line of the file (Bitsy title dialog) or top-level `NAME` |
| `title_dialog` | `string` | `""` | Full title script shown at game start |
| `palettes` | `map<string, Palette>` | — | All color palettes, keyed by id |
| `tiles` | `map<string, Tile>` | — | Tile definitions |
| `sprites` | `map<string, Sprite>` | — | Sprite definitions |
| `items` | `map<string, Item>` | — | Item definitions |
| `rooms` | `map<string, Room>` | — | Room definitions |
| `dialogues` | `map<string, Dialogue>` | — | Dialog scripts |
| `variables` | `map<string, Variable>` | — | Global game variables |
| `endings` | `map<string, Ending>` | — | End-game messages |

### Constants and helpers

| Name | Value / signature | Description |
|---|---|---|
| `Game::kAvatarId` | `"A"` | Avatar sprite id is always `A` |
| `avatar()` | `const Sprite*` | Returns the avatar sprite, or `nullptr` |
| `start_room_id()` | `string` | Room from avatar `POS`, or first room if unset |

### File header directives

```
my game title

# BITSY VERSION 8.15

! ROOM_FORMAT 1
```

| Directive | Maps to |
|---|---|
| First line (before `#` / `!` / segments) | `title`, `title_dialog`, `dialogues["title"]` |
| `# BITSY VERSION X.Y` | `version.major`, `version.minor` |
| `NAME <title>` | `title` (top-level fallback) |
| `! ROOM_FORMAT 1` | `room_format = 1` (comma-separated room rows) |
| `! ROOM_FORMAT 0` | `room_format = 0` (legacy single-char tile ids) |
| `! TXT_MODE 0/1` | Hirez / lorez textbox |
| `! DLG_COMPAT 1` | Pre-7.0 sprite↔dialog id linking (auto for `VER_MAJ < 7`) |
| `TEXT_DIRECTION RTL` | Right-to-left textbox layout |

---

## `GameVersion`

| Property | Type | Description |
|---|---|---|
| `major` | `int` | Major version number |
| `minor` | `int` | Minor version number |

Method `to_string()` returns `"major.minor"`.

---

## `Palette`

A named set of RGB colors used by rooms and drawing entities.

| Property | C++ type | Description |
|---|---|---|
| `id` | `string` | Palette identifier (e.g. `"0"`, `"1"`) |
| `name` | `string` | Human-readable name |
| `colors` | `vector<Color>` | Ordered RGB values |

### Color indices

| Index | Role | Typical use |
|---|---|---|
| `0` | Background | Room fill, pixel value `0` in art |
| `1` | Tile | Default drawing color for tiles (`COL` default) |
| `2` | Sprite | Default drawing color for sprites and items |
| `3+` | Extended | Multi-color art via `COL n` directives |

Each `Color` is `{ r, g, b }` with components 0–255.

### File format

```
PAL 0
NAME forest
0,82,204
128,159,255
255,255,255
```

Optional `COL n` lines before a color row are accepted and ignored (some editor versions emit them). Additional RGB lines extend the palette beyond three colors.

### Usage

Rooms reference a palette via `PAL <id>`. When the avatar enters a room, the active palette switches to that room's palette for rendering.

---

## `Tile`

Static 8×8 pixel art placed on the room grid. Tiles form the background layer — walls, floors, decorations.

| Property | C++ type | Default | Description |
|---|---|---|---|
| `id` | `string` | — | Tile identifier (single char or short string, e.g. `"a"`, `"0"`) |
| `name` | `string` | `""` | Display name |
| `frames` | `vector<TileFrame>` | — | ≥1 frame; multiple frames = animation |
| `is_wall` | `bool` | `false` | If true, blocks avatar movement |
| `color_index` | `uint8_t` | `1` | Palette index for drawing pixels (`1` = tile color) |

### File format

```
TIL a
11111111
10000001
10000001
10000001
10000001
10000001
10000001
11111111
NAME wall
WAL true
COL 2
```

| Sub-key | Maps to |
|---|---|
| `NAME <text>` | `name` |
| `WAL true` / `WAL false` | `is_wall` |
| `COL n` | `color_index` |
| 8×8 binary rows | One animation frame |
| `>` | Frame separator |

### Room placement

Rooms reference tiles by id in their 16×16 grid. The default background tile is typically id `"0"`.

### Wall collision

When `is_wall` is true, the avatar cannot move onto that tile. Legacy games may also list wall tile ids in a room-level `WAL` directive; modern games set `WAL true` on the tile definition itself.

---

## `Sprite`

An animated 8×8 character or object that exists independently of the room grid. The **avatar** (player character) is always sprite id `A`.

| Property | C++ type | Default | Description |
|---|---|---|---|
| `id` | `string` | — | Sprite identifier |
| `name` | `string` | `""` | Display name |
| `frames` | `vector<TileFrame>` | — | Animation frames |
| `position` | `optional<SpritePos>` | `nullopt` | Starting room and tile coordinates |
| `dialog_id` | `string` | `""` | Dialog script id (empty = no dialog) |
| `color_index` | `uint8_t` | `2` | Palette index for drawing pixels |

### `SpritePos`

| Property | Type | Description |
|---|---|---|
| `room_id` | `string` | Room the sprite starts in |
| `x` | `int` | Tile column (0–15) |
| `y` | `int` | Tile row (0–15) |

### File format

```
SPR A
00011000
00011000
00111100
01111110
10111101
00111100
00011000
00011000
NAME Avatar
POS 0 4,7
DLG DLG_0
COL 2
```

| Sub-key | Maps to |
|---|---|
| `NAME <text>` | `name` |
| `POS <room_id> <x>,<y>` | `position` |
| `DLG <dialog_id>` | `dialog_id` |
| `COL n` | `color_index` |

### Avatar rules

- Id is always `A` (`Game::kAvatarId`)
- Exactly one avatar per game
- `start_room_id()` derives from the avatar's `POS` directive
- The avatar moves tile-by-tile in response to directional input

Non-avatar sprites (id `0`, `1`, …) are NPCs or decorative characters placed at fixed positions. They can have dialog attached.

---

## `Item`

A collectible 8×8 object. Items are defined globally and **placed** in specific rooms.

| Property | C++ type | Default | Description |
|---|---|---|---|
| `id` | `string` | — | Item identifier |
| `name` | `string` | `""` | Display name (shown in inventory) |
| `frames` | `vector<TileFrame>` | — | Animation frames |
| `dialog_id` | `string` | `""` | Default dialog when interacted with |
| `color_index` | `uint8_t` | `2` | Palette index for drawing pixels |

### File format (definition)

```
ITM 0
00000000
00011000
00111100
01111110
01111110
00111100
00011000
00000000
NAME key
DLG DLG_0
COL 2
```

### Room placement: `RoomItem`

Items appear in rooms via `ITM` sub-keys on the room segment:

| Property | Type | Description |
|---|---|---|
| `item_id` | `string` | References an `Item` definition |
| `x` | `int` | Tile column in the room |
| `y` | `int` | Tile row in the room |
| `dialog_id` | `string` | Optional per-instance dialog override |

```
ITM 0 4,4
ITM 0 5,3 DLG DLG_special
```

### Runtime behavior (Phase 1)

- Avatar walks onto an item to pick it up and show its dialog
- The item is removed from the room so it is no longer drawn
- Inventory counts and `{item}` give/take scripts are Phase 2

---

## `Room`

A 16×16 grid of tiles plus placed items, exits, and endings.

| Property | C++ type | Default | Description |
|---|---|---|---|
| `id` | `string` | — | Room identifier |
| `name` | `string` | `""` | Display name |
| `tiles` | `TileGrid` | — | 16×16 grid of tile id strings |
| `palette_id` | `string` | `""` | Active palette for this room |
| `items` | `vector<RoomItem>` | — | Placed collectible items |
| `exits` | `vector<Exit>` | — | Warp points to other rooms |
| `endings` | `vector<EndingRef>` | — | Tiles that trigger game endings |
| `avatar_id` | `string` | `""` | Per-room avatar appearance (`AVA`) |
| `tune_id` | `string` | `""` | Room music (`TUNE`); `"0"` / empty = silence |

### `TileGrid`

```cpp
using TileGrid = std::array<std::array<std::string, 16>, 16>;
// tiles[row][col] — tile id at each cell
```

Each cell holds a tile id string (e.g. `"0"`, `"a"`). The string `"0"` typically refers to the default background tile.

### File format

```
ROOM 0
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
0,a,a,a,a,a,a,a,a,a,a,a,a,a,a,0
...
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
NAME room one
PAL 0
ITM 0 4,4
EXT 15,8 1 0,8
END 0 8,0
```

The room body has two parts:

1. **Tile rows** — exactly 16 lines at the top of the segment
2. **Sub-keys** — metadata and placed entities after the grid

| Sub-key | Maps to |
|---|---|
| `NAME <text>` | `name` |
| `PAL <id>` | `palette_id` |
| `ITM <item_id> <x>,<y>` | Entry in `items` |
| `EXT <x>,<y> <dest_room> <dx>,<dy> …` | Entry in `exits` |
| `END <ending_id> <x>,<y>` | Entry in `endings` |
| `AVA <sprite_id>` | Per-room avatar appearance |
| `TUNE <tune_id>` | Room music |
| `WAL <id>,…` | Legacy wall list |

### Room format modes

| `room_format` | Tile row syntax | Example row |
|---|---|---|
| `1` (modern) | Comma-separated ids | `0,a,a,0,0,0,0,0,0,0,0,0,0,a,0,0` |
| `0` (legacy) | 16 consecutive id chars | `0aaa000000000aa0` |

Both formats produce the same `TileGrid` in memory.

---

## `Exit`

A warp tile that transports the avatar to another room when stepped on.

| Property | C++ type | Default | Description |
|---|---|---|---|
| `x` | `int` | `0` | Source tile column in this room |
| `y` | `int` | `0` | Source tile row in this room |
| `dest_room_id` | `string` | — | Target room id |
| `dest_x` | `int` | `0` | Avatar tile column in destination room |
| `dest_y` | `int` | `0` | Avatar tile row in destination room |
| `transition_effect` | `string` | `""` | Visual transition name (empty = instant) |
| `dialog_id` | `string` | `""` | Optional dialog before transition |

### File format

```
EXT 15,8 1 0,8
EXT 0,8 0 15,8 TRANSITION fade DLG DLG_0
```

Syntax: `EXT <src_x>,<src_y> <dest_room> <dest_x>,<dest_y> [TRANSITION <fx>] [DLG <id>]`

Common transition effects include `fade_w`, `fade_b`, `wave`, `tunnel`, and `slide_u` / `slide_d` / `slide_l` / `slide_r` (rendered in video mode). `none` or a missing `FX` warps instantly.

When the avatar's tile position matches `(x, y)`, the engine moves them to `(dest_x, dest_y)` in `dest_room_id`.

---

## `EndingRef`

Links a tile in a room to an ending definition.

| Property | C++ type | Description |
|---|---|---|
| `ending_id` | `string` | References an `Ending` definition |
| `x` | `int` | Tile column |
| `y` | `int` | Tile row |

### File format

```
END 0 8,0
```

When the avatar steps on tile `(x, y)`, the game triggers ending `ending_id` and displays its text.

> **Note:** `END` is overloaded in the file format. As a **room sub-key**, it defines a tile trigger (`EndingRef`). As a **top-level segment**, it defines the ending message (`Ending`). The parser distinguishes them by context.

---

## `Dialogue`

A script attached to sprites, items, or exits. Dialog is Bitsy's lightweight game logic language — not just text display.

| Property | C++ type | Description |
|---|---|---|
| `id` | `string` | Dialog identifier (e.g. `DLG_0`) |
| `content` | `string` | Raw script text, preserved verbatim |

### File format

```
DLG DLG_0
"Hello, world!"
"Press OK to continue."
```

Multiline content is stored with newline separators. The script may include:

| Feature | Example syntax | Status in citsy |
|---|---|---|
| Text lines | `"Hello!"` | Yes |
| Variable interpolation | `{print name}` / `{name}` | Yes |
| Assignment | `{score = 5}` | Yes |
| Conditionals | `{score}?` … `{else}` `{/}` | Yes (list form) |
| Sequence / cycle / shuffle lists | `{sequence` / `{cycle` / `{shuffle` | Yes |
| Item actions | `{item id}` / `{item id n}` | Yes |
| Exit / end / lock | `{exit}` `{end}` `{lock}` | Yes |
| Avatar / palette / tune / blip | `{ava}` `{pal}` `{tune}` `{blip}` | Yes |
| Drawings in dialog | `{printSprite}` `{printTile}` `{printItem}` | Yes |
| Text effects | `{wvy}` `{shk}` `{rbw}` `{clr}` | Yes |

citsy stores dialog content as raw text. `DialogVM` evaluates the script at runtime.

---

## `Variable`

Global game state — numbers or strings that dialog scripts read and write.

| Property | C++ type | Description |
|---|---|---|
| `name` | `string` | Variable name (also the map key) |
| `value` | `string` | Initial value as text |

### File format

```
VAR score
0

VAR name
ada
```

Values are stored as strings. The runtime distinguishes numeric and string variables by whether the value parses as a number. Dialog scripts use variables for branching, counters, and state tracking.

---

## `Ending`

The message displayed when the player reaches a game ending.

| Property | C++ type | Description |
|---|---|---|
| `id` | `string` | Ending identifier |
| `text` | `string` | End-game message (may be multiline) |
| `name` | `string` | Display name (editor label) |

### File format

```
END 0
You found the treasure!
Congratulations!
NAME good ending
```

Text lines come first; an optional `NAME` sub-key at the end sets the display name.

---

## Entity relationships

```
                    ┌─────────────┐
                    │    Game     │
                    └──────┬──────┘
           ┌───────────────┼───────────────┐
           ▼               ▼               ▼
      ┌─────────┐    ┌──────────┐    ┌───────────┐
      │ Palette │    │   Tile   │    │  Sprite   │
      └────┬────┘    └────┬─────┘    └─────┬─────┘
           │              │                 │
           │         tile ids in grid        │ dialog_id
           │              │                 ▼
           │              ▼           ┌───────────┐
           └────────►┌─────────┐      │ Dialogue  │
                     │  Room   │◄─────┤           │
                     └────┬────┘      └───────────┘
                          │
              ┌───────────┼───────────┐
              ▼           ▼           ▼
         ┌─────────┐ ┌─────────┐ ┌───────────┐
         │  Item   │ │  Exit   │ │ EndingRef │
         │(placed) │ │         │ │           │
         └────┬────┘ └────┬────┘ └─────┬─────┘
              │           │            │
              │      dest_room_id      │ ending_id
              │           │            ▼
              │           ▼      ┌─────────┐
              │      (Room)      │ Ending  │
              │                  └─────────┘
              ▼
         (Item def)
```

### Reference summary

| From | Field | References |
|---|---|---|
| `Room` | `palette_id` | `Palette.id` |
| `Room.tiles[y][x]` | tile id string | `Tile.id` |
| `RoomItem` | `item_id` | `Item.id` |
| `RoomItem` | `dialog_id` | `Dialogue.id` (optional override) |
| `Exit` | `dest_room_id` | `Room.id` |
| `Exit` | `dialog_id` | `Dialogue.id` |
| `EndingRef` | `ending_id` | `Ending.id` |
| `Sprite` | `dialog_id` | `Dialogue.id` |
| `Sprite.position` | `room_id` | `Room.id` |
| `Item` | `dialog_id` | `Dialogue.id` |

References are by string id. The parser does not validate that referenced ids exist — that is left to higher-level checks or runtime errors.

---

## Segment reference

Quick lookup of top-level `.bitsy` segment keywords:

| Keyword | Entity | Id format | Example |
|---|---|---|---|
| `PAL` | Palette | string | `PAL 0` |
| `TIL` | Tile | string | `TIL a` |
| `SPR` | Sprite | string | `SPR A` |
| `ITM` | Item | string | `ITM 0` |
| `ROOM` | Room | string | `ROOM 0` |
| `DLG` | Dialogue | string | `DLG DLG_0` |
| `VAR` | Variable | name | `VAR score` |
| `END` | Ending | string | `END 0` |
| `FONT` | Custom font | — | Parsed (`.bitsyfont`) |
| `DEFAULT_FONT` | Font name | — | `ascii_small` if omitted |
| `TEXT_DIRECTION` | `LTR` / `RTL` | — | Right-to-left dialog layout |
| `TUNE` | Music | string | Melody + harmony bars |
| `EXT` | Exit (legacy top-level) | — | Skipped by citsy |

Room-level sub-keys (`ITM`, `EXT`, `END`, `PAL`, `NAME`, `WAL`) appear inside a `ROOM` segment body, not as top-level segments.

---

## C++ struct quick reference

All types live in `src/model/game.hpp` (internal API).

```cpp
struct Game {
    GameVersion version;
    int room_format;
    std::string title;
    std::unordered_map<std::string, Palette>   palettes;
    std::unordered_map<std::string, Tile>      tiles;
    std::unordered_map<std::string, Sprite>    sprites;
    std::unordered_map<std::string, Item>      items;
    std::unordered_map<std::string, Room>      rooms;
    std::unordered_map<std::string, Dialogue>  dialogues;
    std::unordered_map<std::string, Variable>  variables;
    std::unordered_map<std::string, Tune>      tunes;
    std::unordered_map<std::string, Blip>      blips;
};

struct Palette       { id, name, colors };
struct Tile          { id, name, frames, is_wall, color_index };
struct Sprite        { id, name, frames, position, dialog_id, color_index };
struct SpritePos     { room_id, x, y };
struct Item          { id, name, frames, dialog_id, color_index };
struct Room          { id, name, tiles, palette_id, items, exits, endings };
struct RoomItem      { item_id, x, y, dialog_id };
struct Exit          { x, y, dest_room_id, dest_x, dest_y, transition_effect, dialog_id };
struct EndingRef     { ending_id, x, y };
struct Dialogue      { id, content };
struct Variable      { name, value };
struct Ending        { id, text, name };
struct GameVersion   { major, minor };
using TileFrame      = std::array<std::uint8_t, 64>;
using TileGrid       = std::array<std::array<std::string, 16>, 16>;
```

Parse a file into this structure:

```cpp
#include "src/parser/parser.hpp"

auto game = citsy::parse(bitsy_text);  // throws ParseError on failure
```

Or via the public API:

```cpp
#include <citsy/engine.hpp>

citsy::Engine engine(bitsy_text);  // parses internally
```

---

## Example: minimal game

See `tests/data/minimal.bitsy` for a complete game using every entity type. After parsing:

| Entity | Id | Key properties |
|---|---|---|
| Palette | `0` | 3 colors, name `"palette"` |
| Tile | `a` | Wall tile with border pixel art |
| Room | `0` | 16×16 grid, item at (4,4), ending at (8,0) |
| Sprite | `A` | Avatar at room `0`, position (8,8) |
| Item | `0` | Key with dialog `DLG_0` |
| Dialogue | `DLG_0` | `"Hello, world!"` |
| Variable | `score` | Initial value `"0"` |
| Ending | `0` | `"You found the treasure!"` |

---

## Compatibility notes

| Feature | Parsed by citsy | Simulated |
|---|---|---|
| Version header | Yes | — |
| Comma-separated rooms | Yes | Phase 1 |
| Legacy single-char rooms | Yes | Phase 1 |
| Multi-frame animation | Yes (stored) | Yes (400 ms) |
| Extended palettes (`COL n`) | Yes | Yes |
| Wall tiles (`WAL true`) | Yes | Yes |
| Variables | Yes | Yes |
| Dialog scripts | Yes | Yes (Bitsy 8.15 tags) |
| Exit transitions | Yes | Instant or FX in video mode |
| Custom fonts (`FONT`) | Yes | Yes |
| `TEXT_DIRECTION RTL` | Yes | Yes |
| Blips / tunes | Yes | Channel params each frame |
| Room avatar (`AVA`) | Yes | Yes |

For the full compatibility matrix, see the [README compatibility table](../../README.md#file-format-compatibility).
