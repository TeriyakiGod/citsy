#include <catch2/catch_test_macros.hpp>

#include <citsy/engine.hpp>
#include <citsy/types.hpp>
#include "backends/mock/mock_host.hpp"

#include <filesystem>
#include <fstream>
#include <functional>
#include <initializer_list>
#include <sstream>
#include <string>

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static std::string load_fixture(const char* name) {
    fs::path here = fs::path(__FILE__).parent_path();
    fs::path data = here.parent_path() / "data" / name;
    std::ifstream f(data);
    REQUIRE(f.is_open());
    std::ostringstream buf;
    buf << f.rdbuf();
    return buf.str();
}

static std::string empty_grid() {
    std::string s;
    for (int y = 0; y < 16; ++y) {
        s += "0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0\n";
    }
    return s;
}

static std::string grid_with(std::function<const char*(int x, int y)> cell) {
    std::string s;
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            if (x) s += ',';
            s += cell(x, y);
        }
        s += '\n';
    }
    return s;
}

static constexpr std::string_view kPal = R"(PAL 0
NAME pal
0,82,204
128,159,255
255,255,255
)";

static constexpr std::string_view kAvatarArt = R"(SPR A
00011000
00011000
00111100
01111110
10111101
00111100
00011000
00011000
NAME Avatar
)";

static constexpr std::string_view kWallTile = R"(TIL a
11111111
10000001
10000001
10000001
10000001
10000001
10000001
11111111
NAME wall
WAL true
)";

static constexpr std::string_view kNpcArt = R"(SPR 0
00111100
01000010
10100101
10000001
10100101
10011001
01000010
00111100
NAME npc
)";

static constexpr std::string_view kKeyArt = R"(ITM 0
00000000
00011000
00111100
01111110
01111110
00111100
00011000
00000000
NAME key
)";

// Fully opaque floor tile (not a wall) so sprite transparency is visible.
static constexpr std::string_view kSolidTile = R"(TIL s
11111111
11111111
11111111
11111111
11111111
11111111
11111111
11111111
NAME solid
)";

// Bitsy segments are blank-line delimited — join them so the parser
// does not swallow SPR/TIL/DLG into the previous block.
static std::string bitsy_game(std::initializer_list<std::string> segments) {
    std::string s = "# BITSY VERSION 8.12\n\n! ROOM_FORMAT 1\n";
    for (const auto& seg : segments) {
        s += "\n";
        s += seg;
        if (seg.empty() || seg.back() != '\n') s += '\n';
    }
    return s;
}

static std::string room0(const std::string& grid, const std::string& extras = {}) {
    return "ROOM 0\n" + grid + "PAL 0\n" + extras;
}

static std::string avatar_at(int x, int y) {
    return std::string(kAvatarArt) + "POS 0 " + std::to_string(x) + "," +
           std::to_string(y) + "\n";
}

static std::string open_room(int ax, int ay) {
    return bitsy_game({std::string(kPal), room0(empty_grid()), avatar_at(ax, ay)});
}

static void tap(citsy::Engine& engine, citsy::MockHost& host, citsy::Button b) {
    host.set_button(b, true);
    engine.update(host);
    host.set_button(b, false);
    engine.update(host);
}

static void press_ok(citsy::Engine& engine, citsy::MockHost& host) {
    const auto before = std::string(engine.dialog_line());
    tap(engine, host, citsy::Button::Ok);
    if (engine.dialog_active() && std::string(engine.dialog_line()) == before) {
        tap(engine, host, citsy::Button::Ok);
    }
}

static std::size_t map_i(int x, int y) {
    return static_cast<std::size_t>(y * citsy::kMapSize + x);
}

static std::size_t video_i(int px, int py) {
    return static_cast<std::size_t>(py * citsy::kVideoSize + px);
}

// ===========================================================================
// Avatar start position & movement
// ===========================================================================

TEST_CASE("sim: avatar starts at POS", "[engine][sim]") {
    auto src = open_room(4, 7);
    citsy::Engine engine(src);
    citsy::MockHost host;
    engine.start(host);
    CHECK(engine.current_room_id() == "0");
    CHECK(engine.avatar_x() == 4);
    CHECK(engine.avatar_y() == 7);
}

