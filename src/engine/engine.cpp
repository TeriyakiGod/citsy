#include <citsy/engine.hpp>

#include "src/dialog/script.hpp"
#include "src/font/font.hpp"
#include "src/model/game.hpp"
#include "src/parser/parser.hpp"
#include "src/render/compose.hpp"
#include "src/sound/sound.hpp"
#include "src/transition/transition.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace citsy {
namespace {

constexpr double kFirstHoldMs  = 500.0;
constexpr double kRepeatHoldMs = 150.0;
constexpr double kAnimMs       = 400.0;

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

std::string exit_key(std::string_view room, int x, int y) {
    return std::string(room) + ":" + std::to_string(x) + "," + std::to_string(y);
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
    std::string avatar_id{Game::kAvatarId};

    std::unordered_map<std::string, std::vector<RoomItem>> room_items;
    std::unordered_map<std::string, int> inventory;
    std::unordered_map<std::string, DialogValue> variables;
    std::unordered_map<std::string, bool> locked_exits;
    std::unordered_map<std::string, bool> locked_endings;

    Dir    cur_dir        = Dir::None;
    double hold_timer_ms  = 0.0;
    bool   any_held       = false;
    bool   ignore_input   = false;
    bool   menu_held      = false;

    DialogVM                   dlg;
    std::string                dialog_plain;
    std::vector<std::uint8_t>  textbox_pixels;
    double                     dialog_time_ms = 0;
    std::string                lock_key;
    bool                       lock_is_ending = false;

    bool ending_hold = false;  // ended, waiting for dismiss
    bool narrating   = false;

    BitsyFont   font;
    SoundPlayer sound;
    Transition  transition;
    GraphicsMode gfx_mode = GraphicsMode::Map;

    double anim_counter_ms = 0;
    int    anim_frame      = 0;

    std::array<std::uint8_t, kVideoSize * kVideoSize> video{};
    std::array<std::uint8_t, kMapSize   * kMapSize>   map1{};
    std::array<std::uint8_t, kMapSize   * kMapSize>   map2{};
    std::vector<Color>                                palette;

    explicit Impl(Game g) : game(std::move(g)) {
        font = game.font_data.empty() ? default_font()
                                      : parse_bitsyfont(game.font_data);
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
        apply_palette(pal_id);
    }

    void apply_palette(const std::string& pal_id) {
        const Palette* pal = game.find_palette(pal_id);
        if (!pal && !game.palettes.empty()) pal = &game.palettes.begin()->second;
        if (pal) palette = pal->colors;
        while (palette.size() < 3) palette.push_back({});
    }

    std::string resolve_item_id(std::string_view key) const {
        if (const Item* it = game.find_item(key)) return it->id;
        return std::string(key);
    }

    void apply_room_tune() {
        const Room* room = current_room();
        std::string tid = (room && !room->tune_id.empty()) ? room->tune_id : "0";
        if (tid == "0") {
            sound.stop_tune();
            return;
        }
        const Tune* t = game.find_tune(tid);
        if (!t) {
            sound.stop_tune();
            return;
        }
        if (sound.tune_id() != t->id) sound.play_tune(*t);
    }

    void apply_room_avatar() {
        const Room* room = current_room();
        if (room && !room->avatar_id.empty()) {
            if (const Sprite* s = game.find_sprite(room->avatar_id))
                avatar_id = s->id;
            else
                avatar_id = room->avatar_id;
        } else {
            avatar_id = std::string(Game::kAvatarId);
        }
    }

    DialogWorld make_world() {
        DialogWorld w;
        w.get_var = [this](std::string_view name) -> DialogValue {
            auto it = variables.find(std::string(name));
            if (it == variables.end()) return DialogValue::from_string({});
            return it->second;
        };
        w.set_var = [this](std::string_view name, const DialogValue& v) {
            variables[std::string(name)] = v;
        };
        w.item_count = [this](std::string_view id) {
            const std::string key = resolve_item_id(id);
            auto it = inventory.find(key);
            return it == inventory.end() ? 0 : it->second;
        };
        w.set_item = [this](std::string_view id, int count) {
            inventory[resolve_item_id(id)] = std::max(0, count);
        };
        w.get_property = [this](std::string_view name) -> DialogValue {
            if (name != "locked" || lock_key.empty())
                return DialogValue::from_number(0);
            const bool on = lock_is_ending ? locked_endings[lock_key]
                                           : locked_exits[lock_key];
            return DialogValue::from_number(on ? 1 : 0);
        };
        w.set_property = [this](std::string_view name, const DialogValue& v) {
            if (name != "locked" || lock_key.empty()) return;
            const bool on = v.is_truthy();
            if (lock_is_ending) locked_endings[lock_key] = on;
            else locked_exits[lock_key] = on;
        };
        w.set_avatar = [this](std::string_view id) {
            if (const Sprite* s = game.find_sprite(id)) avatar_id = s->id;
            else avatar_id = std::string(id);
        };
        w.set_palette = [this](std::string_view id) {
            apply_palette(std::string(id));
        };
        w.set_tune = [this](std::string_view id) {
            auto s = std::string(id);
            if (s.empty() || s == "0") {
                sound.stop_tune();
                return;
            }
            if (const Tune* t = game.find_tune(s)) sound.play_tune(*t);
        };
        w.play_blip = [this](std::string_view id) {
            if (const Blip* b = game.find_blip(id)) sound.play_blip(*b, game);
        };
        w.do_exit = [this](std::string room, int x, int y, std::string fx) {
            Exit ext;
            if (const Room* r = game.find_room(room)) ext.dest_room_id = r->id;
            else ext.dest_room_id = std::move(room);
            ext.dest_x = x;
            ext.dest_y = y;
            ext.transition_effect = std::move(fx);
            take_exit(ext, /*skip_dialog=*/true);
        };
        w.do_end = [this] {
            trigger_ending("", /*from_script=*/true);
        };
        w.do_lock = [this] {
            if (lock_key.empty()) return;
            if (lock_is_ending) locked_endings[lock_key] = true;
            else locked_exits[lock_key] = true;
        };
        w.find_tile = [this](std::string_view id) -> const Tile* {
            return game.find_tile(id);
        };
        w.find_sprite = [this](std::string_view id) -> const Sprite* {
            return game.find_sprite(id);
        };
        w.find_item = [this](std::string_view id) -> const Item* {
            return game.find_item(id);
        };
        w.anim_frame = [this] { return anim_frame; };
        return w;
    }

    void enter_room(std::string room_id, int x, int y) {
        current_room_id = std::move(room_id);
        avatar_x = x;
        avatar_y = y;
        load_room_palette();
        apply_room_avatar();
        apply_room_tune();
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

        inventory.clear();
        if (const Sprite* av = game.avatar()) {
            inventory = av->inventory;
        }

        variables.clear();
        for (const auto& [name, var] : game.variables) {
            if (var.value == "true") {
                variables[name] = DialogValue::from_number(1);
            } else if (var.value == "false") {
                variables[name] = DialogValue::from_number(0);
            } else if (looks_like_number(var.value)) {
                try {
                    variables[name] = DialogValue::from_number(std::stod(var.value));
                } catch (...) {
                    variables[name] = DialogValue::from_string(var.value);
                }
            } else {
                variables[name] = DialogValue::from_string(var.value);
            }
        }

        locked_exits.clear();
        locked_endings.clear();
        dlg.reset();
        dlg.list_cursors.clear();
        dialog_plain.clear();
        ending_hold = false;
        narrating = false;
        cur_dir = Dir::None;
        hold_timer_ms = 0;
        any_held = false;
        ignore_input = false;
        menu_held = false;
        anim_counter_ms = 0;
        anim_frame = 0;
        gfx_mode = GraphicsMode::Map;
        sound.stop_tune();

        load_room_palette();
        apply_room_avatar();
        apply_room_tune();
        compose();
    }

    static bool looks_like_number(std::string_view s) {
        if (s.empty()) return false;
        std::size_t i = 0;
        if (s[0] == '-' || s[0] == '+') ++i;
        bool digit = false;
        for (; i < s.size(); ++i) {
            if (s[i] == '.') continue;
            if (s[i] < '0' || s[i] > '9') return false;
            digit = true;
        }
        return digit;
    }

    void snapshot_room(TransitionFrame& frame, const std::string& room_id,
                       int ax, int ay, const std::string& ava) {
        ComposeState st;
        st.game = &game;
        auto rit = game.rooms.find(room_id);
        st.room = rit != game.rooms.end() ? &rit->second : nullptr;
        auto iit = room_items.find(room_id);
        st.items = (iit != room_items.end()) ? &iit->second : nullptr;
        st.room_id = room_id;
        st.avatar_x = ax;
        st.avatar_y = ay;
        st.anim_frame = anim_frame;
        st.avatar_id = ava;
        compose_room(st, ComposeBuffers{map1, map2, video});
        frame.pixels = video;
        frame.player_x = ax;
        frame.player_y = ay;
        std::string pal_id = (st.room && !st.room->palette_id.empty())
            ? st.room->palette_id : "0";
        auto pit = game.palettes.find(pal_id);
        if (pit != game.palettes.end()) frame.palette = pit->second.colors;
        else frame.palette = palette;
        while (frame.palette.size() < 3) frame.palette.push_back({});
    }

    void compose() {
        if (narrating) {
            map1.fill(0);
            map2.fill(0);
            video.fill(0);
        } else if (!transition.active()) {
            ComposeState st;
            st.game = &game;
            st.room = current_room();
            auto it = room_items.find(current_room_id);
            st.items = (it != room_items.end()) ? &it->second : nullptr;
            st.room_id = current_room_id;
            st.avatar_x = avatar_x;
            st.avatar_y = avatar_y;
            st.anim_frame = anim_frame;
            st.avatar_id = avatar_id;
            compose_room(st, ComposeBuffers{map1, map2, video});
        }
        refresh_textbox();
    }

    void refresh_textbox() {
        if (!dlg.active()) {
            textbox_pixels.clear();
            dialog_plain.clear();
            return;
        }
        dialog_plain = dlg.plain_text();
        TextboxLayout layout;
        layout.width = kTextboxWidth;
        layout.height = kTextboxHeight;
        layout.rtl = game.text_direction == TextDirection::RightToLeft;
        layout.show_arrow = true;
        layout.time_ms = dialog_time_ms;
        textbox_pixels = render_textbox(font, dlg.spans(), layout);
    }

    [[nodiscard]] std::string_view dialog_line() const {
        return dialog_plain;
    }

    [[nodiscard]] bool is_wall_at(int x, int y) const {
        if (x < 0 || y < 0 || x >= kMapSize || y >= kMapSize) return true;
        const Room* room = current_room();
        if (!room) return true;
        const std::string& tid =
            room->tiles[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)];
        if (tid.empty() || tid == "0") return false;
        if (std::find(room->wall_ids.begin(), room->wall_ids.end(), tid) !=
            room->wall_ids.end()) {
            return true;
        }
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

    std::string dialog_source(const std::string& dialog_id) const {
        if (dialog_id.empty()) return {};
        auto it = game.dialogues.find(dialog_id);
        if (it != game.dialogues.end()) return it->second.content;
        return {};
    }

    std::string sprite_dialog_id(const Sprite& spr) const {
        if (!spr.dialog_id.empty()) return spr.dialog_id;
        if (game.dlg_compat) {
            if (game.dialogues.count(spr.id)) return spr.id;
        }
        return {};
    }

    void finish_dialog(bool from_player) {
        if (from_player) {
            ignore_input = true;
            cur_dir = Dir::None;
        }
        dialog_plain.clear();
        textbox_pixels.clear();
    }

    void start_dialog(std::string source, std::function<void()> on_end) {
        dialog_time_ms = 0;
        dlg.start(std::move(source), make_world(), /*id*/ lock_key, std::move(on_end));
        if (!dlg.active()) {
            finish_dialog(false);
        } else {
            refresh_textbox();
        }
    }

    void advance_dialog() {
        if (!dlg.active()) return;
        if (!dlg.continue_page()) {
            finish_dialog(true);
        } else {
            refresh_textbox();
        }
    }

    void warp_now(const Exit& ext) {
        enter_room(ext.dest_room_id, ext.dest_x, ext.dest_y);
    }

    void begin_transition_to(const Exit& ext) {
        const std::string fx = ext.transition_effect;
        if (fx.empty() || fx == "none" || !is_known_transition(fx)) {
            warp_now(ext);
            return;
        }

        TransitionFrame start_f, end_f;
        snapshot_room(start_f, current_room_id, avatar_x, avatar_y, avatar_id);

        const std::string dest = ext.dest_room_id;
        std::string dest_ava = std::string(Game::kAvatarId);
        auto rit = game.rooms.find(dest);
        if (rit != game.rooms.end() && !rit->second.avatar_id.empty())
            dest_ava = rit->second.avatar_id;
        snapshot_room(end_f, dest, ext.dest_x, ext.dest_y, dest_ava);

        gfx_mode = GraphicsMode::Video;
        transition.begin(start_f, end_f, fx, [this, ext] {
            gfx_mode = GraphicsMode::Map;
            warp_now(ext);
        });
        // First paint.
        std::vector<Color> pal = palette;
        transition.update(0, video, pal);
        palette = std::move(pal);
    }

    void take_exit(const Exit& ext, bool skip_dialog = false) {
        const std::string key = exit_key(current_room_id, ext.x, ext.y);
        auto pages_src = skip_dialog ? std::string{} : dialog_source(ext.dialog_id);

        auto go = [this, ext, key] {
            if (locked_exits[key]) return;
            begin_transition_to(ext);
        };

        if (pages_src.empty()) {
            go();
            return;
        }
        lock_key = key;
        lock_is_ending = false;
        start_dialog(std::move(pages_src), std::move(go));
    }

    void trigger_ending(const std::string& ending_id, bool from_script) {
        const std::string key = from_script
            ? std::string{"script"}
            : exit_key(current_room_id, avatar_x, avatar_y);

        std::string src;
        auto dit = game.dialogues.find(ending_id);
        if (dit != game.dialogues.end()) src = dit->second.content;
        else {
            auto eit = game.endings.find(ending_id);
            if (eit != game.endings.end()) src = eit->second.text;
        }

        sound.stop_tune();
        narrating = true;
        lock_key = key;
        lock_is_ending = true;

        auto after = [this, key] {
            if (locked_endings[key]) {
                narrating = false;
                apply_room_tune();
                return;
            }
            ending_hold = true;
        };

        if (src.empty() && from_script) {
            after();
            return;
        }
        start_dialog(src.empty() ? std::string{" "} : std::move(src), std::move(after));
    }

    void handle_item(int index) {
        auto& items = items_in(current_room_id);
        if (index < 0 || index >= static_cast<int>(items.size())) return;
        const RoomItem ri = items[static_cast<std::size_t>(index)];

        std::string dlg_id = ri.dialog_id;
        std::string blip_id;
        if (auto it = game.items.find(ri.item_id); it != game.items.end()) {
            if (dlg_id.empty()) dlg_id = it->second.dialog_id;
            blip_id = it->second.blip_id;
        }
        if (!blip_id.empty()) {
            auto bit = game.blips.find(blip_id);
            if (bit != game.blips.end()) sound.play_blip(bit->second, game);
        }

        const std::string room_id = current_room_id;
        const std::string item_id = ri.item_id;
        auto pickup = [this, room_id, index, item_id] {
            auto it = room_items.find(room_id);
            if (it != room_items.end() &&
                index >= 0 && index < static_cast<int>(it->second.size())) {
                it->second.erase(it->second.begin() + index);
            }
            inventory[item_id] += 1;
        };

        auto src = dialog_source(dlg_id);
        if (src.empty()) {
            pickup();
            return;
        }
        start_dialog(std::move(src), std::move(pickup));
    }

    void handle_sprite(const Sprite& spr) {
        if (!spr.blip_id.empty()) {
            auto it = game.blips.find(spr.blip_id);
            if (it != game.blips.end()) sound.play_blip(it->second, game);
        }
        start_dialog(dialog_source(sprite_dialog_id(spr)), {});
    }

    void try_move(Dir direction) {
        if (direction == Dir::None) return;
        if (current_room_id.empty() || !current_room()) return;
        if (ending_hold || narrating) return;

        const int nx = avatar_x + dir_dx(direction);
        const int ny = avatar_y + dir_dy(direction);

        const Sprite* bumped = sprite_at(nx, ny);
        const bool wall = is_wall_at(nx, ny);

        if (!bumped && !wall) {
            avatar_x = nx;
            avatar_y = ny;
        }

        const int itm = item_index_at(avatar_x, avatar_y);
        if (itm >= 0) handle_item(itm);

        if (const EndingRef* end = ending_at(avatar_x, avatar_y)) {
            trigger_ending(end->ending_id, false);
        } else if (const Exit* ext = exit_at(avatar_x, avatar_y)) {
            take_exit(*ext);
        } else if (bumped) {
            handle_sprite(*bumped);
        }
    }

    void handle_input(Host& host) {
        const bool down = any_action_down(host);

        if (dlg.active()) {
            if (!any_held && down) advance_dialog();
        } else if (ending_hold) {
            if (!any_held && down) {
                running = false;
            }
        } else if (!ignore_input && !transition.active()) {
            const Dir prev = cur_dir;
            cur_dir = read_direction(host);
            if (cur_dir != Dir::None && cur_dir != prev) {
                try_move(cur_dir);
                hold_timer_ms = kFirstHoldMs;
            }
        }

        if (!down) ignore_input = false;
        any_held = down;

        const bool menu = host.button(Button::Menu);
        if (menu_held && !menu) {
            running = false;
        }
        menu_held = menu;
    }

    void hold_repeat(double dt_ms) {
        if (dlg.active() || ignore_input || ending_hold || transition.active()) return;
        if (cur_dir == Dir::None) return;
        hold_timer_ms -= dt_ms;
        if (hold_timer_ms <= 0.0) {
            try_move(cur_dir);
            hold_timer_ms = kRepeatHoldMs;
        }
    }

    void tick_animation(double dt_ms) {
        if (narrating || transition.active()) return;
        anim_counter_ms += dt_ms;
        if (anim_counter_ms >= kAnimMs) {
            anim_counter_ms = 0;
            ++anim_frame;
        }
    }

    void do_present(Host& host) const {
        TextboxView textbox{};
        if (dlg.active()) {
            textbox.visible = true;
            textbox.width = kTextboxWidth;
            textbox.height = kTextboxHeight;
            textbox.x = 12;
            if (narrating || ending_hold) {
                textbox.y = (kVideoSize / 2) - (kTextboxHeight / 2);
            } else if (avatar_y < kMapSize / 2) {
                textbox.y = kVideoSize - 12 - kTextboxHeight;
            } else {
                textbox.y = 12;
            }
            textbox.pixels = std::span<const std::uint8_t>(textbox_pixels);
        }

        host.present(
            gfx_mode,
            game.txt_mode == 1 ? TextMode::LoRez : TextMode::HiRez,
            std::span<const Color>(palette),
            std::span<const std::uint8_t>(video),
            std::span<const std::uint8_t>(map1),
            std::span<const std::uint8_t>(map2),
            textbox,
            sound.channel1(),
            sound.channel2()
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
    if (!impl_->game.title_dialog.empty()) {
        impl_->narrating = true;
        impl_->start_dialog(impl_->game.title_dialog, [this] {
            impl_->narrating = false;
        });
    }
    host.on_engine_ready();
}

void Engine::update(Host& host) {
    if (!impl_->running) return;

    const double dt = host.delta_time_ms();

    if (impl_->transition.active()) {
        impl_->gfx_mode = GraphicsMode::Video;
        impl_->transition.update(dt, impl_->video, impl_->palette);
    } else {
        impl_->handle_input(host);
        impl_->hold_repeat(dt);
        impl_->tick_animation(dt);
        if (impl_->dlg.active()) impl_->dialog_time_ms += dt;
        impl_->sound.update(dt, impl_->game);
        impl_->compose();
    }

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
    return impl_ && impl_->dlg.active();
}

std::string_view Engine::dialog_line() const noexcept {
    return impl_ ? impl_->dialog_line() : std::string_view{};
}

int Engine::inventory_count(std::string_view item_id) const {
    if (!impl_) return 0;
    auto it = impl_->inventory.find(std::string(item_id));
    return it == impl_->inventory.end() ? 0 : it->second;
}

std::string Engine::variable(std::string_view name) const {
    if (!impl_) return {};
    auto it = impl_->variables.find(std::string(name));
    if (it == impl_->variables.end()) return {};
    return it->second.as_string();
}

bool Engine::ending_active() const noexcept {
    return impl_ && (impl_->ending_hold || impl_->narrating);
}

int Engine::anim_frame() const noexcept {
    return impl_ ? impl_->anim_frame : 0;
}

std::string Engine::avatar_appearance() const {
    return impl_ ? impl_->avatar_id : std::string(Game::kAvatarId);
}

} // namespace citsy
