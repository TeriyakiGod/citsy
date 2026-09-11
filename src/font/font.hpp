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

enum class GlyphEffect {
    None,
    Wavy,
    Shaky,
    Rainbow,
};

struct TextSpan {
    std::string  text;           ///< UTF-8
    GlyphEffect  effect = GlyphEffect::None;
    int          color  = 2;     ///< palette index for ink (ignored by Rainbow)
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
[[nodiscard]] std::vector<std::uint8_t> render_textbox(
    const BitsyFont& font,
    const std::vector<TextSpan>& spans,
    const TextboxLayout& layout);

/// Concatenate span text (drawings become a space) for Engine::dialog_line().
[[nodiscard]] std::string spans_to_plain(const std::vector<TextSpan>& spans);

} // namespace citsy