TEST_CASE("sim: tap right moves one tile", "[engine][sim]") {
    auto src = open_room(4, 4);
    citsy::Engine engine(src);
    citsy::MockHost host;
    engine.start(host);
    tap(engine, host, citsy::Button::Right);
    CHECK(engine.avatar_x() == 5);
    CHECK(engine.avatar_y() == 4);
}

TEST_CASE("sim: tap up/down/left", "[engine][sim]") {
    auto src = open_room(8, 8);
    citsy::Engine engine(src);
    citsy::MockHost host;
    engine.start(host);

    tap(engine, host, citsy::Button::Up);
    CHECK(engine.avatar_x() == 8);
    CHECK(engine.avatar_y() == 7);

    tap(engine, host, citsy::Button::Left);
    CHECK(engine.avatar_x() == 7);
    CHECK(engine.avatar_y() == 7);

    tap(engine, host, citsy::Button::Down);
    CHECK(engine.avatar_x() == 7);
    CHECK(engine.avatar_y() == 8);
}

TEST_CASE("sim: holding a direction does not move twice in one short frame", "[engine][sim]") {
    auto src = open_room(4, 4);
    citsy::Engine engine(src);
    citsy::MockHost host;
    engine.start(host);
    host.dt_ms = 16.667;
    host.set_button(citsy::Button::Right, true);
    engine.update(host);
    CHECK(engine.avatar_x() == 5);
    engine.update(host);  // still held, 500ms first-repeat delay
    CHECK(engine.avatar_x() == 5);
}

TEST_CASE("sim: hold-to-move repeats after delay", "[engine][sim]") {
    auto src = open_room(4, 4);
    citsy::Engine engine(src);
    citsy::MockHost host;
    engine.start(host);
    host.set_button(citsy::Button::Right, true);
    engine.update(host);
    CHECK(engine.avatar_x() == 5);

    host.dt_ms = 500;
    engine.update(host);
    CHECK(engine.avatar_x() == 6);
}

TEST_CASE("sim: wall tile blocks movement", "[engine][sim]") {
    auto src = bitsy_game({
        std::string(kPal),
        room0(grid_with([](int x, int y) { return (x == 5 && y == 4) ? "a" : "0"; })),
        std::string(kWallTile),
        avatar_at(4, 4),
    });
    citsy::Engine engine(src);
    citsy::MockHost host;
    engine.start(host);
    tap(engine, host, citsy::Button::Right);
    CHECK(engine.avatar_x() == 4);
    CHECK(engine.avatar_y() == 4);
}

TEST_CASE("sim: cannot walk off the map", "[engine][sim]") {
    auto src = open_room(0, 0);
    citsy::Engine engine(src);
    citsy::MockHost host;
    engine.start(host);
    tap(engine, host, citsy::Button::Left);
    tap(engine, host, citsy::Button::Up);
    CHECK(engine.avatar_x() == 0);
    CHECK(engine.avatar_y() == 0);
}

// ===========================================================================
// Room / sprite / item drawing
// ===========================================================================

TEST_CASE("sim: map1 encodes wall tile ids", "[engine][sim][render]") {
    auto src = bitsy_game({
        std::string(kPal),
        room0(grid_with([](int x, int y) { return (x == 0 || y == 0) ? "a" : "0"; })),
        std::string(kWallTile),
        avatar_at(4, 4),
    });
    citsy::Engine engine(src);
    citsy::MockHost host;
    engine.start(host);
    engine.update(host);

    const auto* snap = host.last_snapshot();
    REQUIRE(snap != nullptr);
    CHECK(snap->map1[map_i(0, 0)] == static_cast<std::uint8_t>('a'));
    CHECK(snap->map1[map_i(1, 1)] == 0);
}

TEST_CASE("sim: map2 has avatar overlay at POS", "[engine][sim][render]") {
    auto src = open_room(4, 4);
    citsy::Engine engine(src);
    citsy::MockHost host;
    engine.start(host);
    engine.update(host);

    const auto* snap = host.last_snapshot();
    REQUIRE(snap != nullptr);
    CHECK(snap->map2[map_i(4, 4)] == static_cast<std::uint8_t>('A'));
    CHECK(snap->map2[map_i(5, 4)] == 0);
}

TEST_CASE("sim: map2 follows the avatar after a move", "[engine][sim][render]") {
    auto src = open_room(4, 4);
    citsy::Engine engine(src);
    citsy::MockHost host;
    engine.start(host);
    tap(engine, host, citsy::Button::Right);

    const auto* snap = host.last_snapshot();
    REQUIRE(snap != nullptr);
    CHECK(snap->map2[map_i(4, 4)] == 0);
    CHECK(snap->map2[map_i(5, 4)] == static_cast<std::uint8_t>('A'));
}

