#pragma once

// Internal game data model.  Not part of the public citsy API.

#include <citsy/types.hpp>

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace citsy {

// ---------------------------------------------------------------------------
// Pixel art frame
// ---------------------------------------------------------------------------

/// One 8×8 tile frame stored as 64 bytes (row-major, [y*8+x]).
/// Each byte is 0 (background) or 1 (drawing colour).
using TileFrame = std::array<std::uint8_t, kTileSize * kTileSize>;

// ---------------------------------------------------------------------------
// Palette
// ---------------------------------------------------------------------------

struct Palette {
    std::string         id;
    std::string         name;
    std::vector<Color>  colors;  ///< index 0 = bg, 1 = tile, 2 = sprite, …
};

// ---------------------------------------------------------------------------
// Tile
// ---------------------------------------------------------------------------

struct Tile {
    std::string             id;
    std::string             name;
    std::vector<TileFrame>  frames;       ///< ≥1 frame; animated if >1
    bool                    is_wall       = false;
    std::uint8_t            color_index   = 1;  ///< palette colour index
};

// ---------------------------------------------------------------------------
// Sprite
// ---------------------------------------------------------------------------

struct SpritePos {
    std::string room_id;
    int         x = 0;
    int         y = 0;
};

struct Sprite {
    std::string             id;
    std::string             name;
    std::vector<TileFrame>  frames;
    std::optional<SpritePos> position;
    std::string             dialog_id;
    std::uint8_t            color_index = 2;
};

// ---------------------------------------------------------------------------
// Item
// ---------------------------------------------------------------------------

struct Item {
    std::string             id;
    std::string             name;
    std::vector<TileFrame>  frames;
    std::string             dialog_id;
    std::uint8_t            color_index = 2;
};

// ---------------------------------------------------------------------------
// Room contents
// ---------------------------------------------------------------------------

struct RoomItem {
    std::string item_id;
    int         x = 0;
    int         y = 0;
    std::string dialog_id;  ///< optional per-instance dialog override
};

struct Exit {
    int         x    = 0;
    int         y    = 0;
    std::string dest_room_id;
    int         dest_x = 0;
    int         dest_y = 0;
    std::string transition_effect;  ///< empty = no transition
    std::string dialog_id;          ///< optional
};

struct EndingRef {
    std::string ending_id;
    int         x = 0;
    int         y = 0;
};

// ---------------------------------------------------------------------------
// Room
// ---------------------------------------------------------------------------

/// 16×16 grid of tile IDs.  tiles[y][x] holds the tile ID string.
using TileGrid = std::array<std::array<std::string, kMapSize>, kMapSize>;

struct Room {
    std::string          id;
    std::string          name;
    TileGrid             tiles;      ///< tiles[row][col]; "0" = default bg
    std::string          palette_id;
    std::vector<RoomItem> items;
    std::vector<Exit>    exits;
    std::vector<EndingRef> endings;
};

// ---------------------------------------------------------------------------
// Dialogue
// ---------------------------------------------------------------------------

struct Dialogue {
    std::string id;
    std::string content;  ///< raw script text (Bitsy dialog syntax)
};

// ---------------------------------------------------------------------------
// Variable
// ---------------------------------------------------------------------------

struct Variable {
    std::string name;
    std::string value;  ///< initial value; number or string
};

// ---------------------------------------------------------------------------
// Ending
// ---------------------------------------------------------------------------

struct Ending {
    std::string id;
    std::string text;
    std::string name;
};

// ---------------------------------------------------------------------------
// Game version
// ---------------------------------------------------------------------------

struct GameVersion {
    int major = 0;
    int minor = 0;

    [[nodiscard]] std::string to_string() const {
        return std::to_string(major) + "." + std::to_string(minor);
    }
};

// ---------------------------------------------------------------------------
// Game  (root aggregate)
// ---------------------------------------------------------------------------

struct Game {
    GameVersion version;
    int         room_format = 1;   ///< 0 = legacy single-char, 1 = comma-sep
    std::string title;             ///< from NAME directive after BITSY VERSION

    // Avatar is always sprite id "A"
    static constexpr std::string_view kAvatarId = "A";

    std::unordered_map<std::string, Palette>   palettes;
    std::unordered_map<std::string, Tile>      tiles;
    std::unordered_map<std::string, Sprite>    sprites;
    std::unordered_map<std::string, Item>      items;
    std::unordered_map<std::string, Room>      rooms;
    std::unordered_map<std::string, Dialogue>  dialogues;
    std::unordered_map<std::string, Variable>  variables;
    std::unordered_map<std::string, Ending>    endings;

    // Convenience accessors ------------------------------------------------

    [[nodiscard]] const Sprite* avatar() const {
        auto it = sprites.find(std::string(kAvatarId));
        return it != sprites.end() ? &it->second : nullptr;
    }

    /// Room the avatar starts in (derived from avatar sprite POS).
    [[nodiscard]] std::string start_room_id() const {
        const Sprite* av = avatar();
        if (av && av->position) return av->position->room_id;
        // Fall back to first room in insertion order (deterministic via sorted key)
        if (!rooms.empty()) return rooms.begin()->second.id;
        return {};
    }
};

} // namespace citsy
