#include "src/font/font.hpp"

#include <cmath>
#include <algorithm>

namespace citsy {
namespace {

struct PackedGlyph {
    char32_t cp;
    std::uint8_t rows[8];
};

// Bitsy ascii_small: 6×8, rows packed in the low 6 bits (MSB = left pixel).
constexpr PackedGlyph kAsciiSmall[] = {
    {0, {0, 0, 0, 0, 0, 0, 0, 0}},
    {32, {0, 0, 0, 0, 0, 0, 0, 0}},
    {33, {4, 14, 14, 4, 4, 0, 4, 0}},
    {34, {27, 27, 18, 0, 0, 0, 0, 0}},
    {35, {0, 10, 31, 10, 10, 31, 10, 0}},
    {36, {8, 14, 16, 12, 2, 28, 4, 0}},
    {37, {25, 25, 2, 4, 8, 19, 19, 0}},
    {38, {8, 20, 20, 8, 21, 18, 13, 0}},
    {39, {12, 12, 8, 0, 0, 0, 0, 0}},
    {40, {4, 8, 8, 8, 8, 8, 4, 0}},
    {41, {8, 4, 4, 4, 4, 4, 8, 0}},
    {42, {0, 10, 14, 31, 14, 10, 0, 0}},
    {43, {0, 4, 4, 31, 4, 4, 0, 0}},
    {44, {0, 0, 0, 0, 0, 12, 12, 8}},
    {45, {0, 0, 0, 31, 0, 0, 0, 0}},
    {46, {0, 0, 0, 0, 0, 12, 12, 0}},
    {47, {0, 1, 2, 4, 8, 16, 0, 0}},
    {48, {14, 17, 19, 21, 25, 17, 14, 0}},
    {49, {4, 12, 4, 4, 4, 4, 14, 0}},
    {50, {14, 17, 1, 6, 8, 16, 31, 0}},
    {51, {14, 17, 1, 14, 1, 17, 14, 0}},
    {52, {2, 6, 10, 18, 31, 2, 2, 0}},
    {53, {31, 16, 16, 30, 1, 17, 14, 0}},
    {54, {6, 8, 16, 30, 17, 17, 14, 0}},
    {55, {31, 1, 2, 4, 8, 8, 8, 0}},
    {56, {14, 17, 17, 14, 17, 17, 14, 0}},
    {57, {14, 17, 17, 15, 1, 2, 12, 0}},
    {58, {0, 0, 12, 12, 0, 12, 12, 0}},
    {59, {0, 0, 12, 12, 0, 12, 12, 8}},
    {60, {2, 4, 8, 16, 8, 4, 2, 0}},
    {61, {0, 0, 31, 0, 0, 31, 0, 0}},
    {62, {8, 4, 2, 1, 2, 4, 8, 0}},
    {63, {14, 17, 1, 6, 4, 0, 4, 0}},
    {64, {14, 17, 23, 21, 23, 16, 14, 0}},
    {65, {14, 17, 17, 17, 31, 17, 17, 0}},
    {66, {30, 17, 17, 30, 17, 17, 30, 0}},
    {67, {14, 17, 16, 16, 16, 17, 14, 0}},
    {68, {30, 17, 17, 17, 17, 17, 30, 0}},
    {69, {31, 16, 16, 30, 16, 16, 31, 0}},
    {70, {31, 16, 16, 30, 16, 16, 16, 0}},
    {71, {14, 17, 16, 23, 17, 17, 15, 0}},
    {72, {17, 17, 17, 31, 17, 17, 17, 0}},
    {73, {14, 4, 4, 4, 4, 4, 14, 0}},
    {74, {1, 1, 1, 1, 17, 17, 14, 0}},
    {75, {17, 18, 20, 24, 20, 18, 17, 0}},
    {76, {16, 16, 16, 16, 16, 16, 31, 0}},
    {77, {17, 27, 21, 17, 17, 17, 17, 0}},
    {78, {17, 25, 21, 19, 17, 17, 17, 0}},
    {79, {14, 17, 17, 17, 17, 17, 14, 0}},
    {80, {30, 17, 17, 30, 16, 16, 16, 0}},
    {81, {14, 17, 17, 17, 21, 18, 13, 0}},
    {82, {30, 17, 17, 30, 18, 17, 17, 0}},
    {83, {14, 17, 16, 14, 1, 17, 14, 0}},
    {84, {31, 4, 4, 4, 4, 4, 4, 0}},
    {85, {17, 17, 17, 17, 17, 17, 14, 0}},
    {86, {17, 17, 17, 17, 17, 10, 4, 0}},
    {87, {17, 17, 21, 21, 21, 21, 10, 0}},
    {88, {17, 17, 10, 4, 10, 17, 17, 0}},
    {89, {17, 17, 17, 10, 4, 4, 4, 0}},
    {90, {30, 2, 4, 8, 16, 16, 30, 0}},
    {91, {14, 8, 8, 8, 8, 8, 14, 0}},
    {92, {0, 16, 8, 4, 2, 1, 0, 0}},
    {93, {14, 2, 2, 2, 2, 2, 14, 0}},
    {94, {4, 10, 17, 0, 0, 0, 0, 0}},
    {95, {0, 0, 0, 0, 0, 0, 0, 63}},
    {96, {12, 12, 4, 0, 0, 0, 0, 0}},
    {97, {0, 0, 14, 1, 15, 17, 15, 0}},
    {98, {16, 16, 30, 17, 17, 17, 30, 0}},
    {99, {0, 0, 14, 17, 16, 17, 14, 0}},
    {100, {1, 1, 15, 17, 17, 17, 15, 0}},
    {101, {0, 0, 14, 17, 30, 16, 14, 0}},
    {102, {6, 8, 8, 30, 8, 8, 8, 0}},
    {103, {0, 0, 15, 17, 17, 15, 1, 14}},
    {104, {16, 16, 28, 18, 18, 18, 18, 0}},
    {105, {4, 0, 4, 4, 4, 4, 6, 0}},
    {106, {2, 0, 6, 2, 2, 2, 18, 12}},
    {107, {16, 16, 18, 20, 24, 20, 18, 0}},
    {108, {4, 4, 4, 4, 4, 4, 6, 0}},
    {109, {0, 0, 26, 21, 21, 17, 17, 0}},
    {110, {0, 0, 28, 18, 18, 18, 18, 0}},
    {111, {0, 0, 14, 17, 17, 17, 14, 0}},
    {112, {0, 0, 30, 17, 17, 17, 30, 16}},
    {113, {0, 0, 15, 17, 17, 17, 15, 1}},
    {114, {0, 0, 22, 9, 8, 8, 28, 0}},
    {115, {0, 0, 14, 16, 14, 1, 14, 0}},
    {116, {0, 8, 30, 8, 8, 10, 4, 0}},
    {117, {0, 0, 18, 18, 18, 22, 10, 0}},
    {118, {0, 0, 17, 17, 17, 10, 4, 0}},
    {119, {0, 0, 17, 17, 21, 31, 10, 0}},
    {120, {0, 0, 18, 18, 12, 18, 18, 0}},
    {121, {0, 0, 18, 18, 18, 14, 4, 24}},
    {122, {0, 0, 30, 2, 12, 16, 30, 0}},
    {123, {6, 8, 8, 24, 8, 8, 6, 0}},
    {124, {4, 4, 4, 4, 4, 4, 4, 4}},
    {125, {12, 2, 2, 3, 2, 2, 12, 0}},
    {126, {0, 0, 0, 10, 20, 0, 0, 0}},
    {160, {0, 0, 0, 0, 0, 0, 0, 0}},
    {8216, {12, 12, 8, 0, 0, 0, 0, 0}},
    {8217, {12, 12, 8, 0, 0, 0, 0, 0}},
    {8220, {27, 27, 18, 0, 0, 0, 0, 0}},
    {8221, {27, 27, 18, 0, 0, 0, 0, 0}},
};

FontGlyph packed_to_glyph(const PackedGlyph& p, int w, int h) {
    FontGlyph g;
    g.width = w;
    g.height = h;
    g.spacing = w;
    g.data.resize(static_cast<std::size_t>(w * h), 0);
    for (int y = 0; y < h; ++y) {
        const std::uint8_t row = p.rows[y];
        for (int x = 0; x < w; ++x) {
            const int bit = w - 1 - x;  // left pixel is high bit of the 6-bit field
            g.data[static_cast<std::size_t>(y * w + x)] =
                (row & (1u << bit)) ? 1u : 0u;
        }
    }
    return g;
}

void make_missing(BitsyFont& font) {
    font.missing.width = font.width;
    font.missing.height = font.height;
    font.missing.spacing = font.width;
    font.missing.data.assign(
        static_cast<std::size_t>(font.width * font.height), 0);
    for (int y = 0; y < font.height - 1; ++y) {
        for (int x = 0; x < font.width - 1; ++x) {
            font.missing.data[static_cast<std::size_t>(y * font.width + x)] = 1;
        }
    }
}

std::string_view trim_line(std::string_view s) {
    while (!s.empty() && (s.back() == '\r' || s.back() == ' ' || s.back() == '\t'))
        s.remove_suffix(1);
    return s;
}

int to_int(std::string_view s) {
    int n = 0;
    bool neg = false;
    if (!s.empty() && s.front() == '-') { neg = true; s.remove_prefix(1); }
    for (char c : s) {
        if (c < '0' || c > '9') break;
        n = n * 10 + (c - '0');
    }
    return neg ? -n : n;
}

} // namespace

char32_t next_codepoint(std::string_view& s) {
    if (s.empty()) return 0;
    const auto b0 = static_cast<unsigned char>(s[0]);
    if (b0 < 0x80) {
        s.remove_prefix(1);
        return b0;
    }
    if ((b0 & 0xE0) == 0xC0 && s.size() >= 2) {
        char32_t cp = ((b0 & 0x1F) << 6) |
                      (static_cast<unsigned char>(s[1]) & 0x3F);
        s.remove_prefix(2);
        return cp;
    }
    if ((b0 & 0xF0) == 0xE0 && s.size() >= 3) {
        char32_t cp = ((b0 & 0x0F) << 12) |
                      ((static_cast<unsigned char>(s[1]) & 0x3F) << 6) |
                      (static_cast<unsigned char>(s[2]) & 0x3F);
        s.remove_prefix(3);
        return cp;
    }
    if ((b0 & 0xF8) == 0xF0 && s.size() >= 4) {
        char32_t cp = ((b0 & 0x07) << 18) |
                      ((static_cast<unsigned char>(s[1]) & 0x3F) << 12) |
                      ((static_cast<unsigned char>(s[2]) & 0x3F) << 6) |
                      (static_cast<unsigned char>(s[3]) & 0x3F);
        s.remove_prefix(4);
        return cp;
    }
    s.remove_prefix(1);
    return b0;
}

BitsyFont default_font() {
    BitsyFont font;
    font.name = "ascii_small";
    font.width = 6;
    font.height = 8;
    for (const auto& p : kAsciiSmall) {
        font.glyphs[p.cp] = packed_to_glyph(p, 6, 8);
    }
    make_missing(font);
    return font;
}

BitsyFont parse_bitsyfont(std::string_view data) {
    BitsyFont font = default_font();
    if (data.empty()) return font;

    font.glyphs.clear();
    bool in_char = false;
    bool in_props = false;
    char32_t cur_cp = 0;
    int cur_row = 0;
    int char_h = 8;
    int char_w = 6;

    std::size_t line_start = 0;
    auto process = [&](std::string_view line) {
        line = trim_line(line);
        if (line.empty() && !in_char) return;
        if (!line.empty() && line[0] == '#') return;

        if (!in_char) {
            if (line.starts_with("FONT ")) {
                font.name = std::string(line.substr(5));
            } else if (line.starts_with("SIZE ")) {
                auto rest = line.substr(5);
                auto sp = rest.find(' ');
                if (sp != std::string_view::npos) {
                    font.width = to_int(rest.substr(0, sp));
                    font.height = to_int(rest.substr(sp + 1));
                }
            } else if (line.starts_with("CHAR ")) {
                in_char = true;
                in_props = true;
                cur_row = 0;
                cur_cp = static_cast<char32_t>(to_int(line.substr(5)));
                FontGlyph g;
                g.width = font.width;
                g.height = font.height;
                g.spacing = font.width;
                font.glyphs[cur_cp] = std::move(g);
                char_w = font.width;
                char_h = font.height;
            }
            return;
        }

        auto& g = font.glyphs[cur_cp];
        if (in_props) {
            if (line.starts_with("CHAR_SIZE ")) {
                auto rest = line.substr(10);
                auto sp = rest.find(' ');
                if (sp != std::string_view::npos) {
                    g.width = to_int(rest.substr(0, sp));
                    g.height = to_int(rest.substr(sp + 1));
                    g.spacing = g.width;
                    char_w = g.width;
                    char_h = g.height;
                }
                return;
            }
            if (line.starts_with("CHAR_OFFSET ")) {
                auto rest = line.substr(12);
                auto sp = rest.find(' ');
                if (sp != std::string_view::npos) {
                    g.offset_x = to_int(rest.substr(0, sp));
                    g.offset_y = to_int(rest.substr(sp + 1));
                }
                return;
            }
            if (line.starts_with("CHAR_SPACING ")) {
                g.spacing = to_int(line.substr(13));
                return;
            }
            in_props = false;
        }

        const int w = g.width;
        for (int x = 0; x < w; ++x) {
            std::uint8_t bit = 0;
            if (x < static_cast<int>(line.size()) && line[static_cast<std::size_t>(x)] == '1')
                bit = 1;
            g.data.push_back(bit);
        }
        ++cur_row;
        if (cur_row >= g.height) {
            in_char = false;
        }
        (void)char_h;
        (void)char_w;
    };

    while (line_start <= data.size()) {
        auto nl = data.find('\n', line_start);
        if (nl == std::string_view::npos) {
            process(data.substr(line_start));
            break;
        }
        process(data.substr(line_start, nl - line_start));
        line_start = nl + 1;
    }

    make_missing(font);
    return font;
}

std::string spans_to_plain(const std::vector<TextSpan>& spans) {
    std::string out;
    for (const auto& sp : spans) {
        if (sp.is_drawing) out.push_back(' ');
        else out += sp.text;
    }
    return out;
}

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kTwoPi = 2.0 * kPi;
constexpr double kTwoPiOver3 = 2.0 * kPi / 3.0;
constexpr double kFourPiOver3 = 4.0 * kPi / 3.0;

// Bitsy font.js: Math.floor(Math.sin(phase) * 127 + 128)
std::uint8_t rainbow_channel(double phase) {
    return static_cast<std::uint8_t>(std::floor(std::sin(phase) * 127.0 + 128.0));
}

Color rainbow_rgb(double phase) {
    return Color{
        rainbow_channel(phase),
        rainbow_channel(phase + kTwoPiOver3),
        rainbow_channel(phase + kFourPiOver3),
    };
}

double rainbow_step(int col, double time_ms) {
    return time_ms / kTextboxRainbowTimeMs
        - static_cast<double>(col) * kTextboxRainbowColShift;
}

struct Cursor {
    int cx = 0;
    int cy = 0;
    int line_h = 8;
    int page = 0;
    int col = 0;
};

int max_x_of(const TextboxLayout& layout) {
    return layout.width - layout.margin - 1;
}

int max_y_of(const TextboxLayout& layout) {
    return layout.height - layout.margin - 1;
}

/// Advance to the next line. Returns true if that overflowed onto a new page.
bool newline(Cursor& c, const TextboxLayout& layout, int font_h, bool paginate) {
    const int next = c.cy + c.line_h + 1;
    if (next + font_h > max_y_of(layout)) {
        if (paginate) {
            ++c.page;
            c.cx = layout.margin;
            c.cy = layout.margin;
            c.line_h = font_h;
            c.col = 0;
            return true;
        }
    }
    c.cx = layout.margin;
    c.cy = next;
    c.line_h = font_h;
    c.col = 0;
    return false;
}

bool is_wrap_space(char c) {
    return c == ' ' || c == '\t';
}

int measure_word(const BitsyFont& font, std::string_view s) {
    int w = 0;
    while (!s.empty() && s.front() != '\n' && !is_wrap_space(s.front())) {
        w += font.glyph(next_codepoint(s)).spacing;
    }
    return w;
}

/// Wrap to the next row (and the next screen if the box is full) when @p need
/// does not fit on this row. Does not wrap at the start of a row, so a single
/// oversized word is not pulled apart until glyphs are placed one by one.
void wrap_if_needed(Cursor& c, int need, const TextboxLayout& layout, int font_h,
                    bool paginate, bool* new_page = nullptr) {
    if (c.cx + need > max_x_of(layout) && c.cx > layout.margin) {
        const bool np = newline(c, layout, font_h, paginate);
        if (new_page) *new_page = np;
    }
}

void append_run(std::vector<TextSpan>& page, const TextSpan& proto, std::string ch) {
    if (ch.empty()) return;
    if (!page.empty() && !page.back().is_drawing &&
        page.back().effects == proto.effects && page.back().color == proto.color &&
        ch != "\n" && page.back().text != "\n" &&
        (page.back().text.empty() || page.back().text.back() != '\n')) {
        page.back().text += ch;
        return;
    }
    TextSpan sp;
    sp.text = std::move(ch);
    sp.effects = proto.effects;
    sp.color = proto.color;
    page.push_back(std::move(sp));
}

} // namespace