TEST_CASE("sim: video draws avatar pixels with sprite colour", "[engine][sim][render]") {
    auto src = open_room(4, 4);
    citsy::Engine engine(src);
    citsy::MockHost host;
    engine.start(host);
    engine.update(host);

    const auto* snap = host.last_snapshot();
    REQUIRE(snap != nullptr);
    // Avatar frame row 0 is 00011000 — pixels (3,0) and (4,0) of the tile are on.
    CHECK(snap->video[video_i(4 * 8 + 3, 4 * 8 + 0)] == 2);
    CHECK(snap->video[video_i(4 * 8 + 0, 4 * 8 + 0)] == 0);
}

TEST_CASE("sim: wall tile pixels use tile colour", "[engine][sim][render]") {
    auto src = bitsy_game({
        std::string(kPal),
        room0(grid_with([](int x, int y) { return (x == 0 && y == 0) ? "a" : "0"; })),
        std::string(kWallTile),
        avatar_at(4, 4),
    });
    citsy::Engine engine(src);
    citsy::MockHost host;
    engine.start(host);
    engine.update(host);

    const auto* snap = host.last_snapshot();
    REQUIRE(snap != nullptr);
    CHECK(snap->video[video_i(0, 0)] == 1);  // wall border
    CHECK(snap->video[video_i(1, 1)] == 0);  // wall interior
}

TEST_CASE("sim: item is drawn on map2", "[engine][sim][render]") {
    auto src = bitsy_game({
        std::string(kPal),
        room0(empty_grid(), "ITM 0 6,4\n"),
        avatar_at(4, 4),
        std::string(kKeyArt),
    });
    citsy::Engine engine(src);
    citsy::MockHost host;
    engine.start(host);
    engine.update(host);

    const auto* snap = host.last_snapshot();
    REQUIRE(snap != nullptr);
    CHECK(snap->map2[map_i(6, 4)] != 0);
}

TEST_CASE("sim: npc sprite is drawn on map2", "[engine][sim][render]") {
    auto src = bitsy_game({
        std::string(kPal),
        room0(empty_grid()),
        avatar_at(4, 4),
        std::string(kNpcArt) + "POS 0 7,4\n",
    });
    citsy::Engine engine(src);
    citsy::MockHost host;
    engine.start(host);
    engine.update(host);

    const auto* snap = host.last_snapshot();
    REQUIRE(snap != nullptr);
    CHECK(snap->map2[map_i(7, 4)] != 0);
}

TEST_CASE("sim: tiles under sprites are not drawn", "[engine][sim][render][occlusion]") {
    auto src = bitsy_game({
        std::string(kPal),
        room0(grid_with([](int x, int y) {
            return (x == 4 && y == 4) || (x == 5 && y == 4) || (x == 7 && y == 4)
                       ? "s"
                       : "0";
        })),
        std::string(kSolidTile),
        avatar_at(4, 4),
        std::string(kNpcArt) + "POS 0 7,4\n",
    });
    citsy::Engine engine(src);
    citsy::MockHost host;
    engine.start(host);
    engine.update(host);

    const auto* snap = host.last_snapshot();
    REQUIRE(snap != nullptr);

    // Overlay order is unchanged: avatar and npc still occupy map2.
    CHECK(snap->map2[map_i(4, 4)] == static_cast<std::uint8_t>('A'));
    CHECK(snap->map2[map_i(7, 4)] != 0);

    // Neighbouring floor tile with no sprite stays on map1 / video.
    CHECK(snap->map1[map_i(5, 4)] == static_cast<std::uint8_t>('s'));
    CHECK(snap->video[video_i(5 * 8 + 0, 4 * 8 + 0)] == 1);

    // Avatar frame row 0 is 00011000 — (0,0) is transparent, (3,0) is ink.
    const auto avatar_gap = snap->video[video_i(4 * 8 + 0, 4 * 8 + 0)];
    const auto avatar_ink = snap->video[video_i(4 * 8 + 3, 4 * 8 + 0)];
    CHECK(avatar_ink == 2);

    // NPC frame row 0 is 00111100 — (0,0) is transparent, (2,0) is ink.
    const auto npc_gap = snap->video[video_i(7 * 8 + 0, 4 * 8 + 0)];
    const auto npc_ink = snap->video[video_i(7 * 8 + 2, 4 * 8 + 0)];
    CHECK(npc_ink == 2);

    if constexpr (citsy::kOccludeTilesUnderSprites) {
        CHECK(snap->map1[map_i(4, 4)] == 0);
        CHECK(snap->map1[map_i(7, 4)] == 0);
        CHECK(avatar_gap == 0);
        CHECK(npc_gap == 0);
    } else {
        CHECK(snap->map1[map_i(4, 4)] == static_cast<std::uint8_t>('s'));
        CHECK(snap->map1[map_i(7, 4)] == static_cast<std::uint8_t>('s'));
        CHECK(avatar_gap == 1);
        CHECK(npc_gap == 1);
    }
}

