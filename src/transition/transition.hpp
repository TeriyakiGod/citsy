#pragma once

// Room-exit transition effects rendered into the 128×128 video buffer.

#include "src/model/game.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace citsy {

using VideoBuffer = std::array<std::uint8_t, kVideoSize * kVideoSize>;

struct TransitionFrame {
    VideoBuffer        pixels{};
    std::vector<Color> palette;
    int                player_x = 0;  ///< tile
    int                player_y = 0;
};

class Transition {
public:
    void begin(const TransitionFrame& start,
               const TransitionFrame& end,
               std::string effect,
               std::function<void()> on_complete);

    /// Advance by @p dt_ms.  Returns true while the effect is still running.
    bool update(double dt_ms, VideoBuffer& out, std::vector<Color>& palette);

    [[nodiscard]] bool active() const noexcept { return active_; }
    [[nodiscard]] const std::string& effect() const noexcept { return effect_; }

private:
    bool active_ = false;
    std::string effect_ = "none";
    double time_ms_ = 0;
    int step_ = 0;
    TransitionFrame start_;
    TransitionFrame end_;
    std::function<void()> on_complete_;
};

[[nodiscard]] bool is_known_transition(std::string_view name);

} // namespace citsy
