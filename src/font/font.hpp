#pragma once

// .bitsyfont loader and textbox glyph compositor (no GPU).

#include "src/model/game.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace citsy {

struct FontGlyph {
    int width   = 6;
    int height  = 8;
    int offset_x = 0;
    int offset_y = 0;
    int spacing = 6;
    std::vector<std::uint8_t> data;  ///< row-major bits; 1 = ink
};

class BitsyFont {
public:
    std::string name = "ascii_small";
    int width  = 6;
    int height = 8;

    std::unordered_map<char32_t, FontGlyph> glyphs;
    FontGlyph missing;

    [[nodiscard]] const FontGlyph& glyph(char32_t cp) const {
        auto it = glyphs.find(cp);
        if (it != glyphs.end()) return it->second;
        return missing;
    }

    [[nodiscard]] bool has(char32_t cp) const {
        return glyphs.find(cp) != glyphs.end();
    }
};

/// Built-in Bitsy ascii_small (6×8, ASCII + smart quotes).
[[nodiscard]] BitsyFont default_font();

/// Parse a `.bitsyfont` / FONT segment body.
[[nodiscard]] BitsyFont parse_bitsyfont(std::string_view data);

/// Decode the next UTF-8 code point from @p s, advancing the view.
[[nodiscard]] char32_t next_codepoint(std::string_view& s);

/// Combinable text effects (`{wvy}` `{shk}` `{rbw}`).
namespace GlyphFx {
inline constexpr std::uint8_t None    = 0;
inline constexpr std::uint8_t Wavy    = 1 << 0;
inline constexpr std::uint8_t Shaky   = 1 << 1;
inline constexpr std::uint8_t Rainbow = 1 << 2;
}

struct TextSpan {
    std::string  text;           ///< UTF-8
    std::uint8_t effects = GlyphFx::None;
    int          color   = -1;   ///< palette index; -1 = default white
    bool         is_drawing = false;
    TileFrame    drawing{};
    std::uint8_t drawing_color = 2;
};

struct TextboxLayout {
    int width  = 104;
    int height = 32;
    int margin = 2;
    bool rtl   = false;
    bool show_arrow = false;
    double time_ms = 0;          ///< for wavy / shaky animation
};

/// Render @p spans into a colour-index buffer of layout.width × layout.height.
/// Background is always @c kTextboxBlack. Default ink is @c kTextboxWhite.
[[nodiscard]] std::vector<std::uint8_t> render_textbox(
    const BitsyFont& font,
    const std::vector<TextSpan>& spans,
    const TextboxLayout& layout);

/// Split @p spans into screens that fit @p layout. Whole words wrap to the
/// next row; leftover rows become the next page. A word wider than the box is
/// the only case that may break across rows.
[[nodiscard]] std::vector<std::vector<TextSpan>> paginate_spans(
    const BitsyFont& font,
    const std::vector<TextSpan>& spans,
    const TextboxLayout& layout);

/// Install black, white, and rainbow hues at the reserved textbox indices.
void install_textbox_colors(std::vector<Color>& palette);

/// Rainbow palette index for a glyph at pixel @p x and time @p time_ms.
[[nodiscard]] std::uint8_t rainbow_index(int x, double time_ms);

/// Concatenate span text (drawings become a space) for Engine::dialog_line().
[[nodiscard]] std::string spans_to_plain(const std::vector<TextSpan>& spans);

} // namespace citsy