void install_textbox_colors(std::vector<Color>& pal) {
    if (pal.size() < 256) pal.resize(256, Color{0, 0, 0});
    pal[kTextboxBlack] = Color{0, 0, 0};
    pal[kTextboxWhite] = Color{255, 255, 255};
    for (int i = 0; i < kTextboxRainbowCount; ++i) {
        pal[static_cast<std::size_t>(kTextboxRainbow0 + i)] =
            rainbow_rgb(kTwoPi * static_cast<double>(i) / kTextboxRainbowCount);
    }
}

std::uint8_t rainbow_index(int col, double time_ms) {
    const double count = static_cast<double>(kTextboxRainbowCount);
    double step = rainbow_step(col, time_ms);
    step = step - count * std::floor(step / count);
    int idx = static_cast<int>(std::floor(step));
    if (idx < 0) idx = 0;
    if (idx >= kTextboxRainbowCount) idx = kTextboxRainbowCount - 1;
    return static_cast<std::uint8_t>(kTextboxRainbow0 + idx);
}

std::vector<std::vector<TextSpan>> paginate_spans(
    const BitsyFont& font,
    const std::vector<TextSpan>& spans,
    const TextboxLayout& layout)
{
    std::vector<std::vector<TextSpan>> pages;
    std::vector<TextSpan> cur;
    Cursor c;
    c.cx = layout.margin;
    c.cy = layout.margin;
    c.line_h = font.height;

    auto flush_page = [&] {
        if (!cur.empty()) pages.push_back(std::move(cur));
        cur = {};
    };

    for (const auto& sp : spans) {
        if (sp.is_drawing) {
            bool np = false;
            wrap_if_needed(c, kTileSize, layout, font.height, true, &np);
            if (np) flush_page();
            TextSpan d = sp;
            cur.push_back(std::move(d));
            c.cx += kTileSize + 1;
            c.line_h = std::max(c.line_h, kTileSize);
            continue;
        }

        std::string_view rest = sp.text;
        while (!rest.empty()) {
            if (rest.front() == '\n') {
                rest.remove_prefix(1);
                const bool np = newline(c, layout, font.height, true);
                if (np) flush_page();
                else append_run(cur, sp, "\n");
                continue;
            }
            if (is_wrap_space(rest.front())) {
                const std::string_view before = rest;
                const char32_t cp = next_codepoint(rest);
                const FontGlyph& g = font.glyph(cp);
                if (c.cx + g.spacing > max_x_of(layout) && c.cx > layout.margin) {
                    continue;
                }
                append_run(cur, sp, std::string(before.data(), before.size() - rest.size()));
                c.cx += g.spacing;
                c.line_h = std::max(c.line_h, g.height);
                continue;
            }
            bool np = false;
            wrap_if_needed(c, measure_word(font, rest), layout, font.height, true, &np);
            if (np) flush_page();
            while (!rest.empty() && rest.front() != '\n' && !is_wrap_space(rest.front())) {
                const std::string_view before = rest;
                const char32_t cp = next_codepoint(rest);
                const FontGlyph& g = font.glyph(cp);
                np = false;
                wrap_if_needed(c, g.spacing, layout, font.height, true, &np);
                if (np) flush_page();
                append_run(cur, sp, std::string(before.data(), before.size() - rest.size()));
                c.cx += g.spacing;
                c.line_h = std::max(c.line_h, g.height);
            }
        }
    }
    flush_page();
    return pages;
}

