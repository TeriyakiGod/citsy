#pragma once

// MockHost — test double for citsy::Host.
//
// Records every present() call so unit tests can assert on engine output
// without touching a display, audio device, or any platform library.

#include <citsy/host.hpp>

#include <optional>
#include <span>
#include <string>
#include <vector>

namespace citsy {

// ---------------------------------------------------------------------------
// Snapshot of one present() call
// ---------------------------------------------------------------------------

struct PresentSnapshot {
    GraphicsMode               gfx_mode;
    TextMode                   txt_mode;
    std::vector<Color>         palette;
    std::vector<std::uint8_t>  video;   ///< 128×128
    std::vector<std::uint8_t>  map1;    ///< 16×16
    std::vector<std::uint8_t>  map2;    ///< 16×16
    bool                       textbox_visible;
    int                        textbox_x;
    int                        textbox_y;
    int                        textbox_width;
    int                        textbox_height;
    std::vector<std::uint8_t>  textbox_pixels;
    SoundChannel               sound1;
    SoundChannel               sound2;
};

// ---------------------------------------------------------------------------
// MockHost
// ---------------------------------------------------------------------------

class MockHost final : public Host {
public:
    // -----------------------------------------------------------------------
    // Configuration
    // -----------------------------------------------------------------------

    double dt_ms = 16.667;      ///< reported delta_time_ms (≈60 fps)
    bool   buttons[6]{};        ///< indexed by static_cast<int>(Button::*)

    // -----------------------------------------------------------------------
    // Recorded state
    // -----------------------------------------------------------------------

    bool                       engine_ready_called = false;
    std::vector<std::string>   log_messages;
    std::vector<PresentSnapshot> snapshots;  ///< one per present() call

    // -----------------------------------------------------------------------
    // Host interface
    // -----------------------------------------------------------------------

    void on_engine_ready() override {
        engine_ready_called = true;
    }

    [[nodiscard]] double delta_time_ms() const override {
        return dt_ms;
    }

    [[nodiscard]] bool button(Button code) const override {
        return buttons[static_cast<int>(code)];
    }

    void log(std::string_view message) override {
        log_messages.emplace_back(message);
    }

    void present(
        GraphicsMode                  gfx_mode,
        TextMode                      txt_mode,
        std::span<const Color>        palette,
        std::span<const std::uint8_t> video,
        std::span<const std::uint8_t> map1,
        std::span<const std::uint8_t> map2,
        TextboxView                   textbox,
        SoundChannel                  sound1,
        SoundChannel                  sound2
    ) override {
        PresentSnapshot snap;
        snap.gfx_mode         = gfx_mode;
        snap.txt_mode         = txt_mode;
        snap.palette          = {palette.begin(), palette.end()};
        snap.video            = {video.begin(),   video.end()};
        snap.map1             = {map1.begin(),    map1.end()};
        snap.map2             = {map2.begin(),    map2.end()};
        snap.textbox_visible  = textbox.visible;
        snap.textbox_x        = textbox.x;
        snap.textbox_y        = textbox.y;
        snap.textbox_width    = textbox.width;
        snap.textbox_height   = textbox.height;
        snap.textbox_pixels   = {textbox.pixels.begin(), textbox.pixels.end()};
        snap.sound1           = sound1;
        snap.sound2           = sound2;
        snapshots.push_back(std::move(snap));
    }

    // -----------------------------------------------------------------------
    // Helpers
    // -----------------------------------------------------------------------

    /// Returns the most recent snapshot, or nullptr if none.
    [[nodiscard]] const PresentSnapshot* last_snapshot() const {
        if (snapshots.empty()) return nullptr;
        return &snapshots.back();
    }

    void set_button(Button b, bool down) {
        buttons[static_cast<int>(b)] = down;
    }

    void reset() {
        engine_ready_called = false;
        log_messages.clear();
        snapshots.clear();
        for (auto& b : buttons) b = false;
    }
};

} // namespace citsy
