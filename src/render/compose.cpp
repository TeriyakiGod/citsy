#include "src/render/compose.hpp"

#include <algorithm>
#include <array>

namespace citsy {
namespace {

const TileFrame* frame_of(const std::vector<TileFrame>& frames, int anim) {
    if (frames.empty()) return nullptr;
    const auto n = static_cast<int>(frames.size());
    return &frames[static_cast<std::size_t>(((anim % n) + n) % n)];
}

// Opaque tile: every pixel is fg or bg. The common Bitsy floor/wall path.
void blit_tile_opaque(std::uint8_t* dest, const TileFrame& frame,
                      std::uint8_t fg, std::uint8_t bg) {
    const std::uint8_t* src = frame.data();
    for (int row = 0; row < kTileSize; ++row) {
        std::uint8_t* d = dest + row * kVideoSize;
        const std::uint8_t* s = src + row * kTileSize;
        for (int col = 0; col < kTileSize; ++col) {
            d[col] = s[col] ? fg : bg;
        }
    }
}

// Overlay sprite/item: ink writes fg; transparent pixels keep the dest.
void blit_tile_overlay_transparent(std::uint8_t* dest, const TileFrame& frame,
                                   std::uint8_t fg) {
    const std::uint8_t* src = frame.data();
    for (int row = 0; row < kTileSize; ++row) {
        std::uint8_t* d = dest + row * kVideoSize;
        const std::uint8_t* s = src + row * kTileSize;
        for (int col = 0; col < kTileSize; ++col) {
            if (s[col]) d[col] = fg;
        }
    }
}

void blit_tile(std::array<std::uint8_t, kVideoSize * kVideoSize>& video,
               int tile_x, int tile_y,
               const TileFrame& frame, std::uint8_t fg,
               int bgc, bool transparent, bool overlay) {
    const int px0 = tile_x * kTileSize;
    const int py0 = tile_y * kTileSize;
    std::uint8_t* dest = &video[static_cast<std::size_t>(py0 * kVideoSize + px0)];
    const std::uint8_t bg = static_cast<std::uint8_t>(std::max(0, bgc));

    if (!overlay && !transparent) {
        blit_tile_opaque(dest, frame, fg, bg);
        return;
    }
    if (overlay && (transparent || bgc <= 0)) {
        blit_tile_overlay_transparent(dest, frame, fg);
        return;
    }

    const std::uint8_t* src = frame.data();
    for (int row = 0; row < kTileSize; ++row) {
        std::uint8_t* d = dest + row * kVideoSize;
        const std::uint8_t* s = src + row * kTileSize;
        for (int col = 0; col < kTileSize; ++col) {
            if (s[col]) {
                d[col] = fg;
            } else if (!overlay) {
                if (!transparent) d[col] = bg;
            } else if (!transparent && bgc > 0) {
                d[col] = bg;
            }
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
    const int anim = state.anim_frame;

    auto map_index = [](int x, int y) -> std::size_t {
        return static_cast<std::size_t>(y * kMapSize + x);
    };

    // Bitsy skips tiles under sprites and items so entity transparency shows
    // the room background, not the tile. Draw order is unchanged: tiles,
    // then items, then sprites, then the avatar.
    std::array<bool, kMapSize * kMapSize> entity_here{};
    std::array<const Sprite*, kMapSize * kMapSize> sprites{};
    int nsprites = 0;
    if constexpr (kOccludeTilesUnderEntities) {
        auto mark = [&](int sx, int sy) {
            if (in_bounds(sx, sy)) entity_here[map_index(sx, sy)] = true;
        };
        mark(state.avatar_x, state.avatar_y);
        for (const auto& [id, spr] : game.sprites) {
            if (id == Game::kAvatarId) continue;
            if (!spr.position) continue;
            if (spr.position->room_id != state.room_id) continue;
            mark(spr.position->x, spr.position->y);
            if (nsprites < static_cast<int>(sprites.size())) {
                sprites[static_cast<std::size_t>(nsprites++)] = &spr;
            }
        }
        if (state.items) {
            for (const RoomItem& ri : *state.items) {
                mark(ri.x, ri.y);
            }
        }
    } else {
        for (const auto& [id, spr] : game.sprites) {
            if (id == Game::kAvatarId) continue;
            if (!spr.position) continue;
            if (spr.position->room_id != state.room_id) continue;
            if (nsprites < static_cast<int>(sprites.size())) {
                sprites[static_cast<std::size_t>(nsprites++)] = &spr;
            }
        }
    }

    for (int y = 0; y < kMapSize; ++y) {
        for (int x = 0; x < kMapSize; ++x) {
            if constexpr (kOccludeTilesUnderEntities) {
                if (entity_here[map_index(x, y)]) continue;
            }
            const std::string& tid = room.tiles[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)];
            buffers.map1[map_index(x, y)] = drawing_code(tid);
            if (tid.empty() || tid == "0") continue;
            auto it = game.tiles.find(tid);
            if (it == game.tiles.end()) continue;
            const TileFrame* fr = frame_of(it->second.frames, anim);
            if (!fr) continue;
            blit_tile(buffers.video, x, y, *fr, it->second.color_index,
                      it->second.bgc, it->second.bgc_transparent, false);
        }
    }

    if (state.items) {
        for (const RoomItem& ri : *state.items) {
            if (!in_bounds(ri.x, ri.y)) continue;
            buffers.map2[map_index(ri.x, ri.y)] = overlay_code(ri.item_id);
            auto it = game.items.find(ri.item_id);
            if (it == game.items.end()) continue;
            const TileFrame* fr = frame_of(it->second.frames, anim);
            if (!fr) continue;
            blit_tile(buffers.video, ri.x, ri.y, *fr, it->second.color_index,
                      it->second.bgc, it->second.bgc_transparent, true);
        }
    }

    std::sort(sprites.begin(), sprites.begin() + nsprites,
              [](const Sprite* a, const Sprite* b) {
                  return a->id < b->id;
              });

    for (int i = 0; i < nsprites; ++i) {
        const Sprite* spr = sprites[static_cast<std::size_t>(i)];
        const int x = spr->position->x;
        const int y = spr->position->y;
        if (!in_bounds(x, y)) continue;
        buffers.map2[map_index(x, y)] = overlay_code(spr->id);
        const TileFrame* fr = frame_of(spr->frames, anim);
        if (!fr) continue;
        blit_tile(buffers.video, x, y, *fr, spr->color_index,
                  spr->bgc, spr->bgc_transparent, true);
    }

    if (in_bounds(state.avatar_x, state.avatar_y)) {
        buffers.map2[map_index(state.avatar_x, state.avatar_y)] =
            overlay_code(Game::kAvatarId);
        const Sprite* appearance = game.sprite(state.avatar_id);
        if (!appearance) appearance = game.avatar();
        if (appearance) {
            const TileFrame* fr = frame_of(appearance->frames, anim);
            if (fr) {
                blit_tile(buffers.video, state.avatar_x, state.avatar_y, *fr,
                          appearance->color_index, appearance->bgc,
                          appearance->bgc_transparent, true);
            }
        }
    }
}

} // namespace citsy
