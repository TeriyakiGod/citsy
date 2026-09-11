#pragma once

#include <citsy/types.hpp>

#include <span>
#include <string_view>

namespace citsy {

// ---------------------------------------------------------------------------
// Host interface
// ---------------------------------------------------------------------------

/// Abstract host backend.
///
/// A concrete backend (32blit player, MockHost, custom host, …) implements this interface
/// and is handed to Engine::update() each frame.
///
/// The engine calls the host for **time** and **input**; after simulation it
/// calls present() so the host can map memory blocks to pixels and audio.
///
/// The engine never calls platform APIs directly.
class Host {
public:
    virtual ~Host() = default;

    // -----------------------------------------------------------------------
    // Lifecycle
    // -----------------------------------------------------------------------

    /// Called once after Engine::start() before the first update.
    virtual void on_engine_ready() {}

    // -----------------------------------------------------------------------
    // Time & input  (host → engine)
    // -----------------------------------------------------------------------

    /// Elapsed time since the previous frame, in milliseconds.
    [[nodiscard]] virtual double delta_time_ms() const = 0;

    /// Returns true while the given logical button is held down.
    [[nodiscard]] virtual bool button(Button code) const = 0;

    // -----------------------------------------------------------------------
    // Diagnostics
    // -----------------------------------------------------------------------

    /// Optional logging sink.  Default implementation discards messages.
    virtual void log(std::string_view /*message*/) {}

    // -----------------------------------------------------------------------
    // Presentation  (engine → host)
    // -----------------------------------------------------------------------

    /// Called once per engine step after simulation has completed.
    ///
    /// @param gfx_mode   Whether to read @p video or @p map1 / @p map2.
    /// @param txt_mode   Pixel scale for the textbox.
    /// @param palette    Full RGB colour table for the current room.
    /// @param video      128×128 colour-index buffer (used in Video mode).
    /// @param map1       16×16 tile-ID buffer – background layer.
    /// @param map2       16×16 tile-ID buffer – sprite/item overlay.
    /// @param textbox    Current dialog textbox, may be invisible.
    /// @param sound1     First square-wave channel parameters.
    /// @param sound2     Second square-wave channel parameters.
    virtual void present(
        GraphicsMode                    gfx_mode,
        TextMode                        txt_mode,
        std::span<const Color>          palette,
        std::span<const std::uint8_t>   video,
        std::span<const std::uint8_t>   map1,
        std::span<const std::uint8_t>   map2,
        TextboxView                     textbox,
        SoundChannel                    sound1,
        SoundChannel                    sound2
    ) = 0;
};

} // namespace citsy