TEST_CASE("sim: items do not occlude tiles", "[engine][sim][render][occlusion]") {
    auto src = bitsy_game({
        std::string(kPal),
        room0(grid_with([](int x, int y) { return (x == 6 && y == 4) ? "s" : "0"; }),
              "ITM 0 6,4\n"),
        std::string(kSolidTile),
        avatar_at(4, 4),
        std::string(kKeyArt),
    });
    citsy::Engine engine(src);
    citsy::MockHost host;
    engine.start(host);
    engine.update(host);

    const auto* snap = host.last_snapshot();
    REQUIRE(snap != nullptr);
    CHECK(snap->map1[map_i(6, 4)] == static_cast<std::uint8_t>('s'));
    CHECK(snap->map2[map_i(6, 4)] != 0);
    // Key frame row 0 is empty; the solid tile should still show through.
    CHECK(snap->video[video_i(6 * 8 + 0, 4 * 8 + 0)] == 1);
}

// ===========================================================================
// Sprite dialog
// ===========================================================================

TEST_CASE("sim: walking into a sprite starts dialog and does not move", "[engine][sim][dialog]") {
    auto src = bitsy_game({
        std::string(kPal),
        room0(empty_grid()),
        avatar_at(4, 4),
        std::string(kNpcArt) + "POS 0 5,4\nDLG DLG_NPC\n",
        "DLG DLG_NPC\n\"Hello!\"\n\"Nice to meet you.\"\n",
    });
    citsy::Engine engine(src);
    citsy::MockHost host;
    engine.start(host);
    tap(engine, host, citsy::Button::Right);

    CHECK(engine.avatar_x() == 4);
    CHECK(engine.dialog_active());
    CHECK(engine.dialog_line() == "Hello!");

    const auto* snap = host.last_snapshot();
    REQUIRE(snap != nullptr);
    CHECK(snap->textbox_visible);
}

TEST_CASE("sim: cannot walk while dialog is open", "[engine][sim][dialog]") {
    auto src = bitsy_game({
        std::string(kPal),
        room0(empty_grid()),
        avatar_at(4, 4),
        std::string(kNpcArt) + "POS 0 5,4\nDLG DLG_NPC\n",
        "DLG DLG_NPC\n\"Hello!\"\n",
    });
    citsy::Engine engine(src);
    citsy::MockHost host;
    engine.start(host);
    tap(engine, host, citsy::Button::Right);
    REQUIRE(engine.dialog_active());

    tap(engine, host, citsy::Button::Left);
    CHECK(engine.avatar_x() == 4);  // still blocked; Left also advances dialog
}

TEST_CASE("sim: Ok advances then closes linear dialog", "[engine][sim][dialog]") {
    auto src = bitsy_game({
        std::string(kPal),
        room0(empty_grid()),
        avatar_at(4, 4),
        std::string(kNpcArt) + "POS 0 5,4\nDLG DLG_NPC\n",
        "DLG DLG_NPC\n\"Hello!\"\n\"Nice to meet you.\"\n",
    });
    citsy::Engine engine(src);
    citsy::MockHost host;
    engine.start(host);
    tap(engine, host, citsy::Button::Right);
    REQUIRE(engine.dialog_line() == "Hello!");

    press_ok(engine, host);
    CHECK(engine.dialog_active());
    CHECK(engine.dialog_line() == "Nice to meet you.");

    press_ok(engine, host);
    CHECK_FALSE(engine.dialog_active());
    CHECK(engine.dialog_line().empty());
}

