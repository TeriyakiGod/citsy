#include <citsy/engine.hpp>

#include "src/dialog/linear.hpp"
#include "src/model/game.hpp"
#include "src/parser/parser.hpp"
#include "src/render/compose.hpp"

#include <algorithm>
#include <array>
#include <functional>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace citsy {
namespace {

constexpr double kFirstHoldMs  = 500.0;
constexpr double kRepeatHoldMs = 150.0;

constexpr int kTextboxWidth  = 104;
constexpr int kTextboxHeight = 32;

enum class Dir { None = -1, Up = 0, Down = 1, Left = 2, Right = 3 };

[[nodiscard]] Dir read_direction(const Host& host) {
    if (host.button(Button::Up))    return Dir::Up;
    if (host.button(Button::Down))  return Dir::Down;
    if (host.button(Button::Left))  return Dir::Left;
    if (host.button(Button::Right)) return Dir::Right;
    return Dir::None;
}

[[nodiscard]] bool any_action_down(const Host& host) {
    return host.button(Button::Up) || host.button(Button::Down) ||
           host.button(Button::Left) || host.button(Button::Right) ||
           host.button(Button::Ok);
}

[[nodiscard]] int dir_dx(Dir d) {
    if (d == Dir::Left)  return -1;
    if (d == Dir::Right) return  1;
    return 0;
}

[[nodiscard]] int dir_dy(Dir d) {
    if (d == Dir::Up)    return -1;
    if (d == Dir::Down)  return  1;
    return 0;
}

} // namespace

// ===========================================================================
// Engine::Impl
// ===========================================================================

struct Engine::Impl {
    Game game;

    bool running = false;

    std::string current_room_id;
    int         avatar_x = 0;
    int         avatar_y = 0;

    std::unordered_map<std::string, std::vector<RoomItem>> room_items;

    // Input / movement (Bitsy-compatible hold-to-move).
    Dir    cur_dir        = Dir::None;
    double hold_timer_ms  = 0.0;
    bool   any_held       = false;
    bool   ignore_input   = false;

    // Linear dialog.
    bool                       dlg_open = false;
    std::vector<std::string>   dlg_pages;
    std::size_t                dlg_index = 0;
    std::function<void()>      dlg_on_end;
    std::vector<std::uint8_t>  textbox_pixels;

    // Framebuffer / audio state owned by the engine.
    std::array<std::uint8_t, kVideoSize * kVideoSize> video{};
    std::array<std::uint8_t, kMapSize   * kMapSize>   map1{};
    std::array<std::uint8_t, kMapSize   * kMapSize>   map2{};
    std::vector<Color>                                palette;
    SoundChannel                                      sound1;
    SoundChannel                                      sound2;

    explicit Impl(Game g) : game(std::move(g)) {
        seed_palette_fallback();
    }

    void seed_palette_fallback() {
        if (!game.palettes.empty()) {
            auto it = game.palettes.find("0");
            if (it == game.palettes.end()) it = game.palettes.begin();
            palette = it->second.colors;
        }
        while (palette.size() < 3) palette.push_back({});
    }

    [[nodiscard]] const Room* current_room() const {
        auto it = game.rooms.find(current_room_id);
        return it != game.rooms.end() ? &it->second : nullptr;
    }

    [[nodiscard]] std::vector<RoomItem>& items_in(const std::string& room_id) {
        return room_items[room_id];
    }

    void load_room_palette() {
        const Room* room = current_room();
        std::string pal_id = (room && !room->palette_id.empty()) ? room->palette_id : "0";
        auto it = game.palettes.find(pal_id);
        if (it == game.palettes.end() && !game.palettes.empty()) {
            it = game.palettes.begin();
        }
        if (it != game.palettes.end()) palette = it->second.colors;
        while (palette.size() < 3) palette.push_back({});
    }

    void enter_room(std::string room_id, int x, int y) {
        current_room_id = std::move(room_id);
        avatar_x = x;
        avatar_y = y;
        load_room_palette();
    }

    void init_runtime() {
        current_room_id = game.start_room_id();
        avatar_x = 0;
        avatar_y = 0;
        if (const Sprite* av = game.avatar(); av && av->position) {
            current_room_id = av->position->room_id;
            avatar_x = av->position->x;
            avatar_y = av->position->y;
        }

        room_items.clear();
        for (const auto& [id, room] : game.rooms) {
            room_items[id] = room.items;
        }

        dlg_open = false;
        dlg_pages.clear();
        dlg_index = 0;
        dlg_on_end = {};
        cur_dir = Dir::None;
        hold_timer_ms = 0;
        any_held = false;
        ignore_input = false;

        load_room_palette();
        compose();
    }

