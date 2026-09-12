#pragma once

#include <cstdint>
#include <span>

namespace citsy {

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

inline constexpr int kTileSize  = 8;    ///< Pixels per tile edge
inline constexpr int kMapSize   = 16;   ///< Room width/height in tiles
inline constexpr int kVideoSize = 128;  ///< Main framebuffer edge in pixels

/// Textbox buffer indices. Hosts should read these from the palette passed to
/// present() — the engine installs true black / white / rainbow hues there
/// while a dialog is open (they are independent of the room palette).
inline constexpr std::uint8_t kTextboxRainbow0     = 224;
inline constexpr int          kTextboxRainbowCount = 16;
/// Bitsy `RainbowEffect`: `(time / 100) - char.col * 0.5`.
inline constexpr double       kTextboxRainbowTimeMs    = 70.0;
inline constexpr double       kTextboxRainbowColShift  = 0.5;
/// `{wvy}` sine phase rate: `sin(time_ms * speed + index * phase)`.
inline constexpr double       kTextboxWavySpeed        = 0.005;
/// Bitsy `DialogBuffer.nextCharMaxTime`: milliseconds per printable glyph.
inline constexpr double       kTextboxTypewriterMsPerChar = 50.0;
inline constexpr std::uint8_t kTextboxWhite       = 253;
inline constexpr std::uint8_t kTextboxBlack       = 254;

// ---------------------------------------------------------------------------
// Basic types
// ---------------------------------------------------------------------------

/// 24-bit RGB colour.
struct Color {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;

    [[nodiscard]] constexpr bool operator==(const Color&) const noexcept = default;
};

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

/// Logical buttons the host maps from keyboard / gamepad / touch.
enum class Button {
    Up,
    Down,
    Left,
    Right,
    Ok,
    Menu,
};

// ---------------------------------------------------------------------------
// Audio
// ---------------------------------------------------------------------------

/// Square-wave duty cycle.
enum class PulseWave {
    Eighth,   ///< 1/8 duty
    Quarter,  ///< 1/4 duty
    Half,     ///< 1/2 duty (default)
};

/// Parameters for one audio channel.  Engine writes; host plays.
struct SoundChannel {
    bool     active       = false;
    int      duration_ms  = 0;
    int      frequency_hz = 0;
    float    volume       = 0.0f;   ///< 0.0 – 1.0
    PulseWave pulse       = PulseWave::Half;
};

// ---------------------------------------------------------------------------
// Graphics
// ---------------------------------------------------------------------------

/// Which framebuffer mode the engine is currently writing.
enum class GraphicsMode {
    Video,  ///< kGfxVideo – per-pixel colour-index buffer (128×128)
    Map,    ///< kGfxMap   – tile-ID buffers (16×16 each)
};

/// Textbox pixel resolution.
enum class TextMode {
    HiRez,  ///< kTxtHirez – 2× pixel scale
    LoRez,  ///< kTxtLorez – 4× pixel scale
};

/// Non-owning view of the textbox pixel buffer passed to Host::present().
struct TextboxView {
    bool                        visible = false;
    int                         x       = 0;   ///< top-left in the 128×128 video (Bitsy units)
    int                         y       = 0;
    int                         width   = 0;
    int                         height  = 0;
    std::span<const std::uint8_t> pixels;  ///< colour indices; empty when !visible
};

} // namespace citsy