TEST_CASE("sim: input ignored until buttons released after dialog", "[engine][sim][dialog]") {
    auto src = bitsy_game({
        std::string(kPal),
        room0(empty_grid()),
        avatar_at(4, 4),
        std::string(kNpcArt) + "POS 0 5,4\nDLG DLG_NPC\n",
        "DLG DLG_NPC\n\"Bye.\"\n",
    });
    citsy::Engine engine(src);
    citsy::MockHost host;
    engine.start(host);
    tap(engine, host, citsy::Button::Right);
    REQUIRE(engine.dialog_active());

    // Finish typing, then close by holding Right (any action button continues).
    const double saved = host.dt_ms;
    host.dt_ms = 10'000;
    engine.update(host);
    host.dt_ms = saved;
    host.set_button(citsy::Button::Right, true);
    engine.update(host);
    CHECK_FALSE(engine.dialog_active());
    CHECK(engine.avatar_x() == 4);  // must not insta-move into the sprite

    engine.update(host);
    CHECK(engine.avatar_x() == 4);

    host.set_button(citsy::Button::Right, false);
    engine.update(host);
    tap(engine, host, citsy::Button::Left);
    CHECK(engine.avatar_x() == 3);
}

// ===========================================================================
// Items
// ===========================================================================

TEST_CASE("sim: walking onto an item starts dialog and removes it", "[engine][sim][dialog]") {
    auto src = bitsy_game({
        std::string(kPal),
        room0(empty_grid(), "ITM 0 5,4\n"),
        avatar_at(4, 4),
        std::string(kKeyArt) + "DLG DLG_KEY\n",
        "DLG DLG_KEY\n\"You found a key.\"\n",
    });
    citsy::Engine engine(src);
    citsy::MockHost host;
    engine.start(host);

    engine.update(host);
    REQUIRE(host.last_snapshot()->map2[map_i(5, 4)] != 0);

    tap(engine, host, citsy::Button::Right);
    CHECK(engine.avatar_x() == 5);
    CHECK(engine.dialog_active());
    CHECK(engine.dialog_line() == "You found a key.");
    CHECK(engine.item_count("0") == 1);

    const auto* snap = host.last_snapshot();
    REQUIRE(snap != nullptr);
    CHECK(snap->map2[map_i(5, 4)] == static_cast<std::uint8_t>('A'));  // avatar only; item gone while dialog is open

    press_ok(engine, host);
    CHECK_FALSE(engine.dialog_active());
}

TEST_CASE("sim: item dialog sees the updated inventory", "[engine][sim][inventory][dialog]") {
    auto src = bitsy_game({
        std::string(kPal),
        room0(empty_grid(), "ITM 0 5,4\n"),
        avatar_at(4, 4),
        std::string(kKeyArt) + "DLG DLG_KEY\n",
        R"(DLG DLG_KEY
{
  - {item "0"} > 0 ?
    "Got it."
  - else ?
    "Empty hands."
}
)",
    });
    citsy::Engine engine(src);
    citsy::MockHost host;
    engine.start(host);

    tap(engine, host, citsy::Button::Right);
    CHECK(engine.dialog_active());
    CHECK(engine.dialog_line() == "Got it.");
    CHECK(engine.item_count("0") == 1);
    CHECK(host.last_snapshot()->map2[map_i(5, 4)] == static_cast<std::uint8_t>('A'));
}

TEST_CASE("sim: item without dialog is removed immediately", "[engine][sim]") {
    auto src = bitsy_game({
        std::string(kPal),
        room0(empty_grid(), "ITM 0 5,4\n"),
        avatar_at(4, 4),
        std::string(kKeyArt),
    });
    citsy::Engine engine(src);
    citsy::MockHost host;
    engine.start(host);
    tap(engine, host, citsy::Button::Right);
    CHECK(engine.avatar_x() == 5);
    CHECK_FALSE(engine.dialog_active());
    CHECK(host.last_snapshot()->map2[map_i(5, 4)] == static_cast<std::uint8_t>('A'));
}

// ===========================================================================
// Exits
// ===========================================================================

