#pragma once

// Internal game data model.  Not part of the public citsy API.
// Matches Bitsy 8.15 world data (rooms, drawings, dialog, tunes, blips, fonts).

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
// Text
// ---------------------------------------------------------------------------

enum class TextDirection {
    LeftToRight,  ///< TEXT_DIRECTION LTR (default)
    RightToLeft,  ///< TEXT_DIRECTION RTL
};

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
    int                     bgc           = 0;  ///< background palette index
    bool                    bgc_transparent = false;
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
    int                     bgc         = 0;
    bool                    bgc_transparent = false;
    std::string             blip_id;
    std::unordered_map<std::string, int> inventory;  ///< starting item counts
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
    int                     bgc         = 0;
    bool                    bgc_transparent = false;
    std::string             blip_id;
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
    std::string transition_effect;  ///< empty / "none" = instant
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
    std::string          avatar_id;  ///< AVA override; empty = sprite A
    std::string          tune_id;    ///< TUNE id; empty / "0" = silence
    std::vector<std::string> wall_ids;  ///< legacy WAL list of tile ids
};

// ---------------------------------------------------------------------------
// Dialogue
// ---------------------------------------------------------------------------

struct Dialogue {
    std::string id;
    std::string content;  ///< raw script text (Bitsy dialog syntax)
    std::string name;
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
// Sound: blips (SFX) and tunes (music)
// ---------------------------------------------------------------------------

inline constexpr int kBarLength     = 16;
inline constexpr int kMaxTuneLength = 16;

enum class Tempo { Slow = 0, Medium = 1, Fast = 2, ExtraFast = 3 };
enum class Arpeggio { Off = 0, Up = 1, Down = 2, Int5 = 3, Int8 = 4 };

/// One note in a tune bar or blip pitch slot.
struct Pitch {
    int         beats  = 0;     ///< 0 = rest
    int         note   = 0;     ///< chromatic 0–11 or solfa 0–6
    int         octave = 2;     ///< Bitsy octave index (0=C2 … 3=C5); 2 = middle
    bool        solfa  = false;
    std::string blip_id;
};

struct TuneKey {
    std::array<int, 7> notes{};  ///< solfa degree → chromatic note; -1 = none
    std::vector<int>   scale;    ///< enabled solfa degrees
};

struct Tune {
    std::string id;
    std::string name;
    std::vector<std::array<Pitch, kBarLength>> melody;
    std::vector<std::array<Pitch, kBarLength>> harmony;
    std::optional<TuneKey> key;
    Tempo       tempo     = Tempo::Medium;
    PulseWave   instrument_a = PulseWave::Half;
    PulseWave   instrument_b = PulseWave::Half;
    Arpeggio    arpeggio  = Arpeggio::Off;
};

struct BlipEnvelope {
    int attack  = 0;
    int decay   = 0;
    int sustain = 0;  ///< 0–15 volume
    int length  = 0;
    int release = 0;
};

struct BlipBeat {
    int time  = 0;  ///< ms between pitch changes
    int delay = 0;  ///< ms before first pitch change
};

struct Blip {
    std::string   id;
    std::string   name;
    Pitch         pitch_a;
    Pitch         pitch_b;
    Pitch         pitch_c;
    BlipEnvelope  envelope;
    BlipBeat      beat;
    PulseWave     instrument = PulseWave::Half;
    bool          do_repeat  = false;
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
    std::string title;             ///< from NAME directive or first-line title
    std::string title_dialog;      ///< dialog id "title" source (may equal title)

    std::string     font_name = "ascii_small";
    std::string     font_data;     ///< raw .bitsyfont body, if the file embeds one
    TextDirection   text_direction = TextDirection::LeftToRight;
    int             txt_mode   = 0;  ///< 0 = HIREZ, 1 = LOREZ
    int             dlg_compat = 0;  ///< 1 = pre-7.0 dialog linking

    // Avatar is always sprite id "A"
    static constexpr std::string_view kAvatarId = "A";
    static constexpr std::string_view kTitleDialogId = "title";

    std::unordered_map<std::string, Palette>   palettes;
    std::unordered_map<std::string, Tile>      tiles;
    std::unordered_map<std::string, Sprite>    sprites;
    std::unordered_map<std::string, Item>      items;
    std::unordered_map<std::string, Room>      rooms;
    std::unordered_map<std::string, Dialogue>  dialogues;
    std::unordered_map<std::string, Variable>  variables;
    std::unordered_map<std::string, Ending>    endings;
    std::unordered_map<std::string, Tune>      tunes;
    std::unordered_map<std::string, Blip>      blips;

    // Convenience accessors ------------------------------------------------

    [[nodiscard]] const Sprite* avatar() const {
        auto it = sprites.find(std::string(kAvatarId));
        return it != sprites.end() ? &it->second : nullptr;
    }

    /// Room the avatar starts in (derived from avatar sprite POS).
    [[nodiscard]] std::string start_room_id() const {
        const Sprite* av = avatar();
        if (av && av->position) return av->position->room_id;
        if (!rooms.empty()) return rooms.begin()->second.id;
        return {};
    }

    [[nodiscard]] const Sprite* sprite(std::string_view id) const {
        auto it = sprites.find(std::string(id));
        return it != sprites.end() ? &it->second : nullptr;
    }

    /// Resolve a drawing / room / pal / tune / blip by id or NAME.
    template <typename T>
    [[nodiscard]] static const T* by_id_or_name(
        const std::unordered_map<std::string, T>& map, std::string_view key) {
        auto it = map.find(std::string(key));
        if (it != map.end()) return &it->second;
        for (const auto& [id, obj] : map) {
            if (obj.name == key) return &obj;
        }
        return nullptr;
    }

    [[nodiscard]] const Tile* find_tile(std::string_view key) const {
        return by_id_or_name(tiles, key);
    }
    [[nodiscard]] const Sprite* find_sprite(std::string_view key) const {
        return by_id_or_name(sprites, key);
    }
    [[nodiscard]] const Item* find_item(std::string_view key) const {
        return by_id_or_name(items, key);
    }
    [[nodiscard]] const Room* find_room(std::string_view key) const {
        return by_id_or_name(rooms, key);
    }
    [[nodiscard]] const Palette* find_palette(std::string_view key) const {
        return by_id_or_name(palettes, key);
    }
    [[nodiscard]] const Tune* find_tune(std::string_view key) const {
        return by_id_or_name(tunes, key);
    }
    [[nodiscard]] const Blip* find_blip(std::string_view key) const {
        return by_id_or_name(blips, key);
    }
};

} // namespace citsy
