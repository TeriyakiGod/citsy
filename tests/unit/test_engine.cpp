#include <catch2/catch_test_macros.hpp>

#include <citsy/engine.hpp>
#include <citsy/types.hpp>
#include "backends/mock/mock_host.hpp"

// ---------------------------------------------------------------------------
// Inline minimal .bitsy for engine tests (no disk I/O)
// ---------------------------------------------------------------------------

static constexpr std::string_view kMinimalGame = R"(
# BITSY VERSION 8.12

! ROOM_FORMAT 1

PAL 0
NAME pal
0,82,204
128,159,255
255,255,255

ROOM 0
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
NAME empty room
PAL 0

SPR A
00011000
00011000
00111100
01111110
10111101
00111100
00011000
00011000
NAME Avatar
POS 0 4,4
)";

// ===========================================================================
// Engine construction
// ===========================================================================

TEST_CASE("engine: construct from string", "[engine]") {
    REQUIRE_NOTHROW(citsy::Engine(kMinimalGame));
}

TEST_CASE("engine: malformed palette throws ParseError", "[engine]") {
    // Color component out of range (300 > 255) must throw.
    CHECK_THROWS_AS(citsy::Engine("PAL 0\nNAME bad\n300,0,0\n"), citsy::ParseError);
}

TEST_CASE("engine: tile with no pixel data throws ParseError", "[engine]") {
    // TIL segment with an empty body must throw.
    CHECK_THROWS_AS(citsy::Engine("TIL a\n"), citsy::ParseError);
}

TEST_CASE("engine: tile row wrong length throws ParseError", "[engine]") {
    // 7-char pixel row instead of 8 must throw.
    constexpr std::string_view bad = "TIL a\n1111111\n10000001\n10000001\n10000001\n10000001\n10000001\n10000001\n11111111\n";
    CHECK_THROWS_AS(citsy::Engine(bad), citsy::ParseError);
}

TEST_CASE("engine: is_running false before start", "[engine]") {
    citsy::Engine engine(kMinimalGame);
    CHECK_FALSE(engine.is_running());
}

TEST_CASE("engine: is_running true after start", "[engine]") {
    citsy::Engine engine(kMinimalGame);
    citsy::MockHost host;
    engine.start(host);
    CHECK(engine.is_running());
}

// ===========================================================================
// MockHost interaction
// ===========================================================================

TEST_CASE("engine: start calls on_engine_ready", "[engine][mock]") {
    citsy::Engine engine(kMinimalGame);
    citsy::MockHost host;
    engine.start(host);
    CHECK(host.engine_ready_called);
}

TEST_CASE("engine: update calls present once", "[engine][mock]") {
    citsy::Engine engine(kMinimalGame);
    citsy::MockHost host;
    engine.start(host);
    engine.update(host);
    CHECK(host.snapshots.size() == 1);
}

TEST_CASE("engine: update multiple frames accumulates snapshots", "[engine][mock]") {
    citsy::Engine engine(kMinimalGame);
    citsy::MockHost host;
    engine.start(host);
    for (int i = 0; i < 5; ++i) engine.update(host);
    CHECK(host.snapshots.size() == 5);
}

TEST_CASE("engine: present receives correctly-sized buffers", "[engine][mock]") {
    citsy::Engine engine(kMinimalGame);
    citsy::MockHost host;
    engine.start(host);
    engine.update(host);

    const auto* snap = host.last_snapshot();
    REQUIRE(snap != nullptr);
    CHECK(snap->video.size() == citsy::kVideoSize * citsy::kVideoSize);
    CHECK(snap->map1.size() == citsy::kMapSize  * citsy::kMapSize);
    CHECK(snap->map2.size() == citsy::kMapSize  * citsy::kMapSize);
}

