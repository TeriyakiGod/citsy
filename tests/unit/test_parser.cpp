#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

// Include internal parser directly for white-box testing.
#include "src/parser/parser.hpp"

#include <citsy/engine.hpp>
#include <citsy/types.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static std::string load_fixture(const char* name) {
    // Tests run from the build directory; fixtures are in tests/data/.
    // We walk up to find the source tree via __FILE__.
    fs::path here = fs::path(__FILE__).parent_path();          // tests/unit/
    fs::path data = here.parent_path() / "data" / name;       // tests/data/<name>
    std::ifstream f(data);
    REQUIRE(f.is_open());
    std::ostringstream buf;
    buf << f.rdbuf();
    return buf.str();
}

// ===========================================================================
// Version & metadata
// ===========================================================================

TEST_CASE("parser: version header", "[parser]") {
    constexpr std::string_view src = R"(
# BITSY VERSION 8.12

PAL 0
NAME pal
0,0,0
255,255,255
128,128,255

SPR A
00000000
00000000
00000000
00000000
00000000
00000000
00000000
00000000
NAME Avatar
POS 0 0,0
)";
    auto game = citsy::parse(src);
    CHECK(game.version.major == 8);
    CHECK(game.version.minor == 12);
}

TEST_CASE("parser: game title from NAME directive", "[parser]") {
    constexpr std::string_view src = R"(
# BITSY VERSION 8.12

NAME my cool game

PAL 0
NAME pal
0,0,0
255,255,255
128,128,255
)";
    auto game = citsy::parse(src);
    CHECK(game.title == "my cool game");
}

TEST_CASE("parser: room_format directive", "[parser]") {
    constexpr std::string_view src = R"(
# BITSY VERSION 8.12

! ROOM_FORMAT 1

PAL 0
NAME pal
0,0,0
255,255,255
128,128,255
)";
    auto game = citsy::parse(src);
    CHECK(game.room_format == 1);
}

// ===========================================================================
// Palette
// ===========================================================================

TEST_CASE("parser: palette three colors", "[parser][palette]") {
    constexpr std::string_view src = R"(
PAL 0
NAME test palette
0,82,204
128,159,255
255,255,255
)";
    auto game = citsy::parse(src);
    REQUIRE(game.palettes.count("0") == 1);
    const auto& pal = game.palettes.at("0");
    CHECK(pal.id == "0");
    CHECK(pal.name == "test palette");
    REQUIRE(pal.colors.size() == 3);
    CHECK(pal.colors[0].r ==   0); CHECK(pal.colors[0].g ==  82); CHECK(pal.colors[0].b == 204);
    CHECK(pal.colors[1].r == 128); CHECK(pal.colors[1].g == 159); CHECK(pal.colors[1].b == 255);
    CHECK(pal.colors[2].r == 255); CHECK(pal.colors[2].g == 255); CHECK(pal.colors[2].b == 255);
}

TEST_CASE("parser: multiple palettes", "[parser][palette]") {
    constexpr std::string_view src = R"(
PAL 0
NAME first
0,0,0
255,255,255
128,128,128

PAL 1
NAME second
10,20,30
40,50,60
70,80,90
)";
    auto game = citsy::parse(src);
    CHECK(game.palettes.size() == 2);
    CHECK(game.palettes.count("0") == 1);
    CHECK(game.palettes.count("1") == 1);
    CHECK(game.palettes.at("1").name == "second");
}

// ===========================================================================
// Tile
// ===========================================================================

TEST_CASE("parser: simple tile", "[parser][tile]") {
    constexpr std::string_view src = R"(
TIL a
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
    auto game = citsy::parse(src);
    REQUIRE(game.tiles.count("a") == 1);
    const auto& tile = game.tiles.at("a");
    CHECK(tile.id == "a");
    CHECK(tile.name == "wall");
    CHECK(tile.is_wall == true);
    REQUIRE(tile.frames.size() == 1);

    // Verify pixel data — border pixels are 1, interior are 0.
    const auto& frame = tile.frames[0];
    // Top row: all 1s
    for (int c = 0; c < citsy::kTileSize; ++c)
        CHECK(frame[0 * citsy::kTileSize + c] == 1);
    // Interior pixels (row 1, col 1) should be 0
    CHECK(frame[1 * citsy::kTileSize + 1] == 0);
    // Bottom row: all 1s
    for (int c = 0; c < citsy::kTileSize; ++c)
        CHECK(frame[7 * citsy::kTileSize + c] == 1);
}