TEST_CASE("sim: walking onto an exit warps to the destination room", "[engine][sim]") {
    auto text = load_fixture("two_rooms.bitsy");
    citsy::Engine engine(text);
    citsy::MockHost host;
    engine.start(host);
    REQUIRE(engine.current_room_id() == "0");
    REQUIRE(engine.avatar_x() == 4);
    REQUIRE(engine.avatar_y() == 8);

    for (int i = 0; i < 11; ++i) tap(engine, host, citsy::Button::Right);

    CHECK(engine.current_room_id() == "1");
    CHECK(engine.avatar_x() == 0);
    CHECK(engine.avatar_y() == 8);
}

TEST_CASE("sim: room change swaps the palette", "[engine][sim]") {
    auto text = load_fixture("two_rooms.bitsy");
    citsy::Engine engine(text);
    citsy::MockHost host;
    engine.start(host);
    engine.update(host);
    const auto* before = host.last_snapshot();
    REQUIRE(before != nullptr);
    REQUIRE(before->palette.size() >= 1);
    CHECK(before->palette[0].r == 0);
    CHECK(before->palette[0].g == 20);
    CHECK(before->palette[0].b == 100);

    for (int i = 0; i < 11; ++i) tap(engine, host, citsy::Button::Right);

    const auto* after = host.last_snapshot();
    REQUIRE(after != nullptr);
    REQUIRE(after->palette.size() >= 1);
    CHECK(after->palette[0].r == 80);
    CHECK(after->palette[0].g == 0);
    CHECK(after->palette[0].b == 0);
}

TEST_CASE("sim: exit dialog plays before the warp", "[engine][sim][dialog]") {
    auto src = bitsy_game({
        std::string(kPal),
        room0(empty_grid(), "EXT 5,4 1 3,3 DLG DLG_EXT\n"),
        "ROOM 1\n" + empty_grid() + "PAL 0\n",
        avatar_at(4, 4),
        "DLG DLG_EXT\n\"Crossing over.\"\n",
    });
    citsy::Engine engine(src);
    citsy::MockHost host;
    engine.start(host);
    tap(engine, host, citsy::Button::Right);

    CHECK(engine.current_room_id() == "0");
    CHECK(engine.dialog_active());
    CHECK(engine.dialog_line() == "Crossing over.");

    press_ok(engine, host);
    CHECK(engine.current_room_id() == "1");
    CHECK(engine.avatar_x() == 3);
    CHECK(engine.avatar_y() == 3);
    CHECK_FALSE(engine.dialog_active());
}

// ===========================================================================
// Fixture: playable.bitsy
// ===========================================================================

TEST_CASE("sim: playable.bitsy fixture — talk, pick up, exit", "[engine][sim][fixture]") {
    auto text = load_fixture("playable.bitsy");
    citsy::Engine engine(text);
    citsy::MockHost host;
    engine.start(host);

    CHECK(engine.current_room_id() == "0");
    CHECK(engine.avatar_x() == 2);
    CHECK(engine.avatar_y() == 8);

    engine.update(host);
    const auto* snap = host.last_snapshot();
    REQUIRE(snap != nullptr);
    CHECK(snap->map1[map_i(0, 0)] == static_cast<std::uint8_t>('a'));
    CHECK(snap->map2[map_i(2, 8)] == static_cast<std::uint8_t>('A'));
    CHECK(snap->map2[map_i(4, 8)] != 0);  // npc
    CHECK(snap->map2[map_i(2, 6)] != 0);  // key

    // Talk to the NPC two tiles to the right.
    tap(engine, host, citsy::Button::Right);
    tap(engine, host, citsy::Button::Right);
    CHECK(engine.dialog_active());
    CHECK(engine.dialog_line() == "Hello!");
    press_ok(engine, host);
    CHECK(engine.dialog_line() == "Nice to meet you.");
    press_ok(engine, host);
    CHECK_FALSE(engine.dialog_active());
    CHECK(engine.avatar_x() == 3);  // stopped one tile before the NPC

    // Walk up to the key at (2,6).
    tap(engine, host, citsy::Button::Left);
    tap(engine, host, citsy::Button::Up);
    tap(engine, host, citsy::Button::Up);
    CHECK(engine.avatar_x() == 2);
    CHECK(engine.avatar_y() == 6);
    CHECK(engine.dialog_active());
    CHECK(engine.dialog_line() == "You found a key.");
    press_ok(engine, host);
    CHECK_FALSE(engine.dialog_active());

    // Walk around the NPC at (4,8) along y=7, then to the exit at (15,8).
    tap(engine, host, citsy::Button::Down);  // 2,7
    for (int i = 0; i < 3; ++i) tap(engine, host, citsy::Button::Right);  // 5,7
    tap(engine, host, citsy::Button::Down);  // 5,8
    for (int i = 0; i < 10; ++i) tap(engine, host, citsy::Button::Right);  // 15,8
    CHECK(engine.current_room_id() == "1");
    CHECK(engine.avatar_x() == 0);
    CHECK(engine.avatar_y() == 8);
}

