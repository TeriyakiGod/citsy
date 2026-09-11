#include "src/transition/transition.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace citsy {
namespace {

constexpr int kMinStepMs = 125;

int step_count(std::string_view fx) {
    if (fx == "fade_w" || fx == "fade_b") return 6;
    if (fx == "wave" || fx == "tunnel") return 12;
    if (fx.starts_with("slide_")) return 8;
    return 1;
}

Color lerp_color(Color a, Color b, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    auto ch = [&](std::uint8_t x, std::uint8_t y) {
        return static_cast<std::uint8_t>(x + (y - x) * t);
    };
    return {ch(a.r, b.r), ch(a.g, b.g), ch(a.b, b.b)};
}

std::uint8_t px(const VideoBuffer& img, int x, int y) {
    x = std::clamp(x, 0, kVideoSize - 1);
    y = std::clamp(y, 0, kVideoSize - 1);
    return img[static_cast<std::size_t>(y * kVideoSize + x)];
}

Color fade_target(std::string_view fx) {
    if (fx == "fade_w") return {255, 255, 255};
    return {0, 0, 0};
}

std::vector<Color> lerp_palettes(const std::vector<Color>& a,
                                 const std::vector<Color>& b,
                                 float t) {
    const std::size_t n = std::max(a.size(), b.size());
    std::vector<Color> out(n);
    for (std::size_t i = 0; i < n; ++i) {
        const Color ca = i < a.size() ? a[i] : (a.empty() ? Color{} : a.back());
        const Color cb = i < b.size() ? b[i] : (b.empty() ? Color{} : b.back());
        out[i] = lerp_color(ca, cb, t);
    }
    return out;
}

} // namespace

bool is_known_transition(std::string_view name) {
    return name == "fade_w" || name == "fade_b" || name == "wave" ||
           name == "tunnel" || name == "slide_u" || name == "slide_d" ||
           name == "slide_l" || name == "slide_r" || name == "none";
}

void Transition::begin(const TransitionFrame& start,
                       const TransitionFrame& end,
                       std::string effect,
                       std::function<void()> on_complete) {
    if (effect.empty() || effect == "none") {
        active_ = false;
        if (on_complete) on_complete();
        return;
    }
    start_ = start;
    end_ = end;
    effect_ = std::move(effect);
    on_complete_ = std::move(on_complete);
    time_ms_ = 0;
    step_ = 0;
    active_ = true;
}

bool Transition::update(double dt_ms, VideoBuffer& out, std::vector<Color>& palette) {
    if (!active_) return false;

    time_ms_ += dt_ms;
    const int max_step = step_count(effect_);
    if (time_ms_ >= kMinStepMs || step_ == 0) {
        if (time_ms_ >= kMinStepMs) {
            ++step_;
            time_ms_ = 0;
        }
        const float delta = (max_step <= 1)
            ? 1.0f
            : static_cast<float>(step_) / static_cast<float>(max_step);

        if (effect_ == "fade_w" || effect_ == "fade_b") {
            const Color tgt = fade_target(effect_);
            if (delta < 0.5f) {
                const float t = delta / 0.5f;
                palette.clear();
                for (Color c : start_.palette) palette.push_back(lerp_color(c, tgt, t));
                out = start_.pixels;
            } else {
                const float t = (delta - 0.5f) / 0.5f;
                palette.clear();
                for (Color c : end_.palette) palette.push_back(lerp_color(tgt, c, t));
                out = end_.pixels;
            }
        } else if (effect_ == "wave") {
            palette = delta < 0.5f ? start_.palette : end_.palette;
            const auto& img = delta < 0.5f ? start_.pixels : end_.pixels;
            const float wave = delta < 0.5f ? delta / 0.5f : 1.0f - ((delta - 0.5f) / 0.5f);
            const float size = 2.0f + 14.0f * wave;
            for (int y = 0; y < kVideoSize; ++y) {
                const float offset = static_cast<float>(y) + (wave * wave * 0.2f * kVideoSize);
                const int shift = static_cast<int>(std::sin(offset / 4.0f) * size);
                for (int x = 0; x < kVideoSize; ++x) {
                    int sx = x + shift;
                    if (sx < 0) sx += kVideoSize;
                    else if (sx >= kVideoSize) sx -= kVideoSize;
                    out[static_cast<std::size_t>(y * kVideoSize + x)] = px(img, sx, y);
                }
            }
        } else if (effect_ == "tunnel") {
            palette = delta < 0.5f ? start_.palette : end_.palette;
            const int pcx = (delta <= 0.5f)
                ? start_.player_x * kTileSize + kTileSize / 2
                : end_.player_x * kTileSize + kTileSize / 2;
            const int pcy = (delta <= 0.5f)
                ? start_.player_y * kTileSize + kTileSize / 2
                : end_.player_y * kTileSize + kTileSize / 2;
            for (int y = 0; y < kVideoSize; ++y) {
                for (int x = 0; x < kVideoSize; ++x) {
                    std::uint8_t c = 0;
                    const float dx = static_cast<float>(pcx - x);
                    const float dy = static_cast<float>(pcy - y);
                    const float dist = std::sqrt(dx * dx + dy * dy);
                    if (delta <= 0.4f) {
                        const float tun = 1.0f - (delta / 0.4f);
                        if (dist <= kVideoSize * tun)
                            c = px(start_.pixels, x, y);
                    } else if (delta <= 0.6f) {
                        c = 0;
                    } else {
                        const float tun = (delta - 0.6f) / 0.4f;
                        if (dist <= kVideoSize * tun)
                            c = px(end_.pixels, x, y);
                    }
                    out[static_cast<std::size_t>(y * kVideoSize + x)] = c;
                }
            }
        } else if (effect_.starts_with("slide_")) {
            palette = lerp_palettes(start_.palette, end_.palette, delta);
            const int off = static_cast<int>(kVideoSize * delta);
            for (int y = 0; y < kVideoSize; ++y) {
                for (int x = 0; x < kVideoSize; ++x) {
                    int sx = x, sy = y;
                    const VideoBuffer* img = &start_.pixels;
                    if (effect_ == "slide_u") {
                        sy = y - off;
                        if (sy < 0) { sy += kVideoSize; img = &end_.pixels; }
                    } else if (effect_ == "slide_d") {
                        sy = y + off;
                        if (sy >= kVideoSize) { sy -= kVideoSize; img = &end_.pixels; }
                    } else if (effect_ == "slide_l") {
                        sx = x - off;
                        if (sx < 0) { sx += kVideoSize; img = &end_.pixels; }
                    } else {  // slide_r
                        sx = x + off;
                        if (sx >= kVideoSize) { sx -= kVideoSize; img = &end_.pixels; }
                    }
                    out[static_cast<std::size_t>(y * kVideoSize + x)] = px(*img, sx, sy);
                }
            }
        } else {
            out = end_.pixels;
            palette = end_.palette;
        }
    }

    if (step_ >= max_step - 1 && max_step > 1) {
        active_ = false;
        out = end_.pixels;
        palette = end_.palette;
        auto cb = std::move(on_complete_);
        on_complete_ = {};
        if (cb) cb();
        return false;
    }
    if (max_step <= 1) {
        active_ = false;
        out = end_.pixels;
        palette = end_.palette;
        auto cb = std::move(on_complete_);
        on_complete_ = {};
        if (cb) cb();
        return false;
    }
    return true;
}

} // namespace citsy