TEST_CASE("parser: non-wall tile", "[parser][tile]") {
    constexpr std::string_view src = R"(
TIL 0
00000000
00000000
00000000
00000000
00000000
00000000
00000000
00000000
NAME background
WAL false
)";
    auto game = citsy::parse(src);
    REQUIRE(game.tiles.count("0") == 1);
    CHECK(game.tiles.at("0").is_wall == false);
}

TEST_CASE("parser: animated tile with two frames", "[parser][tile]") {
    constexpr std::string_view src = R"(
TIL b
11111111
11111111
11111111
11111111
11111111
11111111
11111111
11111111
>
00000000
01111110
01111110
01111110
01111110
01111110
01111110
00000000
NAME blink
)";
    auto game = citsy::parse(src);
    REQUIRE(game.tiles.count("b") == 1);
    const auto& tile = game.tiles.at("b");
    REQUIRE(tile.frames.size() == 2);
    // Frame 0: all 1s
    for (int i = 0; i < 64; ++i) CHECK(tile.frames[0][i] == 1);
    // Frame 1: border 0s, interior 1s
    for (int c = 0; c < citsy::kTileSize; ++c)
        CHECK(tile.frames[1][0 * citsy::kTileSize + c] == 0);
    CHECK(tile.frames[1][1 * citsy::kTileSize + 1] == 1);
}

TEST_CASE("parser: tile color index COL", "[parser][tile]") {
    constexpr std::string_view src = R"(
TIL c
00000000
00011000
00111100
01111110
01111110
00111100
00011000
00000000
COL 2
NAME gem
)";
    auto game = citsy::parse(src);
    REQUIRE(game.tiles.count("c") == 1);
    CHECK(game.tiles.at("c").color_index == 2);
}

// ===========================================================================
// Sprite
// ===========================================================================

TEST_CASE("parser: avatar sprite position", "[parser][sprite]") {
    constexpr std::string_view src = R"(
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
POS 0 4,7
)";
    auto game = citsy::parse(src);
    REQUIRE(game.sprites.count("A") == 1);
    const auto& spr = game.sprites.at("A");
    CHECK(spr.name == "Avatar");
    REQUIRE(spr.position.has_value());
    CHECK(spr.position->room_id == "0");
    CHECK(spr.position->x == 4);
    CHECK(spr.position->y == 7);
    REQUIRE(spr.frames.size() == 1);
}

TEST_CASE("parser: non-avatar sprite with dialog", "[parser][sprite]") {
    constexpr std::string_view src = R"(
SPR 0
00000000
00011000
01111110
01111110
00111100
00011000
00000000
00000000
NAME cat
POS 2 3,5
DLG DLG_1
)";
    auto game = citsy::parse(src);
    REQUIRE(game.sprites.count("0") == 1);
    const auto& spr = game.sprites.at("0");
    CHECK(spr.dialog_id == "DLG_1");
    CHECK(spr.position->room_id == "2");
    CHECK(spr.position->x == 3);
    CHECK(spr.position->y == 5);
}

// ===========================================================================
// Item
// ===========================================================================

TEST_CASE("parser: item with dialog", "[parser][item]") {
    constexpr std::string_view src = R"(
ITM 0
00000000
00011000
00111100
01111110
01111110
00111100
00011000
00000000
NAME key
DLG DLG_0
)";
    auto game = citsy::parse(src);
    REQUIRE(game.items.count("0") == 1);
    const auto& itm = game.items.at("0");
    CHECK(itm.name == "key");
    CHECK(itm.dialog_id == "DLG_0");
    REQUIRE(itm.frames.size() == 1);
}

// ===========================================================================
// Room
// ===========================================================================

TEST_CASE("parser: room tile grid (comma-separated)", "[parser][room]") {
    // Build a 16×16 room where all cells are "0" except [0][0] = "a"
    std::string src = "ROOM 0\n";
    // Row 0: starts with "a"
    src += "a";
    for (int c = 1; c < citsy::kMapSize; ++c) src += ",0";
    src += "\n";
    // Rows 1-15: all "0"
    for (int r = 1; r < citsy::kMapSize; ++r) {
        src += "0";
        for (int c = 1; c < citsy::kMapSize; ++c) src += ",0";
        src += "\n";
    }
    src += "NAME test room\nPAL 0\n";

    auto game = citsy::parse(src);
    REQUIRE(game.rooms.count("0") == 1);
    const auto& room = game.rooms.at("0");
    CHECK(room.name == "test room");
    CHECK(room.palette_id == "0");
    CHECK(room.tiles[0][0] == "a");
    CHECK(room.tiles[0][1] == "0");
    CHECK(room.tiles[1][0] == "0");
    CHECK(room.tiles[15][15] == "0");
}