    void compose() {
        ComposeState st;
        st.game = &game;
        st.room = current_room();
        auto it = room_items.find(current_room_id);
        st.items = (it != room_items.end()) ? &it->second : nullptr;
        st.room_id = current_room_id;
        st.avatar_x = avatar_x;
        st.avatar_y = avatar_y;
        compose_room(st, ComposeBuffers{map1, map2, video});
        refresh_textbox();
    }

    void refresh_textbox() {
        if (!dlg_open) {
            textbox_pixels.clear();
            return;
        }
        textbox_pixels.assign(
            static_cast<std::size_t>(kTextboxWidth * kTextboxHeight), std::uint8_t{1});
        // Carve a 1-pixel inner margin so the box reads as a panel, not a solid slab.
        for (int y = 1; y < kTextboxHeight - 1; ++y) {
            for (int x = 1; x < kTextboxWidth - 1; ++x) {
                textbox_pixels[static_cast<std::size_t>(y * kTextboxWidth + x)] = 0;
            }
        }
    }

    [[nodiscard]] std::string_view dialog_line() const {
        if (!dlg_open || dlg_index >= dlg_pages.size()) return {};
        return dlg_pages[dlg_index];
    }

    [[nodiscard]] bool is_wall_at(int x, int y) const {
        if (x < 0 || y < 0 || x >= kMapSize || y >= kMapSize) return true;
        const Room* room = current_room();
        if (!room) return true;
        const std::string& tid =
            room->tiles[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)];
        if (tid.empty() || tid == "0") return false;
        auto it = game.tiles.find(tid);
        if (it == game.tiles.end()) return false;
        return it->second.is_wall;
    }

    [[nodiscard]] const Sprite* sprite_at(int x, int y) const {
        for (const auto& [id, spr] : game.sprites) {
            if (id == Game::kAvatarId) continue;
            if (!spr.position) continue;
            if (spr.position->room_id != current_room_id) continue;
            if (spr.position->x == x && spr.position->y == y) return &spr;
        }
        return nullptr;
    }

    [[nodiscard]] int item_index_at(int x, int y) {
        auto& items = items_in(current_room_id);
        for (int i = 0; i < static_cast<int>(items.size()); ++i) {
            if (items[static_cast<std::size_t>(i)].x == x &&
                items[static_cast<std::size_t>(i)].y == y) {
                return i;
            }
        }
        return -1;
    }

    [[nodiscard]] const Exit* exit_at(int x, int y) const {
        const Room* room = current_room();
        if (!room) return nullptr;
        for (const Exit& e : room->exits) {
            if (e.x == x && e.y == y) return &e;
        }
        return nullptr;
    }

    [[nodiscard]] std::vector<std::string> pages_for_dialog(const std::string& dialog_id) const {
        if (dialog_id.empty()) return {};
        auto it = game.dialogues.find(dialog_id);
        if (it == game.dialogues.end()) return {};
        return extract_dialog_pages(it->second.content);
    }

    void finish_dialog(bool from_player) {
        dlg_open = false;
        dlg_pages.clear();
        dlg_index = 0;
        if (from_player) {
            ignore_input = true;
            cur_dir = Dir::None;
        }
        auto cb = std::move(dlg_on_end);
        dlg_on_end = {};
        if (cb) cb();
    }

    void begin_dialog(std::vector<std::string> pages, std::function<void()> on_end) {
        auto prev = std::move(dlg_on_end);
        if (prev && on_end) {
            dlg_on_end = [p = std::move(prev), n = std::move(on_end)] {
                p();
                n();
            };
        } else if (prev) {
            dlg_on_end = std::move(prev);
        } else {
            dlg_on_end = std::move(on_end);
        }

        pages.erase(std::remove_if(pages.begin(), pages.end(),
                                   [](const std::string& s) { return s.empty(); }),
                    pages.end());

        if (pages.empty()) {
            if (!dlg_open) finish_dialog(false);
            return;
        }

        dlg_pages = std::move(pages);
        dlg_index = 0;
        dlg_open = true;
    }

    void advance_dialog() {
        if (!dlg_open) return;
        if (dlg_index + 1 < dlg_pages.size()) {
            ++dlg_index;
            return;
        }
        finish_dialog(true);
    }

    void take_exit(const Exit& ext) {
        // Copy fields — `ext` may dangle if we ever mutate rooms.
        const std::string dest = ext.dest_room_id;
        const int dx = ext.dest_x;
        const int dy = ext.dest_y;
        auto pages = pages_for_dialog(ext.dialog_id);
        pages.erase(std::remove_if(pages.begin(), pages.end(),
                                   [](const std::string& s) { return s.empty(); }),
                    pages.end());
        if (pages.empty()) {
            // Instant warp (Phase 3 will play transition_effect).
            enter_room(dest, dx, dy);
            return;
        }
        begin_dialog(std::move(pages), [this, dest, dx, dy] {
            enter_room(dest, dx, dy);
        });
    }

    void handle_item(int index) {
        auto& items = items_in(current_room_id);
        if (index < 0 || index >= static_cast<int>(items.size())) return;
        const RoomItem& ri = items[static_cast<std::size_t>(index)];

        std::string dlg_id = ri.dialog_id;
        if (dlg_id.empty()) {
            auto it = game.items.find(ri.item_id);
            if (it != game.items.end()) dlg_id = it->second.dialog_id;
        }

        const std::string room_id = current_room_id;
        auto pages = pages_for_dialog(dlg_id);
        begin_dialog(std::move(pages), [this, room_id, index] {
            auto it = room_items.find(room_id);
            if (it == room_items.end()) return;
            if (index >= 0 && index < static_cast<int>(it->second.size())) {
                it->second.erase(it->second.begin() + index);
            }
        });
    }

    void handle_sprite(const Sprite& spr) {
        begin_dialog(pages_for_dialog(spr.dialog_id), {});
    }

    void try_move(Dir direction) {
        if (direction == Dir::None) return;
        if (current_room_id.empty() || !current_room()) return;

        const int nx = avatar_x + dir_dx(direction);
        const int ny = avatar_y + dir_dy(direction);

        const Sprite* bumped = sprite_at(nx, ny);
        const bool wall = is_wall_at(nx, ny);

        if (!bumped && !wall) {
            avatar_x = nx;
            avatar_y = ny;
        }

        // Bitsy: pick up an item AND walk through a door on the same turn.
        const int itm = item_index_at(avatar_x, avatar_y);
        if (itm >= 0) handle_item(itm);

        const Exit* ext = exit_at(avatar_x, avatar_y);
        if (ext) {
            take_exit(*ext);
        } else if (bumped) {
            handle_sprite(*bumped);
        }
    }

    void handle_input(Host& host) {
        const bool down = any_action_down(host);

        if (dlg_open) {
            if (!any_held && down) advance_dialog();
        } else if (!ignore_input) {
            const Dir prev = cur_dir;
            cur_dir = read_direction(host);
            if (cur_dir != Dir::None && cur_dir != prev) {
                try_move(cur_dir);
                hold_timer_ms = kFirstHoldMs;
            }
        }

        if (!down) ignore_input = false;
        any_held = down;
    }

    void hold_repeat(double dt_ms) {
        if (dlg_open || ignore_input) return;
        if (cur_dir == Dir::None) return;
        hold_timer_ms -= dt_ms;
        if (hold_timer_ms <= 0.0) {
            try_move(cur_dir);
            hold_timer_ms = kRepeatHoldMs;
        }
    }

    void do_present(Host& host) const {
        TextboxView textbox{};
        if (dlg_open) {
            textbox.visible = true;
            textbox.width = kTextboxWidth;
            textbox.height = kTextboxHeight;
            textbox.pixels = std::span<const std::uint8_t>(textbox_pixels);
        }

        host.present(
            GraphicsMode::Map,
            TextMode::HiRez,
            std::span<const Color>(palette),
            std::span<const std::uint8_t>(video),
            std::span<const std::uint8_t>(map1),
            std::span<const std::uint8_t>(map2),
            textbox,
            sound1,
            sound2
        );
    }
};

