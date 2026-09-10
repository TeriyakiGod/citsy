#include "src/render/compose.hpp"

#include <algorithm>

namespace citsy {
namespace {

void blit_opaque(std::array<std::uint8_t, kVideoSize * kVideoSize>& video,
                 int tile_x, int tile_y,
                 const TileFrame& frame, std::uint8_t color) {
    const int px0 = tile_x * kTileSize;
    const int py0 = tile_y * kTileSize;
    for (int row = 0; row < kTileSize; ++row) {
        for (int col = 0; col < kTileSize; ++col) {
            const std::uint8_t pix = frame[static_cast<std::size_t>(row * kTileSize + col)];
            video[static_cast<std::size_t>((py0 + row) * kVideoSize + (px0 + col))] =
                pix ? color : 0;
        }
    }
}

void blit_transparent(std::array<std::uint8_t, kVideoSize * kVideoSize>& video,
                      int tile_x, int tile_y,
                      const TileFrame& frame, std::uint8_t color) {
    const int px0 = tile_x * kTileSize;
    const int py0 = tile_y * kTileSize;
    for (int row = 0; row < kTileSize; ++row) {
        for (int col = 0; col < kTileSize; ++col) {
            const std::uint8_t pix = frame[static_cast<std::size_t>(row * kTileSize + col)];
            if (!pix) continue;
            video[static_cast<std::size_t>((py0 + row) * kVideoSize + (px0 + col))] = color;
        }
    }
}

[[nodiscard]] bool in_bounds(int x, int y) {
    return x >= 0 && y >= 0 && x < kMapSize && y < kMapSize;
}

} // namespace

std::uint8_t drawing_code(std::string_view id) {
    if (id.empty() || id == "0") return 0;
    if (id.size() == 1) return static_cast<std::uint8_t>(static_cast<unsigned char>(id[0]));
    std::uint8_t h = 0;
    for (unsigned char c : id) h = static_cast<std::uint8_t>(h * 33u + c);
    return h == 0 ? 1 : h;
}

std::uint8_t overlay_code(std::string_view id) {
    const std::uint8_t c = drawing_code(id);
    return c == 0 ? 1u : c;
}

void compose_room(const ComposeState& state, ComposeBuffers buffers) {
    buffers.map1.fill(0);
    buffers.map2.fill(0);
    buffers.video.fill(0);

    if (!state.game || !state.room) return;

    const Game& game = *state.game;
    const Room& room = *state.room;

    auto map_index = [](int x, int y) -> std::size_t {
        return static_cast<std::size_t>(y * kMapSize + x);
    };

    // --- background tiles (map1 + opaque blit) ---
    for (int y = 0; y < kMapSize; ++y) {
        for (int x = 0; x < kMapSize; ++x) {
            const std::string& tid = room.tiles[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)];
            buffers.map1[map_index(x, y)] = drawing_code(tid);
            if (tid.empty() || tid == "0") continue;
            auto it = game.tiles.find(tid);
            if (it == game.tiles.end() || it->second.frames.empty()) continue;
            blit_opaque(buffers.video, x, y, it->second.frames.front(), it->second.color_index);
        }
    }

    // --- items (map2 + transparent blit) ---
    if (state.items) {
        for (const RoomItem& ri : *state.items) {
            if (!in_bounds(ri.x, ri.y)) continue;
            buffers.map2[map_index(ri.x, ri.y)] = overlay_code(ri.item_id);
            auto it = game.items.find(ri.item_id);
            if (it == game.items.end() || it->second.frames.empty()) continue;
            blit_transparent(buffers.video, ri.x, ri.y, it->second.frames.front(),
                             it->second.color_index);
        }
    }

    // --- non-avatar sprites, stable order by id ---
    std::vector<const Sprite*> sprites;
    sprites.reserve(game.sprites.size());
    for (const auto& [id, spr] : game.sprites) {
        if (id == Game::kAvatarId) continue;
        if (!spr.position) continue;
        if (spr.position->room_id != state.room_id) continue;
        sprites.push_back(&spr);
    }
    std::sort(sprites.begin(), sprites.end(), [](const Sprite* a, const Sprite* b) {
        return a->id < b->id;
    });

    for (const Sprite* spr : sprites) {
        const int x = spr->position->x;
        const int y = spr->position->y;
        if (!in_bounds(x, y)) continue;
        buffers.map2[map_index(x, y)] = overlay_code(spr->id);
        if (spr->frames.empty()) continue;
        blit_transparent(buffers.video, x, y, spr->frames.front(), spr->color_index);
    }

    // --- avatar on top ---
    if (in_bounds(state.avatar_x, state.avatar_y)) {
        buffers.map2[map_index(state.avatar_x, state.avatar_y)] =
            overlay_code(Game::kAvatarId);
        const Sprite* avatar = game.avatar();
        if (avatar && !avatar->frames.empty()) {
            blit_transparent(buffers.video, state.avatar_x, state.avatar_y,
                             avatar->frames.front(), avatar->color_index);
        }
    }
}

} // namespace citsy