TEST_CASE("parser: room items", "[parser][room]") {
    std::string src = "ROOM 0\n";
    for (int r = 0; r < citsy::kMapSize; ++r) {
        src += "0";
        for (int c = 1; c < citsy::kMapSize; ++c) src += ",0";
        src += "\n";
    }
    src += "NAME r\nPAL 0\nITM 0 5,3\n";

    auto game = citsy::parse(src);
    REQUIRE(game.rooms.count("0") == 1);
    const auto& room = game.rooms.at("0");
    REQUIRE(room.items.size() == 1);
    CHECK(room.items[0].item_id == "0");
    CHECK(room.items[0].x == 5);
    CHECK(room.items[0].y == 3);
}

TEST_CASE("parser: room exits", "[parser][room]") {
    std::string src = "ROOM 0\n";
    for (int r = 0; r < citsy::kMapSize; ++r) {
        src += "0";
        for (int c = 1; c < citsy::kMapSize; ++c) src += ",0";
        src += "\n";
    }
    src += "NAME r\nPAL 0\nEXT 15,8 1 0,8\n";

    auto game = citsy::parse(src);
    REQUIRE(game.rooms.count("0") == 1);
    const auto& room = game.rooms.at("0");
    REQUIRE(room.exits.size() == 1);
    CHECK(room.exits[0].x == 15);
    CHECK(room.exits[0].y == 8);
    CHECK(room.exits[0].dest_room_id == "1");
    CHECK(room.exits[0].dest_x == 0);
    CHECK(room.exits[0].dest_y == 8);
}

TEST_CASE("parser: room ending references", "[parser][room]") {
    std::string src = "ROOM 0\n";
    for (int r = 0; r < citsy::kMapSize; ++r) {
        src += "0";
        for (int c = 1; c < citsy::kMapSize; ++c) src += ",0";
        src += "\n";
    }
    src += "NAME r\nPAL 0\nEND 0 8,0\n";

    auto game = citsy::parse(src);
    const auto& room = game.rooms.at("0");
    REQUIRE(room.endings.size() == 1);
    CHECK(room.endings[0].ending_id == "0");
    CHECK(room.endings[0].x == 8);
    CHECK(room.endings[0].y == 0);
}

// ===========================================================================
// Dialogue
// ===========================================================================

TEST_CASE("parser: dialogue content", "[parser][dialogue]") {
    constexpr std::string_view src = R"(
DLG DLG_0
"Hello, world!"
)";
    auto game = citsy::parse(src);
    REQUIRE(game.dialogues.count("DLG_0") == 1);
    CHECK(game.dialogues.at("DLG_0").content == "\"Hello, world!\"");
}

TEST_CASE("parser: multiline dialogue", "[parser][dialogue]") {
    constexpr std::string_view src = R"(
DLG DLG_1
"Line one."
"Line two."
)";
    auto game = citsy::parse(src);
    REQUIRE(game.dialogues.count("DLG_1") == 1);
    const auto& dlg = game.dialogues.at("DLG_1");
    // Content should preserve both lines.
    CHECK(dlg.content.find("Line one") != std::string::npos);
    CHECK(dlg.content.find("Line two") != std::string::npos);
}

// ===========================================================================
// Variable
// ===========================================================================

TEST_CASE("parser: numeric variable", "[parser][variable]") {
    constexpr std::string_view src = R"(
VAR score
0
)";
    auto game = citsy::parse(src);
    REQUIRE(game.variables.count("score") == 1);
    CHECK(game.variables.at("score").value == "0");
}

TEST_CASE("parser: string variable", "[parser][variable]") {
    constexpr std::string_view src = R"(
VAR name
ada
)";
    auto game = citsy::parse(src);
    REQUIRE(game.variables.count("name") == 1);
    CHECK(game.variables.at("name").value == "ada");
}

// ===========================================================================
// Ending
// ===========================================================================

TEST_CASE("parser: triple-quoted dialogue keeps blank lines", "[parser][dialogue]") {
    constexpr std::string_view src = R"(
DLG DLG_0
"""
hello

world
"""
)";
    auto game = citsy::parse(src);
    REQUIRE(game.dialogues.count("DLG_0") == 1);
    const auto& c = game.dialogues.at("DLG_0").content;
    CHECK(c.find("hello") != std::string::npos);
    CHECK(c.find("world") != std::string::npos);
}

TEST_CASE("parser: ending text and name", "[parser][ending]") {
    constexpr std::string_view src = R"(
END 0
You found the treasure!
NAME good ending
)";
    auto game = citsy::parse(src);
    REQUIRE(game.endings.count("0") == 1);
    const auto& end = game.endings.at("0");
    CHECK(end.text == "You found the treasure!");
    CHECK(end.name == "good ending");
}