// ===========================================================================
// Engine  — public methods
// ===========================================================================

Engine::Engine(std::string_view bitsy_text)
    : impl_(std::make_unique<Impl>(citsy::parse(bitsy_text)))
{}

Engine Engine::from_file(const std::string& path) {
    std::ifstream file(path);
    if (!file)
        throw std::ios_base::failure("cannot open file: " + path);
    std::ostringstream buf;
    buf << file.rdbuf();
    return Engine(buf.str());
}

Engine::~Engine() = default;
Engine::Engine(Engine&&) noexcept = default;
Engine& Engine::operator=(Engine&&) noexcept = default;

void Engine::start(Host& host) {
    impl_->running = true;
    impl_->init_runtime();
    host.on_engine_ready();
}

void Engine::update(Host& host) {
    if (!impl_->running) return;

    impl_->handle_input(host);
    impl_->hold_repeat(host.delta_time_ms());
    impl_->compose();
    impl_->do_present(host);
}

bool Engine::is_running() const noexcept {
    return impl_ && impl_->running;
}

std::string Engine::current_room_id() const {
    return impl_ ? impl_->current_room_id : std::string{};
}

int Engine::avatar_x() const noexcept {
    return impl_ ? impl_->avatar_x : 0;
}

int Engine::avatar_y() const noexcept {
    return impl_ ? impl_->avatar_y : 0;
}

bool Engine::dialog_active() const noexcept {
    return impl_ && impl_->dlg_open;
}

std::string_view Engine::dialog_line() const noexcept {
    return impl_ ? impl_->dialog_line() : std::string_view{};
}

} // namespace citsy
