#include "src/dialog/script.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <sstream>
#include <unordered_set>
#include <utility>

namespace citsy {
namespace {

// ===========================================================================
// Value helpers
// ===========================================================================

[[nodiscard]] bool is_ident_start(char c) {
    return std::isalpha(static_cast<unsigned char>(c)) || c == '_';
}

[[nodiscard]] bool is_ident_cont(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

[[nodiscard]] bool is_space(char c) {
    return c == ' ' || c == '\t';
}

[[nodiscard]] bool is_nl(char c) {
    return c == '\n' || c == '\r';
}

[[nodiscard]] bool is_ws(char c) {
    return is_space(c) || is_nl(c);
}

[[nodiscard]] bool parses_as_number(std::string_view s, double& out) {
    if (s.empty()) return false;
    const char* begin = s.data();
    const char* end = s.data() + s.size();
    auto [ptr, ec] = std::from_chars(begin, end, out);
    return ec == std::errc{} && ptr == end;
}

const std::unordered_set<std::string> kFunctions = {
    "print", "say",
    "br",
    "p", "pg", "pagebreak",
    "item",
    "end",
    "exit",
    "wvy", "wavy",
    "shk", "shake",
    "rbw", "rainbow",
    "clr", "clr1", "clr2", "clr3",
    "printSprite", "printTile", "printItem",
    "drw", "drwt", "drws", "drwi",
    "property",
};

[[nodiscard]] bool is_function_name(std::string_view n) {
    return kFunctions.count(std::string(n)) != 0;
}

[[nodiscard]] bool is_visual_noop(std::string_view n) {
    return n == "wvy" || n == "wavy" || n == "shk" || n == "shake" ||
           n == "rbw" || n == "rainbow" || n == "clr" || n == "clr1" ||
           n == "clr2" || n == "clr3" || n == "printSprite" ||
           n == "printTile" || n == "printItem" || n == "drw" ||
           n == "drwt" || n == "drws" || n == "drwi" || n == "property";
}

// ===========================================================================
// AST
// ===========================================================================

enum class NodeKind {
    Literal,
    Var,
    Call,
    Binary,
    Text,
    Block,
    List,
};

enum class ListKind { Branch, Sequence, Cycle, Shuffle };

enum class TextOp { Str, Nested, Page, Br };

struct Node;

struct TextPart {
    TextOp                 op = TextOp::Str;
    std::string            text;
    std::unique_ptr<Node>  nested;
};

struct ListItem {
    std::unique_ptr<Node> cond;
    bool                  is_else = false;
    std::unique_ptr<Node> body;
};

struct Node {
    NodeKind kind = NodeKind::Block;

    Value       literal;
    std::string name;   // var, function, or operator
    std::vector<std::unique_ptr<Node>> kids;

    std::vector<TextPart> parts;  // Text
    std::vector<ListItem> items;  // List
    ListKind list_kind = ListKind::Branch;
    int      list_index = 0;
    std::vector<int> shuffle_bag;
};

[[nodiscard]] std::unique_ptr<Node> make_literal(Value v) {
    auto n = std::make_unique<Node>();
    n->kind = NodeKind::Literal;
    n->literal = std::move(v);
    return n;
}

[[nodiscard]] std::unique_ptr<Node> make_var(std::string name) {
    auto n = std::make_unique<Node>();
    n->kind = NodeKind::Var;
    n->name = std::move(name);
    return n;
}

[[nodiscard]] std::unique_ptr<Node> make_binary(std::string op,
                                                std::unique_ptr<Node> lhs,
                                                std::unique_ptr<Node> rhs) {
    auto n = std::make_unique<Node>();
    n->kind = NodeKind::Binary;
    n->name = std::move(op);
    n->kids.push_back(std::move(lhs));
    n->kids.push_back(std::move(rhs));
    return n;
}

// ===========================================================================
// Stream
// ===========================================================================

struct Stream {
    std::string_view s;
    std::size_t i = 0;

    [[nodiscard]] bool eof() const { return i >= s.size(); }
    [[nodiscard]] char peek() const { return eof() ? '\0' : s[i]; }
    [[nodiscard]] char peek_at(std::size_t k) const {
        return (i + k >= s.size()) ? '\0' : s[i + k];
    }

    char get() { return eof() ? '\0' : s[i++]; }

    void skip_ws() {
        while (!eof() && is_ws(peek())) ++i;
    }

    void skip_spaces() {
        while (!eof() && is_space(peek())) ++i;
    }

    void skip_newline() {
        if (peek() == '\r') get();
        if (peek() == '\n') get();
    }

    [[nodiscard]] bool at_list_bullet() const {
        if (peek() != '-') return false;
        char n = peek_at(1);
        if (n == '\0' || n == '}') return true;
        if (is_space(n) || is_nl(n)) return true;
        if (n == '{') return true;
        if (is_ident_start(n)) return true;
        return false;
    }

    [[nodiscard]] std::string_view rest() const { return s.substr(i); }
};

[[nodiscard]] int op_prec(std::string_view op) {
    if (op == "*" || op == "/") return 3;
    if (op == "+" || op == "-") return 2;
    if (op == "==" || op == "!=" || op == "<" || op == ">" ||
        op == "<=" || op == ">=") {
        return 1;
    }
    return 0;
}

[[nodiscard]] std::string peek_op(const Stream& st) {
    char c = st.peek();
    char n = st.peek_at(1);
    if (c == '=' && n == '=') return "==";
    if (c == '!' && n == '=') return "!=";
    if (c == '<' && n == '=') return "<=";
    if (c == '>' && n == '=') return ">=";
    if (c == '<' || c == '>' || c == '+' || c == '*' || c == '/' || c == '-') {
        return std::string(1, c);
    }
    return {};
}

void consume_op(Stream& st, std::string_view op) {
    for (std::size_t k = 0; k < op.size(); ++k) st.get();
}

// ===========================================================================
// Parser
// ===========================================================================

struct Parser {
    Stream st;

    explicit Parser(std::string_view src) : st{src, 0} {}

    std::unique_ptr<Node> parse_script() {
        auto block = parse_block(/*stop_at_brace=*/false, /*stop_at_bullet=*/false);
        return block;
    }

    // ---- block / text -----------------------------------------------------

    std::unique_ptr<Node> parse_block(bool stop_at_brace, bool stop_at_bullet) {
        auto block = std::make_unique<Node>();
        block->kind = NodeKind::Block;

        bool prev_quoted = false;
        while (!st.eof()) {
            st.skip_spaces();
            if (st.eof()) break;

            if (stop_at_brace && st.peek() == '}') break;
            if (stop_at_bullet && st.at_list_bullet()) break;

            if (is_nl(st.peek())) {
                // Blank line at block level → page break (Bitsy list pages).
                st.skip_newline();
                st.skip_spaces();
                if (!st.eof() && is_nl(st.peek())) {
                    st.skip_newline();
                    auto pb = std::make_unique<Node>();
                    pb->kind = NodeKind::Text;
                    pb->parts.push_back(TextPart{TextOp::Page, {}, {}});
                    block->kids.push_back(std::move(pb));
                    prev_quoted = false;
                }
                continue;
            }

            if (st.peek() == '}') break;

            if (st.peek() == '"') {
                if (prev_quoted) {
                    auto pb = std::make_unique<Node>();
                    pb->kind = NodeKind::Text;
                    pb->parts.push_back(TextPart{TextOp::Page, {}, {}});
                    block->kids.push_back(std::move(pb));
                }
                block->kids.push_back(parse_quoted_text());
                prev_quoted = true;
                continue;
            }

            if (st.peek() == '{') {
                block->kids.push_back(parse_code());
                prev_quoted = false;
                continue;
            }

            if (stop_at_bullet && st.at_list_bullet()) break;

            block->kids.push_back(parse_unquoted_text(stop_at_brace, stop_at_bullet));
            prev_quoted = false;
        }
        return block;
    }

    std::unique_ptr<Node> parse_quoted_text() {
        const bool triple = st.peek_at(0) == '"' && st.peek_at(1) == '"' &&
                            st.peek_at(2) == '"';
        if (triple) {
            st.get(); st.get(); st.get();
            if (is_nl(st.peek())) st.skip_newline();
        } else {
            st.get();  // opening "
        }

        auto node = std::make_unique<Node>();
        node->kind = NodeKind::Text;
        parse_text_contents(*node, /*triple=*/triple,
                            /*unquoted=*/false,
                            /*stop_at_brace=*/false,
                            /*stop_at_bullet=*/false);
        if (triple) {
            if (st.peek_at(0) == '"' && st.peek_at(1) == '"' &&
                st.peek_at(2) == '"') {
                st.get(); st.get(); st.get();
            }
        } else if (st.peek() == '"') {
            st.get();
        }
        return node;
    }

    std::unique_ptr<Node> parse_unquoted_text(bool stop_at_brace,
                                              bool stop_at_bullet) {
        auto node = std::make_unique<Node>();
        node->kind = NodeKind::Text;
        parse_text_contents(*node, /*triple=*/false,
                            /*unquoted=*/true,
                            stop_at_brace, stop_at_bullet);
        return node;
    }

    void parse_text_contents(Node& node, bool triple, bool unquoted,
                             bool stop_at_brace, bool stop_at_bullet) {
        std::string acc;
        auto flush = [&] {
            if (!acc.empty()) {
                node.parts.push_back(TextPart{TextOp::Str, std::move(acc), {}});
                acc.clear();
            }
        };

        while (!st.eof()) {
            if (!triple && !unquoted && st.peek() == '"') break;
            if (triple && st.peek_at(0) == '"' && st.peek_at(1) == '"' &&
                st.peek_at(2) == '"') {
                break;
            }
            if (unquoted && st.peek() == '"') break;
            if (unquoted && stop_at_brace && st.peek() == '}') break;
            if (unquoted && stop_at_bullet && st.at_list_bullet()) break;

            if (st.peek() == '{') {
                flush();
                TextPart p;
                p.op = TextOp::Nested;
                p.nested = parse_code();
                node.parts.push_back(std::move(p));
                continue;
            }

            if (is_nl(st.peek())) {
                const std::size_t before = st.i;
                st.skip_newline();
                st.skip_spaces();
                if (!st.eof() && is_nl(st.peek())) {
                    st.skip_newline();
                    flush();
                    node.parts.push_back(TextPart{TextOp::Page, {}, {}});
                    continue;
                }
                st.i = before;
                acc.push_back('\n');
                st.skip_newline();
                continue;
            }

            acc.push_back(st.get());
        }
        flush();
    }

    // ---- code / lists / expressions ---------------------------------------

    std::unique_ptr<Node> parse_code() {
        if (st.peek() != '{') return make_literal(Value::null());
        st.get();  // {
        st.skip_ws();

        if (st.peek() == '}') {
            st.get();
            return make_literal(Value::null());
        }

        ListKind lk = ListKind::Branch;
        bool is_list = false;
        if (auto w = peek_word(); w == "sequence" || w == "cycle" ||
                                  w == "shuffle") {
            is_list = true;
            if (w == "sequence") lk = ListKind::Sequence;
            else if (w == "cycle") lk = ListKind::Cycle;
            else lk = ListKind::Shuffle;
            consume_word();
            st.skip_ws();
        } else if (st.at_list_bullet()) {
            is_list = true;
            lk = ListKind::Branch;
        }

        std::unique_ptr<Node> node;
        if (is_list) {
            node = parse_list(lk);
        } else {
            node = parse_expression();
        }
        st.skip_ws();
        if (st.peek() == '}') st.get();
        return node;
    }

    [[nodiscard]] std::string peek_word() const {
        std::size_t k = st.i;
        if (k >= st.s.size() || !is_ident_start(st.s[k])) return {};
        std::size_t b = k++;
        while (k < st.s.size() && is_ident_cont(st.s[k])) ++k;
        return std::string(st.s.substr(b, k - b));
    }

    std::string consume_word() {
        st.skip_ws();
        if (!is_ident_start(st.peek())) return {};
        std::string w;
        w.push_back(st.get());
        while (is_ident_cont(st.peek())) w.push_back(st.get());
        return w;
    }

    std::unique_ptr<Node> parse_list(ListKind kind) {
        auto node = std::make_unique<Node>();
        node->kind = NodeKind::List;
        node->list_kind = kind;

        st.skip_ws();
        while (!st.eof() && st.peek() != '}') {
            st.skip_ws();
            if (st.peek() == '}' || st.eof()) break;
            if (!st.at_list_bullet()) break;
            st.get();  // '-'
            node->items.push_back(parse_list_item());
        }
        return node;
    }

    [[nodiscard]] bool looks_like_condition() {
        const std::size_t saved = st.i;
        st.skip_ws();
        int depth = 0;
        bool in_str = false;
        bool prev_ident = false;
        bool two_idents = false;
        bool seen_q = false;

        while (!st.eof()) {
            char c = st.peek();
            if (!in_str && depth == 0 && is_nl(c)) break;
            if (!in_str && depth == 0 && c == '}') break;

            if (!in_str && c == '"') {
                in_str = true;
                prev_ident = false;
                st.get();
                if (st.peek_at(0) == '"' && st.peek_at(1) == '"') {
                    st.get(); st.get();
                    while (!st.eof() &&
                           !(st.peek() == '"' && st.peek_at(1) == '"' &&
                             st.peek_at(2) == '"')) {
                        st.get();
                    }
                    if (!st.eof()) { st.get(); st.get(); st.get(); }
                    in_str = false;
                } else {
                    while (!st.eof() && st.peek() != '"') st.get();
                    if (st.peek() == '"') st.get();
                    in_str = false;
                }
                continue;
            }

            if (!in_str && c == '{') {
                ++depth;
                prev_ident = false;
                st.get();
                continue;
            }
            if (!in_str && c == '}') {
                if (depth == 0) break;
                --depth;
                prev_ident = false;
                st.get();
                continue;
            }

            if (!in_str && depth == 0 && c == '?') {
                seen_q = true;
                break;
            }

            if (!in_str && depth == 0 && is_ident_start(c)) {
                if (prev_ident) two_idents = true;
                st.get();
                while (is_ident_cont(st.peek())) st.get();
                prev_ident = true;
                continue;
            }

            if (!in_str && depth == 0 && is_ws(c)) {
                st.get();
                continue;
            }

            prev_ident = false;
            st.get();
        }

        st.i = saved;
        return seen_q && !two_idents;
    }

    ListItem parse_list_item() {
        ListItem item;
        st.skip_ws();
        if (looks_like_condition()) {
            item.cond = parse_expression();
            st.skip_ws();
            if (st.peek() == '?') st.get();
            if (item.cond && item.cond->kind == NodeKind::Var &&
                item.cond->name == "else") {
                item.is_else = true;
            }
            st.skip_ws();
        }
        item.body = parse_block(/*stop_at_brace=*/true, /*stop_at_bullet=*/true);
        return item;
    }

    std::unique_ptr<Node> parse_expression() {
        st.skip_ws();

        if (is_ident_start(st.peek())) {
            const std::size_t saved = st.i;
            std::string name = consume_word();
            st.skip_ws();

            if (st.peek() == '=' && st.peek_at(1) != '=') {
                st.get();
                auto rhs = parse_expression();
                return make_binary("=", make_var(std::move(name)), std::move(rhs));
            }

            const bool known = is_function_name(name);
            const bool next_is_arg = next_is_argument();

            if (known || next_is_arg) {
                auto call = std::make_unique<Node>();
                call->kind = NodeKind::Call;
                call->name = std::move(name);
                while (next_is_argument()) {
                    call->kids.push_back(parse_unary());
                    st.skip_ws();
                }
                return parse_binop_rest(std::move(call), 1);
            }

            st.i = saved;
        }

        auto lhs = parse_unary();
        return parse_binop_rest(std::move(lhs), 1);
    }

    [[nodiscard]] bool next_is_argument() {
        st.skip_ws();
        if (st.eof() || st.peek() == '}' || st.peek() == '?') return false;
        if (!peek_op(st).empty()) return false;
        if (st.at_list_bullet()) return false;
        char c = st.peek();
        return c == '"' || c == '{' || c == '.' ||
               std::isdigit(static_cast<unsigned char>(c)) ||
               is_ident_start(c) ||
               (c == '-' && std::isdigit(static_cast<unsigned char>(st.peek_at(1))));
    }

    std::unique_ptr<Node> parse_binop_rest(std::unique_ptr<Node> lhs, int min_prec) {
        for (;;) {
            st.skip_ws();
            std::string op = peek_op(st);
            if (op.empty()) break;
            const int prec = op_prec(op);
            if (prec < min_prec) break;
            consume_op(st, op);
            auto rhs = parse_unary();
            for (;;) {
                st.skip_ws();
                std::string nop = peek_op(st);
                if (nop.empty() || op_prec(nop) <= prec) break;
                rhs = parse_binop_rest(std::move(rhs), prec + 1);
            }
            lhs = make_binary(std::move(op), std::move(lhs), std::move(rhs));
        }
        return lhs;
    }

    std::unique_ptr<Node> parse_unary() {
        st.skip_ws();
        if (st.peek() == '-' && !st.at_list_bullet()) {
            st.get();
            auto rhs = parse_unary();
            return make_binary("-", make_literal(Value::number(0)), std::move(rhs));
        }
        return parse_primary();
    }

    std::unique_ptr<Node> parse_primary() {
        st.skip_ws();
        if (st.peek() == '{') return parse_code();
        if (st.peek() == '"') return parse_string_literal();

        if (std::isdigit(static_cast<unsigned char>(st.peek())) ||
            (st.peek() == '.' &&
             std::isdigit(static_cast<unsigned char>(st.peek_at(1))))) {
            return parse_number();
        }

        if (is_ident_start(st.peek())) {
            std::string name = consume_word();
            if (name == "true") return make_literal(Value::number(1));
            if (name == "false") return make_literal(Value::number(0));
            return make_var(std::move(name));
        }

        return make_literal(Value::null());
    }

    std::unique_ptr<Node> parse_string_literal() {
        const bool triple = st.peek_at(0) == '"' && st.peek_at(1) == '"' &&
                            st.peek_at(2) == '"';
        std::string out;
        if (triple) {
            st.get(); st.get(); st.get();
            if (is_nl(st.peek())) st.skip_newline();
            while (!st.eof() &&
                   !(st.peek() == '"' && st.peek_at(1) == '"' &&
                     st.peek_at(2) == '"')) {
                out.push_back(st.get());
            }
            if (!st.eof()) { st.get(); st.get(); st.get(); }
            if (!out.empty() && out.back() == '\n') out.pop_back();
        } else {
            st.get();
            while (!st.eof() && st.peek() != '"') out.push_back(st.get());
            if (st.peek() == '"') st.get();
        }
        return make_literal(Value::string(std::move(out)));
    }

    std::unique_ptr<Node> parse_number() {
        const std::size_t b = st.i;
        if (st.peek() == '.') st.get();
        while (std::isdigit(static_cast<unsigned char>(st.peek()))) st.get();
        if (st.peek() == '.') {
            st.get();
            while (std::isdigit(static_cast<unsigned char>(st.peek()))) st.get();
        }
        auto tok = st.s.substr(b, st.i - b);
        double n = 0;
        if (!parses_as_number(tok, n)) n = 0;
        return make_literal(Value::number(n));
    }
};

// ===========================================================================
// Evaluator
// ===========================================================================

struct EvalCtx {
    DialogWorld& world;
    std::vector<std::string> pages;
    std::string current;
    bool end_game = false;
    std::optional<DialogExit> exit;

    void print(std::string_view s) { current.append(s); }

    void br() { current.push_back('\n'); }

    void page_break() {
        flush();
    }

    void flush() {
        auto is_ws_s = [](unsigned char c) {
            return c == ' ' || c == '\t' || c == '\n' || c == '\r';
        };
        std::string_view v = current;
        while (!v.empty() && is_ws_s(static_cast<unsigned char>(v.front())))
            v.remove_prefix(1);
        while (!v.empty() && is_ws_s(static_cast<unsigned char>(v.back())))
            v.remove_suffix(1);
        if (!v.empty()) pages.emplace_back(v);
        current.clear();
    }
};

Value eval_node(Node& n, EvalCtx& ctx);

void eval_text(Node& n, EvalCtx& ctx) {
    for (auto& p : n.parts) {
        switch (p.op) {
        case TextOp::Str:
            ctx.print(p.text);
            break;
        case TextOp::Page:
            ctx.page_break();
            break;
        case TextOp::Br:
            ctx.br();
            break;
        case TextOp::Nested:
            if (p.nested) {
                Value v = eval_node(*p.nested, ctx);
                if (!v.is_null()) ctx.print(v.as_string());
            }
            break;
        }
    }
}

void eval_block(Node& n, EvalCtx& ctx) {
    for (auto& kid : n.kids) {
        if (!kid) continue;
        if (kid->kind == NodeKind::Text) {
            eval_text(*kid, ctx);
        } else {
            (void)eval_node(*kid, ctx);
        }
    }
}

Value apply_binary(std::string_view op, const Value& lhs, const Value& rhs) {
    if (op == "=") return rhs;  // assignment handled by caller

    if (op == "+") {
        if (lhs.is_string() || rhs.is_string()) {
            return Value::string(lhs.as_string() + rhs.as_string());
        }
        return Value::number(lhs.as_number() + rhs.as_number());
    }
    if (op == "-") return Value::number(lhs.as_number() - rhs.as_number());
    if (op == "*") return Value::number(lhs.as_number() * rhs.as_number());
    if (op == "/") {
        const double d = rhs.as_number();
        if (d == 0.0) return Value::number(0);
        return Value::number(lhs.as_number() / d);
    }

    auto cmp = [&]() -> int {
        if (lhs.is_number() && rhs.is_number()) {
            const double a = lhs.as_number();
            const double b = rhs.as_number();
            if (a < b) return -1;
            if (a > b) return 1;
            return 0;
        }
        const std::string a = lhs.as_string();
        const std::string b = rhs.as_string();
        if (a < b) return -1;
        if (a > b) return 1;
        return 0;
    };

    const int c = cmp();
    bool ok = false;
    if (op == "==") ok = c == 0;
    else if (op == "!=") ok = c != 0;
    else if (op == "<")  ok = c < 0;
    else if (op == ">")  ok = c > 0;
    else if (op == "<=") ok = c <= 0;
    else if (op == ">=") ok = c >= 0;
    return Value::number(ok ? 1 : 0);
}

Value eval_call(Node& n, EvalCtx& ctx) {
    std::vector<Value> args;
    args.reserve(n.kids.size());
    for (auto& k : n.kids) {
        args.push_back(k ? eval_node(*k, ctx) : Value::null());
    }

    const std::string& fn = n.name;

    if (fn == "print" || fn == "say") {
        std::string out;
        for (const auto& a : args) out += a.as_string();
        ctx.print(out);
        return Value::null();
    }
    if (fn == "br") {
        ctx.br();
        return Value::null();
    }
    if (fn == "p" || fn == "pg" || fn == "pagebreak") {
        ctx.page_break();
        return Value::null();
    }
    if (fn == "item") {
        if (args.empty()) return Value::number(0);
        const std::string id = args[0].as_string();
        if (args.size() >= 2) {
            const int count = static_cast<int>(args[1].as_number());
            ctx.world.set_item(id, count);
            return Value::number(count);
        }
        return Value::number(ctx.world.get_item(id));
    }
    if (fn == "end") {
        ctx.end_game = true;
        return Value::null();
    }
    if (fn == "exit") {
        DialogExit ex;
        if (args.size() == 1) {
            // {exit "room,x,y"} or {exit "room,x,y,effect"}
            std::string spec = args[0].as_string();
            std::vector<std::string> parts;
            std::string cur;
            for (char c : spec) {
                if (c == ',') {
                    parts.push_back(std::move(cur));
                    cur.clear();
                } else {
                    cur.push_back(c);
                }
            }
            parts.push_back(std::move(cur));
            if (!parts.empty()) ex.room_id = ctx.world.resolve_room(parts[0]);
            if (parts.size() > 1) {
                double v = 0;
                if (parses_as_number(parts[1], v)) ex.x = static_cast<int>(v);
            }
            if (parts.size() > 2) {
                double v = 0;
                if (parses_as_number(parts[2], v)) ex.y = static_cast<int>(v);
            }
            if (parts.size() > 3) ex.effect = parts[3];
        } else if (args.size() >= 3) {
            ex.room_id = ctx.world.resolve_room(args[0].as_string());
            ex.x = static_cast<int>(args[1].as_number());
            ex.y = static_cast<int>(args[2].as_number());
            if (args.size() >= 4) ex.effect = args[3].as_string();
        }
        ctx.exit = std::move(ex);
        return Value::null();
    }
    if (is_visual_noop(fn)) return Value::null();

    // Unknown function: ignore.
    return Value::null();
}

void eval_list(Node& n, EvalCtx& ctx) {
    if (n.items.empty()) return;

    auto run_item = [&](ListItem& it) {
        if (it.body) eval_node(*it.body, ctx);
    };

    auto cond_ok = [&](ListItem& it) -> bool {
        if (it.is_else) return true;
        if (!it.cond) return true;
        return eval_node(*it.cond, ctx).is_truthy();
    };

    const int nitems = static_cast<int>(n.items.size());

    if (n.list_kind == ListKind::Branch) {
        for (auto& it : n.items) {
            if (cond_ok(it)) {
                run_item(it);
                return;
            }
        }
        return;
    }

    int idx = 0;
    if (n.list_kind == ListKind::Sequence) {
        idx = std::min(n.list_index, nitems - 1);
        n.list_index = std::min(n.list_index + 1, nitems - 1);
    } else if (n.list_kind == ListKind::Cycle) {
        idx = n.list_index % nitems;
        n.list_index = (n.list_index + 1) % nitems;
    } else {  // Shuffle
        if (n.shuffle_bag.empty()) {
            n.shuffle_bag.resize(static_cast<std::size_t>(nitems));
            for (int i = 0; i < nitems; ++i) n.shuffle_bag[static_cast<std::size_t>(i)] = i;
            for (int i = nitems - 1; i > 0; --i) {
                const int j = ctx.world.random_int(i + 1);
                std::swap(n.shuffle_bag[static_cast<std::size_t>(i)],
                          n.shuffle_bag[static_cast<std::size_t>(j)]);
            }
        }
        idx = n.shuffle_bag.back();
        n.shuffle_bag.pop_back();
    }

    if (idx >= 0 && idx < nitems) run_item(n.items[static_cast<std::size_t>(idx)]);
}

Value eval_node(Node& n, EvalCtx& ctx) {
    switch (n.kind) {
    case NodeKind::Literal:
        return n.literal;
    case NodeKind::Var:
        return ctx.world.get_var(n.name);
    case NodeKind::Call:
        return eval_call(n, ctx);
    case NodeKind::Binary: {
        if (n.kids.size() < 2) return Value::null();
        if (n.name == "=") {
            Value rhs = eval_node(*n.kids[1], ctx);
            if (n.kids[0]->kind == NodeKind::Var) {
                ctx.world.set_var(n.kids[0]->name, rhs);
            }
            return rhs;
        }
        Value lhs = eval_node(*n.kids[0], ctx);
        Value rhs = eval_node(*n.kids[1], ctx);
        return apply_binary(n.name, lhs, rhs);
    }
    case NodeKind::Text:
        eval_text(n, ctx);
        return Value::null();
    case NodeKind::Block:
        eval_block(n, ctx);
        return Value::null();
    case NodeKind::List:
        eval_list(n, ctx);
        return Value::null();
    }
    return Value::null();
}

} // namespace

// ===========================================================================
// Value
// ===========================================================================

Value Value::null() { return {}; }

Value Value::number(double n) {
    Value v;
    v.kind_ = Kind::Number;
    v.number_ = n;
    return v;
}

Value Value::string(std::string s) {
    Value v;
    v.kind_ = Kind::String;
    v.string_ = std::move(s);
    return v;
}

double Value::as_number() const {
    if (kind_ == Kind::Number) return number_;
    if (kind_ == Kind::Null) return 0;
    double n = 0;
    if (parses_as_number(string_, n)) return n;
    return 0;
}

std::string Value::as_string() const {
    if (kind_ == Kind::Null) return {};
    if (kind_ == Kind::String) return string_;
    if (std::isfinite(number_) && number_ == std::floor(number_) &&
        std::abs(number_) < 1e15) {
        return std::to_string(static_cast<long long>(number_));
    }
    std::ostringstream os;
    os << number_;
    return os.str();
}

bool Value::is_truthy() const {
    if (kind_ == Kind::Null) return false;
    if (kind_ == Kind::Number) return number_ != 0;
    if (string_.empty() || string_ == "0" || string_ == "false") return false;
    double n = 0;
    if (parses_as_number(string_, n)) return n != 0;
    return true;
}

// ===========================================================================
// Public API
// ===========================================================================

struct DialogScript::Impl {
    std::unique_ptr<Node> root;
};

DialogScript::DialogScript() : impl_(std::make_unique<Impl>()) {}
DialogScript::DialogScript(DialogScript&&) noexcept = default;
DialogScript& DialogScript::operator=(DialogScript&&) noexcept = default;
DialogScript::~DialogScript() = default;

DialogScript parse_dialog_script(std::string_view source) {
    Parser p{source};
    DialogScript script;
    script.impl_->root = p.parse_script();
    return script;
}

DialogResult run_dialog_script(DialogScript& script, DialogWorld& world) {
    DialogResult result;
    if (!script.impl_ || !script.impl_->root) return result;

    EvalCtx ctx{world, {}, {}, false, {}};
    eval_node(*script.impl_->root, ctx);
    ctx.flush();

    result.pages = std::move(ctx.pages);
    result.end_game = ctx.end_game;
    result.exit = std::move(ctx.exit);
    return result;
}

} // namespace citsy