// ===========================================================================
// Inventory, variables, conditionals, endings (Phase 2)
// ===========================================================================

TEST_CASE("sim: picking up an item increments inventory", "[engine][sim][inventory]") {
    auto src = bitsy_game({
        std::string(kPal),
        room0(empty_grid(), "ITM 0 5,4\n"),
        avatar_at(4, 4),
        std::string(kKeyArt),
    });
    citsy::Engine engine(src);
    citsy::MockHost host;
    engine.start(host);
    CHECK(engine.item_count("0") == 0);
    CHECK(engine.item_count("key") == 0);

    tap(engine, host, citsy::Button::Right);
    CHECK(engine.item_count("0") == 1);
    CHECK(engine.item_count("key") == 1);
}

TEST_CASE("sim: dialog can give and take items", "[engine][sim][inventory]") {
    auto src = bitsy_game({
        std::string(kPal),
        room0(empty_grid()),
        avatar_at(4, 4),
        std::string(kNpcArt) + "POS 0 5,4\nDLG DLG_NPC\n",
        std::string(kKeyArt),
        R"(DLG DLG_NPC
{item "0" 1}
"Here, take this."
)",
    });
    citsy::Engine engine(src);
    citsy::MockHost host;
    engine.start(host);
    tap(engine, host, citsy::Button::Right);
    CHECK(engine.dialog_line() == "Here, take this.");
    CHECK(engine.item_count("0") == 1);
    press_ok(engine, host);
    CHECK(engine.item_count("key") == 1);
}

TEST_CASE("sim: branching dialog depends on item count", "[engine][sim][dialog]") {
    auto src = bitsy_game({
        std::string(kPal),
        room0(empty_grid(), "ITM 0 5,4\n"),
        avatar_at(4, 4),
        std::string(kNpcArt) + "POS 0 7,4\nDLG DLG_NPC\n",
        std::string(kKeyArt),
        R"(DLG DLG_NPC
{
  - {item "0"} > 0 ?
    "You have the key."
  - else ?
    "The door is locked."
}
)",
    });
    citsy::Engine engine(src);
    citsy::MockHost host;
    engine.start(host);

    tap(engine, host, citsy::Button::Right);  // pick up key at 5,4
    CHECK(engine.item_count("0") == 1);
    if (engine.dialog_active()) press_ok(engine, host);

    tap(engine, host, citsy::Button::Right);  // 6,4
    tap(engine, host, citsy::Button::Right);  // bump npc at 7,4
    CHECK(engine.dialog_active());
    CHECK(engine.dialog_line() == "You have the key.");
}

TEST_CASE("sim: variable assignment persists across talks", "[engine][sim][dialog]") {
    auto src = bitsy_game({
        std::string(kPal),
        room0(empty_grid()),
        avatar_at(4, 4),
        std::string(kNpcArt) + "POS 0 5,4\nDLG DLG_NPC\n",
        "VAR talks\n0\n",
        R"(DLG DLG_NPC
{talks = talks + 1}
"Talked {print talks} times."
)",
    });
    citsy::Engine engine(src);
    citsy::MockHost host;
    engine.start(host);
    CHECK(engine.variable_value("talks") == "0");

    tap(engine, host, citsy::Button::Right);
    CHECK(engine.dialog_line() == "Talked 1 times.");
    CHECK(engine.variable_value("talks") == "1");
    press_ok(engine, host);

    tap(engine, host, citsy::Button::Right);
    CHECK(engine.dialog_line() == "Talked 2 times.");
    CHECK(engine.variable_value("talks") == "2");
}

