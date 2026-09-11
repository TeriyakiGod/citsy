#include "src/render/compose.hpp"

#include <algorithm>

namespace citsy {
namespace {

const TileFrame* frame_of(const std::vector<TileFrame>& frames, int anim) {
    if (frames.empty()) return nullptr;
    const auto n = static_cast<int>(frames.size());
    return &frames[static_cast<std::size_t>(((anim % n) + n) % n)];
}

void blit_tile(std::array<std::uint8_t, kVideoSize * kVideoSize>& video,
               int tile_x, int tile_y,
               const TileFrame& frame, std::uint8_t fg,
               int bgc, bool transparent, bool overlay) {
    const int px0 = tile_x * kTileSize;
    const int py0 = tile_y * kTileSize;
    for (int row = 0; row < kTileSize; ++row) {
        for (int col = 0; col < kTileSize; ++col) {
            const std::uint8_t pix = frame[static_cast<std::size_t>(row * kTileSize + col)];
            auto& dest = video[static_cast<std::size_t>((py0 + row) * kVideoSize + (px0 + col))];
            if (pix) {
                dest = fg;
            } else if (!overlay) {
                dest = transparent ? dest : static_cast<std::uint8_t>(std::max(0, bgc));
            } else if (!transparent && bgc > 0) {
                dest = static_cast<std::uint8_t>(bgc);
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

    for (int y = 0; y < kMapSize; ++y) {
        for (int x = 0; x < kMapSize; ++x) {
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