TEST_CASE("engine: present receives palette with at least 3 entries", "[engine][mock]") {
    citsy::Engine engine(kMinimalGame);
    citsy::MockHost host;
    engine.start(host);
    engine.update(host);

    const auto* snap = host.last_snapshot();
    REQUIRE(snap != nullptr);
    CHECK(snap->palette.size() >= 3);
}

TEST_CASE("engine: present palette matches parsed palette colors", "[engine][mock]") {
    citsy::Engine engine(kMinimalGame);
    citsy::MockHost host;
    engine.start(host);
    engine.update(host);

    const auto* snap = host.last_snapshot();
    REQUIRE(snap != nullptr);
    REQUIRE(snap->palette.size() >= 3);

    // PAL 0 in kMinimalGame: (0,82,204), (128,159,255), (255,255,255)
    CHECK(snap->palette[0].r ==   0); CHECK(snap->palette[0].g ==  82); CHECK(snap->palette[0].b == 204);
    CHECK(snap->palette[1].r == 128); CHECK(snap->palette[1].g == 159); CHECK(snap->palette[1].b == 255);
    CHECK(snap->palette[2].r == 255); CHECK(snap->palette[2].g == 255); CHECK(snap->palette[2].b == 255);
}

TEST_CASE("engine: present gfx_mode is Map in Phase 0", "[engine][mock]") {
    citsy::Engine engine(kMinimalGame);
    citsy::MockHost host;
    engine.start(host);
    engine.update(host);

    const auto* snap = host.last_snapshot();
    REQUIRE(snap != nullptr);
    CHECK(snap->gfx_mode == citsy::GraphicsMode::Map);
}

TEST_CASE("engine: sound channels inactive in Phase 0", "[engine][mock]") {
    citsy::Engine engine(kMinimalGame);
    citsy::MockHost host;
    engine.start(host);
    engine.update(host);

    const auto* snap = host.last_snapshot();
    REQUIRE(snap != nullptr);
    CHECK_FALSE(snap->sound1.active);
    CHECK_FALSE(snap->sound2.active);
}

TEST_CASE("engine: textbox hidden in Phase 0", "[engine][mock]") {
    citsy::Engine engine(kMinimalGame);
    citsy::MockHost host;
    engine.start(host);
    engine.update(host);

    const auto* snap = host.last_snapshot();
    REQUIRE(snap != nullptr);
    CHECK_FALSE(snap->textbox_visible);
}

TEST_CASE("engine: update before start is a no-op", "[engine][mock]") {
    citsy::Engine engine(kMinimalGame);
    citsy::MockHost host;
    engine.update(host);  // should not crash
    CHECK(host.snapshots.empty());
}

// ===========================================================================
// MockHost helpers
// ===========================================================================

TEST_CASE("mock_host: delta_time_ms default", "[mock]") {
    citsy::MockHost host;
    CHECK(host.delta_time_ms() == 16.667);
}

TEST_CASE("mock_host: button default false", "[mock]") {
    citsy::MockHost host;
    CHECK_FALSE(host.button(citsy::Button::Up));
    CHECK_FALSE(host.button(citsy::Button::Ok));
}

TEST_CASE("mock_host: set_button", "[mock]") {
    citsy::MockHost host;
    host.set_button(citsy::Button::Left, true);
    CHECK(host.button(citsy::Button::Left));
    CHECK_FALSE(host.button(citsy::Button::Right));
}

TEST_CASE("mock_host: log messages captured", "[mock]") {
    citsy::MockHost host;
    host.log("hello");
    host.log("world");
    REQUIRE(host.log_messages.size() == 2);
    CHECK(host.log_messages[0] == "hello");
    CHECK(host.log_messages[1] == "world");
}

TEST_CASE("mock_host: reset clears state", "[mock]") {
    citsy::MockHost host;
    host.log("msg");
    host.set_button(citsy::Button::Up, true);
    host.reset();
    CHECK(host.log_messages.empty());
    CHECK_FALSE(host.button(citsy::Button::Up));
    CHECK_FALSE(host.engine_ready_called);
}