TEST_CASE("sim: sequence dialog changes on each visit", "[engine][sim][dialog]") {
    auto src = bitsy_game({
        std::string(kPal),
        room0(empty_grid()),
        avatar_at(4, 4),
        std::string(kNpcArt) + "POS 0 5,4\nDLG DLG_NPC\n",
        R"(DLG DLG_NPC
{sequence
  - "first"
  - "second"
}
)",
    });
    citsy::Engine engine(src);
    citsy::MockHost host;
    engine.start(host);

    tap(engine, host, citsy::Button::Right);
    CHECK(engine.dialog_line() == "first");
    press_ok(engine, host);

    tap(engine, host, citsy::Button::Right);
    CHECK(engine.dialog_line() == "second");
    press_ok(engine, host);

    tap(engine, host, citsy::Button::Right);
    CHECK(engine.dialog_line() == "second");
}

TEST_CASE("sim: ending tile shows text then stops the game", "[engine][sim][ending]") {
    auto src = bitsy_game({
        std::string(kPal),
        room0(empty_grid(), "END 0 5,4\n"),
        avatar_at(4, 4),
        "END 0\nYou win!\nNAME good\n",
    });
    citsy::Engine engine(src);
    citsy::MockHost host;
    engine.start(host);
    REQUIRE(engine.is_running());

    tap(engine, host, citsy::Button::Right);
    CHECK(engine.avatar_x() == 5);
    CHECK(engine.dialog_active());
    CHECK(engine.dialog_line() == "You win!");
    CHECK(engine.is_running());

    press_ok(engine, host);
    CHECK_FALSE(engine.dialog_active());
    CHECK_FALSE(engine.is_running());
}

TEST_CASE("sim: {end} in dialog stops the game after the box closes", "[engine][sim][ending]") {
    auto src = bitsy_game({
        std::string(kPal),
        room0(empty_grid()),
        avatar_at(4, 4),
        std::string(kNpcArt) + "POS 0 5,4\nDLG DLG_NPC\n",
        R"(DLG DLG_NPC
"Farewell."
{end}
)",
    });
    citsy::Engine engine(src);
    citsy::MockHost host;
    engine.start(host);
    tap(engine, host, citsy::Button::Right);
    CHECK(engine.dialog_line() == "Farewell.");
    CHECK(engine.is_running());
    press_ok(engine, host);
    CHECK_FALSE(engine.is_running());
}

TEST_CASE("sim: {exit} in dialog warps after the box closes", "[engine][sim][dialog]") {
    auto src = bitsy_game({
        std::string(kPal),
        room0(empty_grid()),
        "ROOM 1\n" + empty_grid() + "PAL 0\n",
        avatar_at(4, 4),
        std::string(kNpcArt) + "POS 0 5,4\nDLG DLG_NPC\n",
        R"(DLG DLG_NPC
"Off you go."
{exit "1" 3 3}
)",
    });
    citsy::Engine engine(src);
    citsy::MockHost host;
    engine.start(host);
    tap(engine, host, citsy::Button::Right);
    CHECK(engine.current_room_id() == "0");
    CHECK(engine.dialog_line() == "Off you go.");
    press_ok(engine, host);
    CHECK(engine.current_room_id() == "1");
    CHECK(engine.avatar_x() == 3);
    CHECK(engine.avatar_y() == 3);
}

TEST_CASE("sim: fixture scripted.bitsy — key, gate, ending", "[engine][sim][fixture]") {
    auto text = load_fixture("scripted.bitsy");
    citsy::Engine engine(text);
    citsy::MockHost host;
    engine.start(host);

    CHECK(engine.variable_value("met") == "0");
    CHECK(engine.item_count("0") == 0);

    // Talk to the guard at (5,4) without a key.
    tap(engine, host, citsy::Button::Right);
    CHECK(engine.dialog_line() == "I need a key.");
    press_ok(engine, host);
    CHECK(engine.variable_value("met") == "1");

    // Pick up the key at (4,2).
    tap(engine, host, citsy::Button::Up);
    tap(engine, host, citsy::Button::Up);
    CHECK(engine.avatar_y() == 2);
    CHECK(engine.item_count("0") == 1);
    CHECK(engine.dialog_line() == "Got a key.");
    press_ok(engine, host);

    // Talk to the guard again.
    tap(engine, host, citsy::Button::Down);
    tap(engine, host, citsy::Button::Down);
    tap(engine, host, citsy::Button::Right);
    CHECK(engine.dialog_line() == "Go on through.");
    press_ok(engine, host);

    // Walk left onto the ending at (3,4).
    tap(engine, host, citsy::Button::Left);
    CHECK(engine.dialog_line() == "You found the way out.");
    press_ok(engine, host);
    CHECK_FALSE(engine.is_running());
}