TEST_CASE("parser: triple-quoted ending keeps blank lines", "[parser][ending]") {
    constexpr std::string_view src = R"(
END 0
"""
it's a big world

good luck
"""
NAME good ending
)";
    auto game = citsy::parse(src);
    REQUIRE(game.endings.count("0") == 1);
    CHECK(game.endings.at("0").text.find("it's a big world") != std::string::npos);
    CHECK(game.endings.at("0").text.find("good luck") != std::string::npos);
    CHECK(game.endings.at("0").name == "good ending");
}

// ===========================================================================
// Game model helpers
// ===========================================================================

TEST_CASE("game: avatar() returns sprite A", "[model]") {
    constexpr std::string_view src = R"(
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
    auto game = citsy::parse(src);
    const auto* av = game.avatar();
    REQUIRE(av != nullptr);
    CHECK(av->id == "A");
}

TEST_CASE("game: start_room_id from avatar POS", "[model]") {
    constexpr std::string_view src = R"(
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
POS 3 2,2
)";
    auto game = citsy::parse(src);
    CHECK(game.start_room_id() == "3");
}

// ===========================================================================
// Error handling
// ===========================================================================

TEST_CASE("parser: bad tile row length throws", "[parser][errors]") {
    constexpr std::string_view src = R"(
TIL x
1111111
10000001
10000001
10000001
10000001
10000001
10000001
11111111
)";
    CHECK_THROWS_AS(citsy::parse(src), citsy::ParseError);
}

TEST_CASE("parser: incomplete tile frame throws", "[parser][errors]") {
    constexpr std::string_view src = R"(
TIL x
11111111
10000001
10000001
)";
    CHECK_THROWS_AS(citsy::parse(src), citsy::ParseError);
}

TEST_CASE("parser: bad palette color throws", "[parser][errors]") {
    constexpr std::string_view src = R"(
PAL 0
NAME bad
0,82,300
)";
    CHECK_THROWS_AS(citsy::parse(src), citsy::ParseError);
}

// ===========================================================================
// Full fixture tests
// ===========================================================================

TEST_CASE("fixture: minimal.bitsy parses cleanly", "[fixture]") {
    auto text = load_fixture("minimal.bitsy");
    citsy::Game game;
    REQUIRE_NOTHROW(game = citsy::parse(text));

    CHECK(game.version.major == 8);
    CHECK(game.room_format == 1);
    CHECK(game.palettes.count("0") == 1);
    CHECK(game.rooms.count("0") == 1);
    CHECK(game.tiles.count("a") == 1);
    CHECK(game.sprites.count("A") == 1);
    CHECK(game.items.count("0") == 1);
    CHECK(game.dialogues.count("DLG_0") == 1);
    CHECK(game.variables.count("score") == 1);
    CHECK(game.endings.count("0") == 1);
    CHECK(game.tiles.at("a").is_wall == true);
}

TEST_CASE("fixture: animated.bitsy parses two tile frames", "[fixture]") {
    auto text = load_fixture("animated.bitsy");
    auto game = citsy::parse(text);
    REQUIRE(game.tiles.count("b") == 1);
    CHECK(game.tiles.at("b").frames.size() == 2);
}

TEST_CASE("fixture: two_rooms.bitsy has two rooms and exits", "[fixture]") {
    auto text = load_fixture("two_rooms.bitsy");
    auto game = citsy::parse(text);
    CHECK(game.rooms.size() == 2);
    CHECK(game.palettes.size() == 2);
    REQUIRE(game.rooms.count("0") == 1);
    REQUIRE(game.rooms.count("1") == 1);
    CHECK(game.rooms.at("0").exits.size() == 1);
    CHECK(game.rooms.at("1").exits.size() == 1);
    CHECK(game.rooms.at("0").exits[0].dest_room_id == "1");
    CHECK(game.rooms.at("1").exits[0].dest_room_id == "0");
    CHECK(game.rooms.at("0").palette_id == "0");
    CHECK(game.rooms.at("1").palette_id == "1");
}

TEST_CASE("fixture: playable.bitsy has npc, item, and two rooms", "[fixture]") {
    auto text = load_fixture("playable.bitsy");
    auto game = citsy::parse(text);
    CHECK(game.rooms.size() == 2);
    REQUIRE(game.sprites.count("A") == 1);
    REQUIRE(game.sprites.count("0") == 1);
    CHECK(game.sprites.at("A").position->x == 2);
    CHECK(game.sprites.at("A").position->y == 8);
    CHECK(game.sprites.at("0").dialog_id == "DLG_NPC");
    REQUIRE(game.rooms.at("0").items.size() == 1);
    CHECK(game.rooms.at("0").items[0].item_id == "0");
    CHECK(game.dialogues.count("DLG_NPC") == 1);
    CHECK(game.dialogues.count("DLG_KEY") == 1);
}

