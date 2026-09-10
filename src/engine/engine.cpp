#include <citsy/engine.hpp>

#include "src/model/game.hpp"
#include "src/parser/parser.hpp"

#include <array>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace citsy {

// ===========================================================================
// Engine::Impl  — private implementation
// ===========================================================================

struct Engine::Impl {
    Game game;

    // Runtime state (expanded in Phase 1).
    bool running = false;

    // Framebuffer / audio state owned by the engine.
    std::array<std::uint8_t, kVideoSize * kVideoSize>     video{};
    std::array<std::uint8_t, kMapSize   * kMapSize>       map1{};
    std::array<std::uint8_t, kMapSize   * kMapSize>       map2{};
    std::vector<Color>                                     palette;
    SoundChannel                                           sound1;
    SoundChannel                                           sound2;

    explicit Impl(Game g) : game(std::move(g)) {
        // Seed the default palette from the first palette entry, if any.
        if (!game.palettes.empty()) {
            palette = game.palettes.begin()->second.colors;
        }
        // Ensure at least 3 entries (bg, tile, sprite).
        while (palette.size() < 3) palette.push_back({});
    }

    void do_present(Host& host) const {
        // Textbox: not yet active in Phase 0.
        TextboxView textbox{};

        host.present(
            GraphicsMode::Map,
            TextMode::HiRez,
            std::span<const Color>(palette),
            std::span<const std::uint8_t>(video),
            std::span<const std::uint8_t>(map1),
            std::span<const std::uint8_t>(map2),
            textbox,
            sound1,
            sound2
        );
    }
};

// ===========================================================================
// Engine  — public methods
// ===========================================================================

Engine::Engine(std::string_view bitsy_text)
    : impl_(std::make_unique<Impl>(citsy::parse(bitsy_text)))
{}

Engine Engine::from_file(const std::string& path) {
    std::ifstream file(path);
    if (!file)
        throw std::ios_base::failure("cannot open file: " + path);
    std::ostringstream buf;
    buf << file.rdbuf();
    return Engine(buf.str());
}

Engine::~Engine() = default;
Engine::Engine(Engine&&) noexcept = default;
Engine& Engine::operator=(Engine&&) noexcept = default;

void Engine::start(Host& host) {
    impl_->running = true;
    host.on_engine_ready();
}

void Engine::update(Host& host) {
    if (!impl_->running) return;

    // Phase 0: no simulation yet — just present the (empty) buffers.
    // Phase 1 will fill map1/map2 from the current room and move the avatar.
    impl_->do_present(host);
}

bool Engine::is_running() const noexcept {
    return impl_ && impl_->running;
}

} // namespace citsy
