#include "src/dialog/script.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <numeric>
#include <random>
#include <sstream>

namespace citsy {
namespace {

std::string_view trim_sv(std::string_view s) {
    auto ws = [](unsigned char c) {
        return c == ' ' || c == '\t' || c == '\n' || c == '\r';
    };
    while (!s.empty() && ws(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
    while (!s.empty() && ws(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
    return s;
}

std::string unquote(std::string_view s) {
    s = trim_sv(s);
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"') {
        return std::string(s.substr(1, s.size() - 2));
    }
    return std::string(s);
}

std::vector<std::string> split_args(std::string_view s) {
    std::vector<std::string> out;
    std::string cur;
    bool in_str = false;
    for (std::size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (c == '"') {
            in_str = !in_str;
            cur.push_back(c);
        } else if (!in_str && (c == ' ' || c == '\t' || c == ',')) {
            if (!cur.empty()) {
                out.push_back(cur);
                cur.clear();
            }
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

bool looks_number(std::string_view s) {
    s = trim_sv(s);
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

std::string_view first_ident(std::string_view s) {
    s = trim_sv(s);
    std::size_t i = 0;
    if (i < s.size() && (std::isalpha(static_cast<unsigned char>(s[i])) || s[i] == '_')) {
        ++i;
        while (i < s.size() &&
               (std::isalnum(static_cast<unsigned char>(s[i])) || s[i] == '_')) {
            ++i;
        }
    }
    return s.substr(0, i);
}

// Find matching '}' from an opening '{' at @p open (the '{' itself).
std::size_t matching_brace(std::string_view s, std::size_t open) {
    int depth = 0;
    bool in_str = false;
    for (std::size_t i = open; i < s.size(); ++i) {
        if (s[i] == '"' && (i == 0 || s[i - 1] != '\\')) in_str = !in_str;
        if (in_str) continue;
        if (s[i] == '{') ++depth;
        else if (s[i] == '}') {
            --depth;
            if (depth == 0) return i;
        }
    }
    return std::string_view::npos;
}

} // namespace

void DialogVM::reset() {
    source_.clear();
    dialog_id_.clear();
    pos_ = 0;
    active_ = false;
    page_ready_ = false;
    list_serial_ = 0;
    spans_.clear();
    fx_stack_.clear();
    color_stack_.clear();
    pending_.clear();
    on_end = {};
    world_ = {};
}

void DialogVM::start(std::string source, DialogWorld world,
                     std::string dialog_id, std::function<void()> on_end_cb) {
    reset();
    source_ = std::move(source);
    dialog_id_ = std::move(dialog_id);
    world_ = std::move(world);
    on_end = std::move(on_end_cb);
    active_ = true;
    run_until_pause();
    if (!page_ready_ && spans_.empty()) {
        // No text — still fire on_end (empty dialog).
        active_ = false;
        auto cb = std::move(on_end);
        on_end = {};
        if (cb) cb();
    }
}

bool DialogVM::continue_page() {
    if (!active_) return false;
    spans_.clear();
    page_ready_ = false;
    run_until_pause();
    if (!page_ready_ && spans_.empty()) {
        active_ = false;
        auto cb = std::move(on_end);
        on_end = {};
        if (cb) cb();
        return false;
    }
    return true;
}

void DialogVM::new_page() {
    page_ready_ = true;
}

void DialogVM::emit_text(std::string_view text) {
    if (text.empty()) return;
    TextSpan sp;
    sp.text = std::string(text);
    sp.effect = current_fx();
    sp.color = current_color();
    spans_.push_back(std::move(sp));
}

void DialogVM::emit_drawing(const TileFrame& frame, std::uint8_t color) {
    TextSpan sp;
    sp.is_drawing = true;
    sp.drawing = frame;
    sp.drawing_color = color;
    spans_.push_back(std::move(sp));
}

void DialogVM::run_until_pause() {
    if (!pending_.empty()) {
        std::string rest = std::move(pending_);
        exec_chunk(rest, false);
        if (page_ready_) return;
    }

    const std::size_t n = source_.size();
    bool last_was_string = false;

    auto skip_ws = [&] {
        while (pos_ < n && (source_[pos_] == ' ' || source_[pos_] == '\t' ||
                            source_[pos_] == '\n' || source_[pos_] == '\r')) {
            ++pos_;
        }
    };

    while (pos_ < n && !page_ready_) {
        skip_ws();
        if (pos_ >= n) break;

        // Triple-quoted block.
        if (pos_ + 2 < n && source_[pos_] == '"' && source_[pos_ + 1] == '"' &&
            source_[pos_ + 2] == '"') {
            if (last_was_string && !spans_.empty()) {
                new_page();
                break;
            }
            pos_ += 3;
            if (pos_ < n && (source_[pos_] == '\n' || source_[pos_] == '\r')) {
                if (source_[pos_] == '\r' && pos_ + 1 < n && source_[pos_ + 1] == '\n')
                    pos_ += 2;
                else ++pos_;
            }
            const std::size_t start = pos_;
            while (pos_ + 2 < n &&
                   !(source_[pos_] == '"' && source_[pos_ + 1] == '"' &&
                     source_[pos_ + 2] == '"')) {
                ++pos_;
            }
            auto body = std::string_view(source_).substr(start, pos_ - start);
            if (!body.empty() && (body.back() == '\n' || body.back() == '\r')) {
                body.remove_suffix(1);
                if (!body.empty() && body.back() == '\r') body.remove_suffix(1);
            }
            exec_chunk(body, false);
            if (pos_ + 2 < n) pos_ += 3;
            else pos_ = n;
            last_was_string = true;
            if (page_ready_) break;
            continue;
        }

        if (source_[pos_] == '"') {
            if (last_was_string && !spans_.empty()) {
                new_page();
                break;
            }
            ++pos_;
            const std::size_t start = pos_;
            while (pos_ < n && source_[pos_] != '"') ++pos_;
            auto body = std::string_view(source_).substr(start, pos_ - start);
            if (pos_ < n) ++pos_;
            exec_chunk(body, false);
            last_was_string = true;
            if (page_ready_) break;
            continue;
        }

        if (source_[pos_] == '{') {
            const auto end = matching_brace(source_, pos_);
            if (end == std::string_view::npos) {
                ++pos_;
                continue;
            }
            auto inner = trim_sv(std::string_view(source_).substr(pos_ + 1, end - pos_ - 1));
            pos_ = end + 1;
            last_was_string = false;
            if (inner.find('\n') != std::string_view::npos ||
                inner.find('-') != std::string_view::npos) {
                exec_block(inner);
            } else {
                exec_tag(inner);
            }
            continue;
        }

        // Unquoted run (endings, title).
        const std::size_t start = pos_;
        while (pos_ < n && source_[pos_] != '"' && source_[pos_] != '{') ++pos_;
        auto run = trim_sv(std::string_view(source_).substr(start, pos_ - start));
        if (!run.empty()) {
            if (last_was_string && !spans_.empty()) {
                pos_ = start;
                new_page();
                break;
            }
            exec_chunk(run, false);
            last_was_string = true;
        }
    }
}

void DialogVM::exec_chunk(std::string_view chunk, bool /*implicit_page*/) {
    std::size_t i = 0;
    while (i < chunk.size() && !page_ready_) {
        while (i < chunk.size() && (chunk[i] == ' ' || chunk[i] == '\t' ||
                                    chunk[i] == '\n' || chunk[i] == '\r')) {
            ++i;
        }
        if (i >= chunk.size()) break;
        if (chunk[i] == '"') {
            ++i;
            const std::size_t start = i;
            while (i < chunk.size() && chunk[i] != '"') ++i;
            auto body = chunk.substr(start, i - start);
            if (i < chunk.size()) ++i;
            exec_chunk(body, false);
            continue;
        }
        if (chunk[i] == '{') {
            const auto end = matching_brace(chunk, i);
            if (end == std::string_view::npos) {
                emit_text(chunk.substr(i, 1));
                ++i;
                continue;
            }
            auto inner = trim_sv(chunk.substr(i + 1, end - i - 1));
            i = end + 1;
            if (inner.find('\n') != std::string_view::npos ||
                (!inner.empty() && inner[0] == '-')) {
                exec_block(inner);
            } else {
                const bool paused = exec_tag(inner);
                (void)paused;
                if (inner == "p" || inner == "pg") {
                    pending_ = std::string(chunk.substr(i));
                    return;
                }
            }
            continue;
        }
        const std::size_t start = i;
        while (i < chunk.size() && chunk[i] != '{') ++i;
        emit_text(chunk.substr(start, i - start));
    }
}

bool DialogVM::exec_tag(std::string_view tag) {
    tag = trim_sv(tag);
    if (tag.empty()) return false;

    // Close-effect tags.
    if (tag == "/wvy" || tag == "/shk" || tag == "/rbw") {
        if (!fx_stack_.empty()) fx_stack_.pop_back();
        return true;
    }
    if (tag == "/clr" || tag == "/clr1" || tag == "/clr2" || tag == "/clr3") {
        if (!color_stack_.empty()) color_stack_.pop_back();
        return true;
    }

    if (tag == "br") {
        emit_text("\n");
        return true;
    }
    if (tag == "p" || tag == "pg") {
        new_page();
        return true;
    }

    auto toggle_fx = [&](GlyphEffect e) {
        auto it = std::find(fx_stack_.rbegin(), fx_stack_.rend(), e);
        if (it != fx_stack_.rend()) {
            fx_stack_.erase(std::next(it).base());
        } else {
            fx_stack_.push_back(e);
        }
    };
    auto toggle_color = [&](int c) {
        auto it = std::find(color_stack_.rbegin(), color_stack_.rend(), c);
        if (it != color_stack_.rend()) {
            color_stack_.erase(std::next(it).base());
        } else {
            color_stack_.push_back(c);
        }
    };

    if (tag == "wvy") { toggle_fx(GlyphEffect::Wavy); return true; }
    if (tag == "shk") { toggle_fx(GlyphEffect::Shaky); return true; }
    if (tag == "rbw") { toggle_fx(GlyphEffect::Rainbow); return true; }
    if (tag == "clr" || tag == "clr1") { toggle_color(1); return true; }
    if (tag == "clr2") { toggle_color(2); return true; }
    if (tag == "clr3") { toggle_color(3); return true; }
    if (tag == "end") {
        if (world_.do_end) world_.do_end();
        return true;
    }
    if (tag == "lock") {
        if (world_.do_lock) world_.do_lock();
        return true;
    }

    // Assignment: `{name = expr}` — left side must be a single identifier.
    const auto eq = tag.find('=');
    if (eq != std::string_view::npos && eq > 0 &&
        tag.find("==") == std::string_view::npos &&
        tag.find("!=") == std::string_view::npos &&
        tag.find("<=") == std::string_view::npos &&
        tag.find(">=") == std::string_view::npos) {
        bool is_cmp = false;
        if (eq + 1 < tag.size() && tag[eq + 1] == '=') is_cmp = true;
        if (eq > 0 && (tag[eq - 1] == '<' || tag[eq - 1] == '>' || tag[eq - 1] == '!'))
            is_cmp = true;
        auto name = trim_sv(tag.substr(0, eq));
        if (!is_cmp && first_ident(name).size() == name.size()) {
            auto expr = trim_sv(tag.substr(eq + 1));
            auto val = eval_expr(expr);
            if (world_.set_var) world_.set_var(name, val);
            return true;
        }
    }

    auto ident = first_ident(tag);
    auto rest = trim_sv(tag.substr(ident.size()));
    auto args = split_args(rest);

    if (ident == "clr" && !args.empty()) {
        int idx = 2;
        try { idx = std::stoi(unquote(args[0])); } catch (...) {}
        color_stack_.push_back(idx);
        return true;
    }

    auto result = call_func(ident, args, /*as_statement=*/true);
    // Functions that produce a printable value (print / item / bare var).
    if (ident == "print" || ident == "say") {
        emit_text(result.as_string());
        return true;
    }
    if (ident.empty()) return false;

    // Bare variable / unknown tag: print its value (empty if unset).
    if (ident != "item" && ident != "exit" && ident != "ava" && ident != "pal" &&
        ident != "tune" && ident != "blip" && ident != "printSprite" &&
        ident != "printTile" && ident != "printItem" && ident != "end" &&
        ident != "lock" && ident != "property" && ident != "drwt" &&
        ident != "drws" && ident != "drwi") {
        // Could be `{name}` — print variable.
        if (args.empty()) {
            emit_text(result.as_string());
        }
    } else if (ident == "item" && args.size() <= 1) {
        // `{item id}` in text: print count.
        emit_text(result.as_string());
    }
    return true;
}

void DialogVM::exec_block(std::string_view inner) {
    inner = trim_sv(inner);

    enum class Mode { Sequence, Cycle, Shuffle, Conditional };
    Mode mode = Mode::Sequence;
    std::string_view body = inner;

    auto ident = first_ident(inner);
    if (ident == "sequence") {
        mode = Mode::Sequence;
        body = trim_sv(inner.substr(ident.size()));
    } else if (ident == "cycle") {
        mode = Mode::Cycle;
        body = trim_sv(inner.substr(ident.size()));
    } else if (ident == "shuffle") {
        mode = Mode::Shuffle;
        body = trim_sv(inner.substr(ident.size()));
    }

    struct Item {
        std::string condition;  // empty = always
        bool is_else = false;
        std::string body;
    };
    std::vector<Item> items;
    bool any_cond = false;

    // Split on lines starting with '-' (list items) or treat as a code block.
    std::string body_str(body);
    std::istringstream iss(body_str);
    std::string line;
    Item cur;
    bool in_item = false;
    auto flush = [&] {
        if (in_item) {
            cur.body = std::string(trim_sv(cur.body));
            items.push_back(std::move(cur));
            cur = {};
        }
        in_item = false;
    };

    while (std::getline(iss, line)) {
        auto t = trim_sv(line);
        if (t.starts_with("-")) {
            flush();
            in_item = true;
            auto rest = trim_sv(t.substr(1));
            // `cond ?` prefix
            auto q = rest.find('?');
            if (first_ident(rest) == "else") {
                cur.is_else = true;
                any_cond = true;
                auto after = trim_sv(rest.substr(4));
                if (!after.empty() && after[0] == '?') after = trim_sv(after.substr(1));
                cur.body = std::string(after) + "\n";
            } else if (q != std::string_view::npos) {
                cur.condition = std::string(trim_sv(rest.substr(0, q)));
                cur.body = std::string(trim_sv(rest.substr(q + 1))) + "\n";
                any_cond = true;
            } else {
                cur.body = std::string(rest) + "\n";
            }
        } else if (in_item) {
            cur.body += line;
            cur.body += '\n';
        } else if (!t.empty()) {
            // Bare statements inside `{ a = 1 \n "hi" }`
            exec_chunk(t, false);
        }
    }
    flush();

    if (items.empty()) return;

    if (any_cond) mode = Mode::Conditional;

    int chosen = -1;
    if (mode == Mode::Conditional) {
        bool matched = false;
        int else_i = -1;
        for (int i = 0; i < static_cast<int>(items.size()); ++i) {
            if (items[static_cast<std::size_t>(i)].is_else) {
                else_i = i;
                continue;
            }
            if (items[static_cast<std::size_t>(i)].condition.empty() ||
                eval_expr(items[static_cast<std::size_t>(i)].condition).is_truthy()) {
                chosen = i;
                matched = true;
                break;
            }
        }
        if (!matched) chosen = else_i;
    } else {
        const std::string key = dialog_id_ + "#" + std::to_string(list_serial_++) +
                                "#" + std::string(ident);
        const int n = static_cast<int>(items.size());
        if (mode == Mode::Cycle) {
            int& cursor = list_cursors[key];
            chosen = cursor % n;
            ++cursor;
        } else if (mode == Mode::Shuffle) {
            auto& order = shuffle_orders[key];
            if (order.empty()) {
                order.resize(static_cast<std::size_t>(n));
                std::iota(order.begin(), order.end(), 0);
                static thread_local std::mt19937 rng{std::random_device{}()};
                std::shuffle(order.begin(), order.end(), rng);
            }
            chosen = order.front();
            order.erase(order.begin());
        } else {  // sequence: first unused, then stay on last
            int& cursor = list_cursors[key];
            chosen = std::min(cursor, n - 1);
            if (cursor < n) ++cursor;
        }
    }

    if (chosen < 0 || chosen >= static_cast<int>(items.size())) return;
    exec_chunk(items[static_cast<std::size_t>(chosen)].body, false);
}

DialogValue DialogVM::eval_expr(std::string_view expr) {
    expr = trim_sv(expr);
    if (expr.empty()) return DialogValue::from_number(0);

    // Expand {tags} so `{has} == 1` works as a condition.
    if (expr.find('{') != std::string_view::npos) {
        std::string expanded;
        for (std::size_t i = 0; i < expr.size();) {
            if (expr[i] == '{') {
                auto end = matching_brace(expr, i);
                if (end == std::string_view::npos) {
                    expanded.push_back(expr[i++]);
                    continue;
                }
                auto inner = trim_sv(expr.substr(i + 1, end - i - 1));
                expanded += eval_expr(inner).as_string();
                i = end + 1;
            } else {
                expanded.push_back(expr[i++]);
            }
        }
        return eval_expr(expanded);
    }

    // Comparisons (lowest precedence among binary ops we care about).
    auto cmp_at = [&](std::string_view op) -> std::size_t {
        int depth = 0;
        bool in_str = false;
        for (std::size_t i = 0; i + op.size() <= expr.size(); ++i) {
            if (expr[i] == '"') in_str = !in_str;
            if (in_str) continue;
            if (expr[i] == '(') ++depth;
            else if (expr[i] == ')') --depth;
            if (depth == 0 && expr.substr(i, op.size()) == op) return i;
        }
        return std::string_view::npos;
    };

    auto do_cmp = [&](auto&& pred, std::string_view op) -> DialogValue {
        auto at = cmp_at(op);
        auto l = eval_expr(expr.substr(0, at));
        auto r = eval_expr(expr.substr(at + op.size()));
        return DialogValue::from_number(pred(l, r) ? 1 : 0);
    };

    if (cmp_at("==") != std::string_view::npos) {
        return do_cmp([](const DialogValue& a, const DialogValue& b) {
            if (a.kind == DialogValue::Kind::String || b.kind == DialogValue::Kind::String)
                return a.as_string() == b.as_string();
            return a.as_number() == b.as_number();
        }, "==");
    }
    if (cmp_at("!=") != std::string_view::npos) {
        return do_cmp([](const DialogValue& a, const DialogValue& b) {
            if (a.kind == DialogValue::Kind::String || b.kind == DialogValue::Kind::String)
                return a.as_string() != b.as_string();
            return a.as_number() != b.as_number();
        }, "!=");
    }
    if (cmp_at(">=") != std::string_view::npos) {
        return do_cmp([](const DialogValue& a, const DialogValue& b) {
            return a.as_number() >= b.as_number();
        }, ">=");
    }
    if (cmp_at("<=") != std::string_view::npos) {
        return do_cmp([](const DialogValue& a, const DialogValue& b) {
            return a.as_number() <= b.as_number();
        }, "<=");
    }
    if (cmp_at(">") != std::string_view::npos) {
        return do_cmp([](const DialogValue& a, const DialogValue& b) {
            return a.as_number() > b.as_number();
        }, ">");
    }
    if (cmp_at("<") != std::string_view::npos) {
        return do_cmp([](const DialogValue& a, const DialogValue& b) {
            return a.as_number() < b.as_number();
        }, "<");
    }

    auto bin_at = [&](char op) -> std::size_t {
        int depth = 0;
        bool in_str = false;
        for (std::size_t i = 0; i < expr.size(); ++i) {
            if (expr[i] == '"') in_str = !in_str;
            if (in_str) continue;
            if (expr[i] == '(') ++depth;
            else if (expr[i] == ')') --depth;
            if (depth == 0 && expr[i] == op && i > 0) return i;
        }
        return std::string_view::npos;
    };

    if (auto at = bin_at('+'); at != std::string_view::npos) {
        return DialogValue::from_number(
            eval_expr(expr.substr(0, at)).as_number() +
            eval_expr(expr.substr(at + 1)).as_number());
    }
    if (auto at = bin_at('-'); at != std::string_view::npos) {
        return DialogValue::from_number(
            eval_expr(expr.substr(0, at)).as_number() -
            eval_expr(expr.substr(at + 1)).as_number());
    }
    if (auto at = bin_at('*'); at != std::string_view::npos) {
        return DialogValue::from_number(
            eval_expr(expr.substr(0, at)).as_number() *
            eval_expr(expr.substr(at + 1)).as_number());
    }
    if (auto at = bin_at('/'); at != std::string_view::npos) {
        double d = eval_expr(expr.substr(at + 1)).as_number();
        return DialogValue::from_number(
            d == 0 ? 0 : eval_expr(expr.substr(0, at)).as_number() / d);
    }

    if (expr.front() == '(' && expr.back() == ')') {
        return eval_expr(expr.substr(1, expr.size() - 2));
    }
    if (expr.front() == '"') {
        return DialogValue::from_string(unquote(expr));
    }
    if (looks_number(expr)) {
        try { return DialogValue::from_number(std::stod(std::string(expr))); }
        catch (...) { return DialogValue::from_number(0); }
    }

    auto ident = first_ident(expr);
    auto rest = trim_sv(expr.substr(ident.size()));
    if (ident == "true" && rest.empty()) return DialogValue::from_number(1);
    if (ident == "false" && rest.empty()) return DialogValue::from_number(0);
    auto args = split_args(rest);
    return call_func(ident, args, /*as_statement=*/false);
}

DialogValue DialogVM::call_func(std::string_view name,
                                const std::vector<std::string>& args,
                                bool as_statement) {
    auto id_of = [&](std::size_t i) -> std::string {
        if (i >= args.size()) return {};
        return unquote(args[i]);
    };

    if (name == "print" || name == "say") {
        if (args.empty()) return DialogValue::from_string({});
        return eval_expr(args[0]);
    }
    if (name == "item") {
        const std::string id = id_of(0);
        if (args.size() >= 2) {
            int val = static_cast<int>(eval_expr(args[1]).as_number());
            if (val < 0) val = 0;
            if (world_.set_item) world_.set_item(id, val);
            return DialogValue::from_number(val);
        }
        int count = world_.item_count ? world_.item_count(id) : 0;
        return DialogValue::from_number(count);
    }
    if (name == "property") {
        std::vector<std::string> filtered;
        filtered.reserve(args.size());
        for (const auto& a : args) {
            if (a != "=") filtered.push_back(a);
        }
        const std::string pname = filtered.empty() ? std::string{} : unquote(filtered[0]);
        if (filtered.size() >= 2) {
            auto val = eval_expr(filtered[1]);
            if (world_.set_property) world_.set_property(pname, val);
            return val;
        }
        if (world_.get_property) return world_.get_property(pname);
        return DialogValue::from_number(0);
    }
    if (name == "ava") {
        if (world_.set_avatar) world_.set_avatar(id_of(0));
        return DialogValue::from_string(id_of(0));
    }
    if (name == "pal") {
        if (world_.set_palette) world_.set_palette(id_of(0));
        return DialogValue::from_string(id_of(0));
    }
    if (name == "tune") {
        if (world_.set_tune) world_.set_tune(id_of(0));
        return DialogValue::from_string(id_of(0));
    }
    if (name == "blip") {
        if (world_.play_blip) world_.play_blip(id_of(0));
        return DialogValue::from_string(id_of(0));
    }
    if (name == "exit") {
        std::string room = id_of(0);
        int x = args.size() > 1 ? static_cast<int>(eval_expr(args[1]).as_number()) : 0;
        int y = args.size() > 2 ? static_cast<int>(eval_expr(args[2]).as_number()) : 0;
        std::string fx = args.size() > 3 ? id_of(3) : std::string{};
        if (world_.do_exit) world_.do_exit(room, x, y, fx);
        return DialogValue::from_number(1);
    }
    if (name == "end") {
        if (world_.do_end) world_.do_end();
        return DialogValue::from_number(1);
    }
    if (name == "lock") {
        if (world_.do_lock) world_.do_lock();
        return DialogValue::from_number(1);
    }
    if (name == "printSprite" || name == "printTile" || name == "printItem" ||
        name == "drws" || name == "drwt" || name == "drwi") {
        const std::string id = id_of(0);
        int frame_i = world_.anim_frame ? world_.anim_frame() : 0;
        const bool spr = name == "printSprite" || name == "drws";
        const bool til = name == "printTile" || name == "drwt";
        const bool itm = name == "printItem" || name == "drwi";
        if (spr && world_.find_sprite) {
            if (const Sprite* s = world_.find_sprite(id); s && !s->frames.empty()) {
                emit_drawing(s->frames[static_cast<std::size_t>(frame_i) % s->frames.size()],
                             s->color_index);
            }
        } else if (til && world_.find_tile) {
            if (const Tile* t = world_.find_tile(id); t && !t->frames.empty()) {
                emit_drawing(t->frames[static_cast<std::size_t>(frame_i) % t->frames.size()],
                             t->color_index);
            }
        } else if (itm && world_.find_item) {
            if (const Item* it = world_.find_item(id); it && !it->frames.empty()) {
                emit_drawing(it->frames[static_cast<std::size_t>(frame_i) % it->frames.size()],
                             it->color_index);
            }
        }
        return DialogValue::from_string({});
    }

    (void)as_statement;
    if (world_.get_var) return world_.get_var(name);
    return DialogValue::from_string({});
}

} // namespace citsy
