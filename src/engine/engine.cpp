#include <citsy/engine.hpp>

#include "src/dialog/script.hpp"
#include "src/model/game.hpp"
#include "src/parser/parser.hpp"
#include "src/render/compose.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <functional>
#include <fstream>
#include <random>
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

[[nodiscard]] Value parse_init_value(std::string_view s) {
    if (s == "true")  return Value::number(1);
    if (s == "false") return Value::number(0);
    double n = 0;
    const char* begin = s.data();
    const char* end = s.data() + s.size();
    auto [ptr, ec] = std::from_chars(begin, end, n);
    if (ec == std::errc{} && ptr == end) return Value::number(n);
    return Value::string(std::string(s));
}

} // namespace

// ===========================================================================
// Engine::Impl
// ===========================================================================

struct Engine::Impl : DialogWorld {
    Game game;

    bool running = false;

    std::string current_room_id;
    int         avatar_x = 0;
    int         avatar_y = 0;

    std::unordered_map<std::string, std::vector<RoomItem>> room_items;
    std::unordered_map<std::string, Value>                 variables;
    std::unordered_map<std::string, int>                   inventory;
    std::unordered_map<std::string, DialogScript>          scripts;
    std::mt19937                                           rng{0xC175u};

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

    [[nodiscard]] std::string resolve_item_id(std::string_view id_or_name) const {
        std::string key{id_or_name};
        if (game.items.count(key)) return key;
        for (const auto& [id, itm] : game.items) {
            if (itm.name == key) return id;
        }
        return key;
    }

    Value get_var(std::string_view name) const override {
        auto it = variables.find(std::string(name));
        if (it == variables.end()) return Value::number(0);
        return it->second;
    }

    void set_var(std::string_view name, Value v) override {
        variables[std::string(name)] = std::move(v);
    }

    int get_item(std::string_view id_or_name) const override {
        auto it = inventory.find(resolve_item_id(id_or_name));
        return it == inventory.end() ? 0 : it->second;
    }

    void set_item(std::string_view id_or_name, int count) override {
        inventory[resolve_item_id(id_or_name)] = std::max(0, count);
    }

    std::string resolve_room(std::string_view id_or_name) const override {
        std::string key{id_or_name};
        if (game.rooms.count(key)) return key;
        for (const auto& [id, room] : game.rooms) {
            if (room.name == key) return id;
        }
        return key;
    }

    int random_int(int n) override {
        if (n <= 1) return 0;
        std::uniform_int_distribution<int> dist(0, n - 1);
        return dist(rng);
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

        variables.clear();
        for (const auto& [name, var] : game.variables) {
            variables[name] = parse_init_value(var.value);
        }

        inventory.clear();
        scripts.clear();
        rng.seed(0xC175u);

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

    [[nodiscard]] const EndingRef* ending_at(int x, int y) const {
        const Room* room = current_room();
        if (!room) return nullptr;
        for (const EndingRef& e : room->endings) {
            if (e.x == x && e.y == y) return &e;
        }
        return nullptr;
    }

    DialogScript& script_for(const std::string& key, std::string_view content) {
        auto it = scripts.find(key);
        if (it == scripts.end()) {
            it = scripts.emplace(key, parse_dialog_script(content)).first;
        }
        return it->second;
    }

    void play_script(const std::string& key, std::string_view content,
                     std::function<void()> on_end) {
        DialogResult r = run_dialog_script(script_for(key, content), *this);
        const bool end_game = r.end_game;
        auto queued_exit = std::move(r.exit);
        begin_dialog(std::move(r.pages),
                     [this, end_game, queued_exit = std::move(queued_exit),
                      on_end = std::move(on_end)]() mutable {
                         if (on_end) on_end();
                         if (queued_exit) {
                             enter_room(queued_exit->room_id, queued_exit->x,
                                        queued_exit->y);
                         }
                         if (end_game) running = false;
                     });
    }

    void play_dialog_id(const std::string& dialog_id, std::function<void()> on_end) {
        if (dialog_id.empty()) {
            begin_dialog({}, std::move(on_end));
            return;
        }
        auto it = game.dialogues.find(dialog_id);
        if (it == game.dialogues.end()) {
            begin_dialog({}, std::move(on_end));
            return;
        }
        play_script("DLG:" + dialog_id, it->second.content, std::move(on_end));
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
        const std::string dest = ext.dest_room_id;
        const int dx = ext.dest_x;
        const int dy = ext.dest_y;
        if (ext.dialog_id.empty()) {
            enter_room(dest, dx, dy);
            return;
        }
        play_dialog_id(ext.dialog_id, [this, dest, dx, dy] {
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

        inventory[ri.item_id] += 1;

        const std::string room_id = current_room_id;
        play_dialog_id(dlg_id, [this, room_id, index] {
            auto it = room_items.find(room_id);
            if (it == room_items.end()) return;
            if (index >= 0 && index < static_cast<int>(it->second.size())) {
                it->second.erase(it->second.begin() + index);
            }
        });
    }

    void handle_sprite(const Sprite& spr) {
        play_dialog_id(spr.dialog_id, {});
    }

    void handle_ending(const EndingRef& er) {
        std::string content;
        auto it = game.endings.find(er.ending_id);
        if (it != game.endings.end()) content = it->second.text;
        play_script("END:" + er.ending_id, content, [this] { running = false; });
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

        if (const EndingRef* er = ending_at(avatar_x, avatar_y)) {
            handle_ending(*er);
        } else if (const Exit* ext = exit_at(avatar_x, avatar_y)) {
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

int Engine::item_count(std::string_view id_or_name) const {
    return impl_ ? impl_->get_item(id_or_name) : 0;
}

std::string Engine::variable_value(std::string_view name) const {
    if (!impl_) return {};
    return impl_->get_var(name).as_string();
}

} // namespace citsy
