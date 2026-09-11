#pragma once

// Bitsy 8.15 dialog script interpreter.
// Evaluates quoted text, {tags}, conditionals, lists, and world actions.

#include "src/font/font.hpp"
#include "src/model/game.hpp"

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

namespace citsy {

struct DialogValue {
    enum class Kind { Number, String };
    Kind        kind = Kind::Number;
    double      number = 0;
    std::string str;

    static DialogValue from_number(double n) {
        DialogValue v;
        v.kind = Kind::Number;
        v.number = n;
        return v;
    }
    static DialogValue from_string(std::string s) {
        DialogValue v;
        v.kind = Kind::String;
        v.str = std::move(s);
        return v;
    }

    [[nodiscard]] bool is_truthy() const {
        if (kind == Kind::Number) return number != 0;
        return !str.empty() && str != "0" && str != "false";
    }

    [[nodiscard]] double as_number() const {
        if (kind == Kind::Number) return number;
        try { return std::stod(str); } catch (...) { return 0; }
    }

    [[nodiscard]] std::string as_string() const {
        if (kind == Kind::String) return str;
        if (number == static_cast<double>(static_cast<long long>(number)))
            return std::to_string(static_cast<long long>(number));
        return std::to_string(number);
    }
};

/// Engine callbacks the VM uses to read/write game state.
struct DialogWorld {
    std::function<DialogValue(std::string_view)> get_var;
    std::function<void(std::string_view, const DialogValue&)> set_var;
    std::function<int(std::string_view)> item_count;
    std::function<void(std::string_view, int)> set_item;  ///< Bitsy `{item id n}` *sets* count
    std::function<DialogValue(std::string_view)> get_property;
    std::function<void(std::string_view, const DialogValue&)> set_property;
    std::function<void(std::string_view)> set_avatar;
    std::function<void(std::string_view)> set_palette;
    std::function<void(std::string_view)> set_tune;
    std::function<void(std::string_view)> play_blip;
    std::function<void(std::string room, int x, int y, std::string fx)> do_exit;
    std::function<void()> do_end;
    std::function<void()> do_lock;
    std::function<const Tile*(std::string_view)> find_tile;
    std::function<const Sprite*(std::string_view)> find_sprite;
    std::function<const Item*(std::string_view)> find_item;
    std::function<int()> anim_frame;
};

class DialogVM {
public:
    void reset();

    /// Begin interpreting @p source.  Stops at the first page of text.
    void start(std::string source, DialogWorld world,
               std::string dialog_id = {},
               std::function<void()> on_end = {});

    /// Advance to the next page (or finish).  Returns false when dialog ends.
    bool continue_page();

    [[nodiscard]] bool active() const noexcept { return active_; }
    [[nodiscard]] const std::vector<TextSpan>& spans() const { return spans_; }
    [[nodiscard]] std::string plain_text() const { return spans_to_plain(spans_); }

    std::function<void()> on_end;

    /// Persistent list cursors (sequence / cycle / shuffle), keyed by dialog+site.
    std::unordered_map<std::string, int> list_cursors;
    std::unordered_map<std::string, std::vector<int>> shuffle_orders;

private:
    std::string source_;
    std::string dialog_id_;
    std::size_t pos_ = 0;
    bool active_ = false;
    bool page_ready_ = false;
    int  list_serial_ = 0;

    DialogWorld world_;
    std::vector<TextSpan> spans_;
    std::vector<GlyphEffect> fx_stack_;
    std::vector<int> color_stack_;
    std::string pending_;

    void run_until_pause();
    void exec_chunk(std::string_view chunk, bool implicit_page_after_string);
    bool exec_tag(std::string_view tag);
    void exec_block(std::string_view inner);
    void emit_text(std::string_view text);
    void emit_drawing(const TileFrame& frame, std::uint8_t color);
    void new_page();

    DialogValue eval_expr(std::string_view expr);
    DialogValue call_func(std::string_view name, const std::vector<std::string>& args,
                          bool as_statement);

    [[nodiscard]] GlyphEffect current_fx() const {
        return fx_stack_.empty() ? GlyphEffect::None : fx_stack_.back();
    }
    [[nodiscard]] int current_color() const {
        return color_stack_.empty() ? 2 : color_stack_.back();
    }
};

/// Still used by unit tests and as a fallback for unquoted ending text.
[[nodiscard]] std::vector<std::string> extract_dialog_pages(std::string_view content);

using Value = DialogValue;

struct DialogExit {
    std::string room_id;
    int         x = 0;
    int         y = 0;
    std::string effect;
};

struct DialogResult {
    std::vector<std::string> pages;
    bool                     end_game = false;
    std::optional<DialogExit> exit;
};

/// Parsed script with persistent list/shuffle cursors (Phase 2 test API).
class DialogScript {
public:
    std::string source;
    DialogVM    vm;
};

[[nodiscard]] DialogScript parse_dialog_script(std::string_view source);
[[nodiscard]] DialogResult run_dialog_script(DialogScript& script, DialogWorld world);

} // namespace citsy