std::vector<std::uint8_t> render_textbox(
    const BitsyFont& font,
    const std::vector<TextSpan>& spans,
    const TextboxLayout& layout)
{
    const int w = layout.width;
    const int h = layout.height;
    std::vector<std::uint8_t> buf(
        static_cast<std::size_t>(std::max(0, w * h)), kTextboxBlack);
    if (w < 4 || h < 4) return buf;

    auto put = [&](int x, int y, std::uint8_t col) {
        if (x < 0 || y < 0 || x >= w || y >= h) return;
        buf[static_cast<std::size_t>(y * w + x)] = col;
    };

    struct Placed {
        int x, y, gw, gh;
        const std::uint8_t* data;
        int color;
        std::uint8_t effects;
        int index;
        int col;
        int offx, offy;
    };
    std::vector<Placed> placed;
    Cursor c;
    c.cx = layout.margin;
    c.cy = layout.margin;
    c.line_h = font.height;
    int index = 0;

    for (const auto& sp : spans) {
        if (sp.is_drawing) {
            wrap_if_needed(c, kTileSize, layout, font.height, false);
            Placed p;
            p.x = c.cx;
            p.y = c.cy;
            p.gw = kTileSize;
            p.gh = kTileSize;
            p.data = sp.drawing.data();
            p.color = sp.drawing_color;
            p.effects = GlyphFx::None;
            p.index = index++;
            p.col = c.col++;
            p.offx = 0;
            p.offy = 0;
            placed.push_back(p);
            c.cx += kTileSize + 1;
            c.line_h = std::max(c.line_h, kTileSize);
            continue;
        }

        std::string_view rest = sp.text;
        while (!rest.empty()) {
            if (rest.front() == '\n') {
                rest.remove_prefix(1);
                newline(c, layout, font.height, false);
                continue;
            }
            if (is_wrap_space(rest.front())) {
                const char32_t cp = next_codepoint(rest);
                const FontGlyph& g = font.glyph(cp);
                if (c.cx + g.spacing > max_x_of(layout) && c.cx > layout.margin) {
                    continue;
                }
                Placed p;
                p.x = c.cx;
                p.y = c.cy;
                p.gw = g.width;
                p.gh = g.height;
                p.data = g.data.data();
                p.color = sp.color;
                p.effects = sp.effects;
                p.index = index++;
                p.col = c.col++;
                p.offx = g.offset_x;
                p.offy = g.offset_y;
                placed.push_back(p);
                c.cx += g.spacing;
                c.line_h = std::max(c.line_h, g.height);
                continue;
            }
            wrap_if_needed(c, measure_word(font, rest), layout, font.height, false);
            while (!rest.empty() && rest.front() != '\n' && !is_wrap_space(rest.front())) {
                const char32_t cp = next_codepoint(rest);
                const FontGlyph& g = font.glyph(cp);
                wrap_if_needed(c, g.spacing, layout, font.height, false);
                Placed p;
                p.x = c.cx;
                p.y = c.cy;
                p.gw = g.width;
                p.gh = g.height;
                p.data = g.data.data();
                p.color = sp.color;
                p.effects = sp.effects;
                p.index = index++;
                p.col = c.col++;
                p.offx = g.offset_x;
                p.offy = g.offset_y;
                placed.push_back(p);
                c.cx += g.spacing;
                c.line_h = std::max(c.line_h, g.height);
            }
        }
    }

    if (layout.rtl) {
        int line_start = 0;
        while (line_start < static_cast<int>(placed.size())) {
            const int ly = placed[static_cast<std::size_t>(line_start)].y;
            int line_end = line_start + 1;
            while (line_end < static_cast<int>(placed.size()) &&
                   placed[static_cast<std::size_t>(line_end)].y == ly) {
                ++line_end;
            }
            int min_x = w, max_r = 0;
            for (int i = line_start; i < line_end; ++i) {
                min_x = std::min(min_x, placed[static_cast<std::size_t>(i)].x);
                max_r = std::max(max_r,
                    placed[static_cast<std::size_t>(i)].x +
                    placed[static_cast<std::size_t>(i)].gw);
            }
            const int span = max_r - min_x;
            const int origin = w - layout.margin - span;
            for (int i = line_start; i < line_end; ++i) {
                auto& p = placed[static_cast<std::size_t>(i)];
                p.x = origin + (p.x - min_x);
            }
            line_start = line_end;
        }
    }

    const double t = layout.time_ms;
    for (const auto& p : placed) {
        int dx = 0, dy = 0;
        std::uint8_t color = p.color < 0
            ? kTextboxWhite
            : static_cast<std::uint8_t>(p.color);
        if (p.effects & GlyphFx::Wavy) {
            dy += static_cast<int>(std::sin((t * kTextboxWavySpeed) + p.index * 0.7) * 2.0);
        }
        if (p.effects & GlyphFx::Shaky) {
            const int hsh = static_cast<int>(t / 40.0) + p.index * 13;
            dx += (hsh % 3) - 1;
            dy += ((hsh / 3) % 3) - 1;
        }
        if (p.effects & GlyphFx::Rainbow) {
            color = rainbow_index(p.col, t);
        }
        if (!p.data) continue;
        for (int yy = 0; yy < p.gh; ++yy) {
            for (int xx = 0; xx < p.gw; ++xx) {
                if (!p.data[static_cast<std::size_t>(yy * p.gw + xx)]) continue;
                put(p.x + p.offx + xx + dx, p.y + p.offy + yy + dy, color);
            }
        }
    }

    if (layout.show_arrow) {
        const int ax = w - 5;
        const int ay = h - 4;
        put(ax, ay, kTextboxWhite);
        put(ax - 1, ay - 1, kTextboxWhite);
        put(ax + 1, ay - 1, kTextboxWhite);
        put(ax, ay - 1, kTextboxWhite);
    }

    return buf;
}

} // namespace citsy