// ---------------------------------------------------------------------------
// Real-world fixture: mossland by candle (Bitsy v6.4)
// https://adamledoux.itch.io/mossland
// ---------------------------------------------------------------------------

TEST_CASE("fixture: mossland.bitsy parses real game", "[fixture][mossland]") {
    auto text = load_fixture("mossland.bitsy");
    citsy::Game game;
    REQUIRE_NOTHROW(game = citsy::parse(text));

    // Header
    CHECK(game.version.major == 6);
    CHECK(game.version.minor == 4);
    CHECK(game.room_format == 1);

    // Palettes
    CHECK(game.palettes.size() == 3);
    REQUIRE(game.palettes.count("0") == 1);
    REQUIRE(game.palettes.count("1") == 1);
    REQUIRE(game.palettes.count("2") == 1);
    CHECK(game.palettes.at("0").name == "brown");
    CHECK(game.palettes.at("1").name == "green");
    CHECK(game.palettes.at("2").name == "purple");
    CHECK(game.palettes.at("0").colors.size() == 3);

    // Entity counts
    CHECK(game.rooms.size()   == 7);
    CHECK(game.tiles.size()   == 31);
    CHECK(game.sprites.size() == 18);
    CHECK(game.items.size()   == 2);
    CHECK(game.dialogues.size() == 10);
    CHECK(game.endings.size() == 1);

    // Avatar (sprite A) — two animation frames, starts in room 1
    const auto* avatar = game.avatar();
    REQUIRE(avatar != nullptr);
    CHECK(avatar->frames.size() == 2);
    REQUIRE(avatar->position.has_value());
    CHECK(avatar->position->room_id == "1");
    CHECK(avatar->position->x == 8);
    CHECK(avatar->position->y == 9);
    CHECK(game.start_room_id() == "1");

    // Tiles — named wall tile and animated leaf tile
    REQUIRE(game.tiles.count("b") == 1);
    CHECK(game.tiles.at("b").name == "Moss");
    CHECK(game.tiles.at("b").is_wall == true);
    REQUIRE(game.tiles.count("p") == 1);
    CHECK(game.tiles.at("p").frames.size() == 2);
    CHECK(game.tiles.at("p").is_wall == true);

    // Items
    REQUIRE(game.items.count("0") == 1);
    REQUIRE(game.items.count("1") == 1);
    CHECK(game.items.at("0").name == "Spores");
    CHECK(game.items.at("1").name == "Can");
    CHECK(game.items.at("0").dialog_id == "ITM_1");
    CHECK(game.items.at("1").dialog_id == "ITM_0");

    // Room 2 — seven placed spore tiles plus the watering can
    REQUIRE(game.rooms.count("2") == 1);
    const auto& room2 = game.rooms.at("2");
    CHECK(room2.palette_id == "2");
    CHECK(room2.exits.size() == 4);
    CHECK(room2.items.size() == 7);
    CHECK(room2.items[0].item_id == "1");
    CHECK(room2.items[0].x == 10);
    CHECK(room2.items[0].y == 12);

    // Room 6 — ending trigger tile
    REQUIRE(game.rooms.count("6") == 1);
    const auto& room6 = game.rooms.at("6");
    REQUIRE(room6.endings.size() == 1);
    CHECK(room6.endings[0].ending_id == "0");
    CHECK(room6.endings[0].x == 15);
    CHECK(room6.endings[0].y == 8);

    // Dialogues — simple text and scripted sequence (triple-quoted blocks)
    REQUIRE(game.dialogues.count("SPR_0") == 1);
    CHECK(game.dialogues.at("SPR_0").content == "I'm a cat");
    REQUIRE(game.dialogues.count("SPR_1") == 1);
    CHECK(game.dialogues.at("SPR_1").content.find("sequence") != std::string::npos);
    CHECK(game.dialogues.at("SPR_1").content.find("I tend the moss") != std::string::npos);
    CHECK(game.dialogues.at("SPR_1").content.find("moss-some") != std::string::npos);

    // Ending text (triple-quoted block, including the line after a blank)
    REQUIRE(game.endings.count("0") == 1);
    CHECK(game.endings.at("0").text.find("it's a big world, little bug") != std::string::npos);
    CHECK(game.endings.at("0").text.find("good luck") != std::string::npos);
}
