#pragma once

// Logical room compositor: fills map1 / map2 / video from the current room.
// No GPU — colour indices and tile-id bytes only.

#include "src/model/game.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace citsy {

/// Encode a drawing id as a map-buffer byte.
/// `"0"` and empty → 0; a single character uses its code point; otherwise a hash.
[[nodiscard]] std::uint8_t drawing_code(std::string_view id);

/// Overlay cells must be non-zero when occupied (item id `"0"` would otherwise clash).
[[nodiscard]] std::uint8_t overlay_code(std::string_view id);

struct ComposeState {
    const Game*                     game = nullptr;
    const Room*                     room = nullptr;
    const std::vector<RoomItem>*    items = nullptr;  ///< runtime item list
    std::string                     room_id;
    int                             avatar_x = 0;
    int                             avatar_y = 0;
    int                             anim_frame = 0;
    std::string                     avatar_id = "A";
};

struct ComposeBuffers {
    std::array<std::uint8_t, kMapSize * kMapSize>&         map1;
    std::array<std::uint8_t, kMapSize * kMapSize>&         map2;
    std::array<std::uint8_t, kVideoSize * kVideoSize>&     video;
};

/// Fill map1 (tiles), map2 (items / sprites / avatar), and the 128×128 video buffer.
/// When kOccludeTilesUnderSprites is true, map1 / video skip tiles under sprites.
void compose_room(const ComposeState& state, ComposeBuffers buffers);

} // namespace citsy
