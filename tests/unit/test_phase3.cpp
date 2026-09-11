#include <catch2/catch_test_macros.hpp>

#include <citsy/engine.hpp>
#include <citsy/types.hpp>
#include "backends/mock/mock_host.hpp"
#include "src/dialog/script.hpp"
#include "src/font/font.hpp"
#include "src/parser/parser.hpp"
#include "src/sound/sound.hpp"
#include "src/transition/transition.hpp"

#include <string>
#include <sstream>
#include <fstream>
#include <filesystem>
#include <unordered_map>

namespace {

constexpr std::string_view kPal = R"(PAL 0
NAME pal
0,82,204
128,159,255
255,255,255
)";

constexpr std::string_view kAvatar = R"(SPR A
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

constexpr std::string_view kEmptyGrid =
    "0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0\n"
    "0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0\n"
    "0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0\n"
    "0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0\n"
    "0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0\n"
    "0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0\n"
    "0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0\n"
    "0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0\n"
    "0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0\n"
    "0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0\n"
    "0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0\n"
    "0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0\n"
    "0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0\n"
    "0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0\n"
    "0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0\n"
    "0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0\n";

std::string game_src(std::string extra) {
    return std::string("# BITSY VERSION 8.15\n\n! ROOM_FORMAT 1\n\n") +
           std::string(kPal) + "\nROOM 0\n" + std::string(kEmptyGrid) +
           "PAL 0\n" + extra + "\n" + std::string(kAvatar);
}

void tap(citsy::Engine& e, citsy::MockHost& h, citsy::Button b) {
    h.set_button(b, true);
    e.update(h);
    h.set_button(b, false);
    e.update(h);
}

} // namespace

// ---------------------------------------------------------------------------
// Parser: 8.15 segments
// ---------------------------------------------------------------------------

TEST_CASE("parser: TEXT_DIRECTION RTL", "[parser][phase3]") {
    auto g = citsy::parse("TEXT_DIRECTION RTL\n");
    CHECK(g.text_direction == citsy::TextDirection::RightToLeft);
}

TEST_CASE("parser: DEFAULT_FONT and FONT data", "[parser][phase3][font]") {
    constexpr std::string_view src = R"(
DEFAULT_FONT myfont

FONT myfont
SIZE 4 4
CHAR 65
1111
1001
1001
1111
)";
    auto g = citsy::parse(src);
    CHECK(g.font_name == "myfont");
    CHECK(g.font_data.find("CHAR 65") != std::string::npos);
}

TEST_CASE("parser: room AVA and TUNE", "[parser][phase3]") {
    std::string src = "ROOM 0\n";
    src += kEmptyGrid;
    src += "PAL 0\nAVA 1\nTUNE 2\n";
    auto g = citsy::parse(src);
    REQUIRE(g.rooms.count("0"));
    CHECK(g.rooms.at("0").avatar_id == "1");
    CHECK(g.rooms.at("0").tune_id == "2");
}

TEST_CASE("parser: sprite BLIP and BGC", "[parser][phase3]") {
    constexpr std::string_view src = R"(
SPR 0
00000000
00000000
00000000
00000000
00000000
00000000
00000000
00000000
BLIP 1
BGC *
)";
    auto g = citsy::parse(src);
    REQUIRE(g.sprites.count("0"));
    CHECK(g.sprites.at("0").blip_id == "1");
    CHECK(g.sprites.at("0").bgc_transparent);
}

TEST_CASE("parser: blip pitches and envelope", "[parser][phase3][sound]") {
    constexpr std::string_view src = R"(
BLIP 1
C4,E4,G4
NAME chirp
ENV 10 20 8 40 15
BEAT 30 0
SQR P8
)";
    auto g = citsy::parse(src);
    REQUIRE(g.blips.count("1"));
    const auto& b = g.blips.at("1");
    CHECK(b.name == "chirp");
    CHECK(b.pitch_a.beats == 1);
    CHECK(b.pitch_a.note == 0);
    CHECK(b.envelope.attack == 10);
    CHECK(b.envelope.release == 15);
    CHECK(b.instrument == citsy::PulseWave::Eighth);
}

TEST_CASE("parser: tune bars tempo square arp", "[parser][phase3][sound]") {
    constexpr std::string_view src = R"(
TUNE 1
C,0,E,0,G,0,0,0,0,0,0,0,0,0,0,0
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
NAME jam
TMP FST
SQR P4 P8
ARP UP
)";
    auto g = citsy::parse(src);
    REQUIRE(g.tunes.count("1"));
    const auto& t = g.tunes.at("1");
    CHECK(t.name == "jam");
    REQUIRE(t.melody.size() == 1);
    CHECK(t.melody[0][0].note == 0);
    CHECK(t.tempo == citsy::Tempo::Fast);
    CHECK(t.instrument_a == citsy::PulseWave::Quarter);
    CHECK(t.arpeggio == citsy::Arpeggio::Up);
}

TEST_CASE("parser: exit FX transition", "[parser][phase3]") {
    std::string src = "ROOM 0\n";
    src += kEmptyGrid;
    src += "EXT 15,8 1 0,8 FX fade_w\n";
    auto g = citsy::parse(src);
    REQUIRE(g.rooms.at("0").exits.size() == 1);
    CHECK(g.rooms.at("0").exits[0].transition_effect == "fade_w");
}

// ---------------------------------------------------------------------------
// Font
// ---------------------------------------------------------------------------

TEST_CASE("font: default ascii_small has A and space", "[font][phase3]") {
    auto f = citsy::default_font();
    CHECK(f.has(U'A'));
    CHECK(f.has(U' '));
    CHECK(f.glyph(U'A').width == 6);
    CHECK(f.glyph(U'A').height == 8);
}

TEST_CASE("font: custom CHAR_SIZE variable width", "[font][phase3]") {
    auto f = citsy::parse_bitsyfont(R"(
FONT skinny
SIZE 6 8
CHAR 73
CHAR_SIZE 4 8
CHAR_SPACING 5
1110
0100
0100
0100
0100
0100
1110
0000
)");
    REQUIRE(f.has(U'I'));
    CHECK(f.glyph(U'I').width == 4);
    CHECK(f.glyph(U'I').spacing == 5);
}

TEST_CASE("font: textbox renders letter pixels", "[font][phase3]") {
    auto f = citsy::default_font();
    citsy::TextSpan sp;
    sp.text = "Hi";
    citsy::TextboxLayout layout;
    layout.width = 104;
    layout.height = 32;
    auto pix = citsy::render_textbox(f, {sp}, layout);
    REQUIRE(pix.size() == 104 * 32);
    int ink = 0;
    for (auto p : pix) if (p == citsy::kTextboxWhite) ++ink;
    CHECK(ink > 10);
}

TEST_CASE("font: RTL flips glyph origin", "[font][phase3]") {
    auto f = citsy::default_font();
    citsy::TextSpan sp;
    sp.text = "A";
    sp.color = 2;
    citsy::TextboxLayout ltr;
    ltr.width = 40;
    ltr.height = 16;
    ltr.rtl = false;
    citsy::TextboxLayout rtl = ltr;
    rtl.rtl = true;
    auto a = citsy::render_textbox(f, {sp}, ltr);
    auto b = citsy::render_textbox(f, {sp}, rtl);
    CHECK(a != b);
}

// ---------------------------------------------------------------------------
// Dialog VM
// ---------------------------------------------------------------------------

TEST_CASE("dialog vm: two quoted pages", "[dialog][phase3]") {
    citsy::DialogVM vm;
    vm.start("\"Hello!\"\n\"Nice to meet you.\"", {});
    REQUIRE(vm.active());
    CHECK(vm.plain_text() == "Hello!");
    REQUIRE(vm.continue_page());
    CHECK(vm.plain_text() == "Nice to meet you.");
    CHECK_FALSE(vm.continue_page());
}

TEST_CASE("dialog vm: {p} splits a string", "[dialog][phase3]") {
    citsy::DialogVM vm;
    vm.start(R"("First{p}Second")", {});
    CHECK(vm.plain_text() == "First");
    REQUIRE(vm.continue_page());
    CHECK(vm.plain_text() == "Second");
}

TEST_CASE("dialog vm: {br} newline", "[dialog][phase3]") {
    citsy::DialogVM vm;
    vm.start(R"("Hello{br}world")", {});
    CHECK(vm.plain_text() == "Hello\nworld");
}

TEST_CASE("dialog vm: assignment and print", "[dialog][phase3]") {
    citsy::DialogVM vm;
    std::unordered_map<std::string, citsy::DialogValue> vars;
    citsy::DialogWorld w;
    w.get_var = [&](std::string_view n) {
        auto it = vars.find(std::string(n));
        return it == vars.end() ? citsy::DialogValue::from_string({}) : it->second;
    };
    w.set_var = [&](std::string_view n, const citsy::DialogValue& v) {
        vars[std::string(n)] = v;
    };
    vm.start("{score = 3}{score = score + 1}\"n={print score}\"", w);
    CHECK(vm.plain_text() == "n=4");
}

TEST_CASE("dialog vm: conditional list", "[dialog][phase3]") {
    citsy::DialogVM vm;
    std::unordered_map<std::string, citsy::DialogValue> vars;
    vars["has"] = citsy::DialogValue::from_number(1);
    citsy::DialogWorld w;
    w.get_var = [&](std::string_view n) {
        auto it = vars.find(std::string(n));
        return it == vars.end() ? citsy::DialogValue::from_number(0) : it->second;
    };
    vm.start("{\n- {has} == 1 ?\n\"yes\"\n- else\n\"no\"\n}", w);
    CHECK(vm.plain_text() == "yes");
}

TEST_CASE("dialog vm: sequence list advances", "[dialog][phase3]") {
    citsy::DialogVM vm;
    vm.start("{\n- \"one\"\n- \"two\"\n}", {}, "seq");
    CHECK(vm.plain_text() == "one");
    vm.continue_page();
    vm.start("{\n- \"one\"\n- \"two\"\n}", {}, "seq");
    CHECK(vm.plain_text() == "two");
}

// ---------------------------------------------------------------------------
// Engine: animation, inventory, endings, variables, transitions, sound
// ---------------------------------------------------------------------------

TEST_CASE("engine: animation frame advances after 400ms", "[engine][phase3][anim]") {
    auto src = game_src("");
    // animated tile in the room? avatar is enough if we add frames.
    citsy::Engine engine(src);
    citsy::MockHost host;
    engine.start(host);
    CHECK(engine.anim_frame() == 0);
    host.dt_ms = 400;
    engine.update(host);
    CHECK(engine.anim_frame() == 1);
}

TEST_CASE("engine: picking up an item increments inventory", "[engine][phase3][inventory]") {
    std::string extra = "ITM 0 5,4\n";
    extra += R"(
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
)";
    auto src = game_src(extra);
    citsy::Engine engine(src);
    citsy::MockHost host;
    engine.start(host);
    CHECK(engine.inventory_count("0") == 0);
    tap(engine, host, citsy::Button::Right);
    CHECK(engine.avatar_x() == 5);
    CHECK(engine.inventory_count("0") == 1);
}

TEST_CASE("engine: variable assignment from dialog", "[engine][phase3]") {
    std::string extra = R"(
SPR 0
00111100
01000010
10100101
10000001
10100101
10011001
01000010
00111100
NAME npc
POS 0 5,4
DLG DLG_V

DLG DLG_V
{score = 7}
"ok"

VAR score
0
)";
    auto src = game_src(extra);
    citsy::Engine engine(src);
    citsy::MockHost host;
    engine.start(host);
    CHECK(engine.variable("score") == "0");
    tap(engine, host, citsy::Button::Right);
    REQUIRE(engine.dialog_active());
    CHECK(engine.dialog_line() == "ok");
    CHECK(engine.variable("score") == "7");
}

TEST_CASE("engine: ending tile ends the game after dismiss", "[engine][phase3][ending]") {
    std::string extra = "END 0 5,4\n\nEND 0\nYou win!\n";
    auto src = game_src(extra);
    citsy::Engine engine(src);
    citsy::MockHost host;
    engine.start(host);
    tap(engine, host, citsy::Button::Right);
    REQUIRE(engine.dialog_active());
    CHECK(engine.dialog_line() == "You win!");
    tap(engine, host, citsy::Button::Ok);
    CHECK(engine.ending_active());
    tap(engine, host, citsy::Button::Ok);
    CHECK_FALSE(engine.is_running());
}

TEST_CASE("engine: room AVA changes appearance", "[engine][phase3]") {
    std::string extra = "AVA 0\n";
    extra += R"(
SPR 0
11111111
10000001
10000001
10000001
10000001
10000001
10000001
11111111
NAME alt
)";
    auto src = game_src(extra);
    citsy::Engine engine(src);
    citsy::MockHost host;
    engine.start(host);
    CHECK(engine.avatar_appearance() == "0");
}

TEST_CASE("engine: fade transition uses video mode", "[engine][phase3][transition]") {
    std::string extra = "EXT 5,4 1 3,3 FX fade_w\n\n";
    extra += "ROOM 1\n";
    extra += kEmptyGrid;
    extra += "PAL 0\n";
    auto src = game_src(extra);
    citsy::Engine engine(src);
    citsy::MockHost host;
    host.dt_ms = 125;
    engine.start(host);
    tap(engine, host, citsy::Button::Right);
    // During / after the first transition step, gfx mode should be Video
    // until the effect finishes.
    bool saw_video = false;
    for (const auto& snap : host.snapshots) {
        if (snap.gfx_mode == citsy::GraphicsMode::Video) saw_video = true;
    }
    CHECK(saw_video);
}

TEST_CASE("engine: blip sets sound channel", "[engine][phase3][sound]") {
    std::string extra = "ITM 0 5,4\n";
    extra += R"(
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
BLIP 1

BLIP 1
C4,E4,0
ENV 5 5 8 20 5
BEAT 10 0
SQR P2
)";
    auto src = game_src(extra);
    citsy::Engine engine(src);
    citsy::MockHost host;
    engine.start(host);
    tap(engine, host, citsy::Button::Right);
    bool heard = false;
    for (const auto& snap : host.snapshots) {
        if (snap.sound1.active) heard = true;
    }
    CHECK(heard);
}

TEST_CASE("engine: {ava} from dialog changes appearance", "[engine][phase3]") {
    std::string extra = R"(
SPR 0
11111111
11111111
11111111
11111111
11111111
11111111
11111111
11111111
NAME alt
POS 0 5,4
DLG DLG_A

DLG DLG_A
{ava 0}
"swap"
)";
    auto src = game_src(extra);
    citsy::Engine engine(src);
    citsy::MockHost host;
    engine.start(host);
    CHECK(engine.avatar_appearance() == "A");
    tap(engine, host, citsy::Button::Right);
    CHECK(engine.avatar_appearance() == "0");
}

TEST_CASE("transition: known effect names", "[transition][phase3]") {
    CHECK(citsy::is_known_transition("fade_w"));
    CHECK(citsy::is_known_transition("slide_l"));
    CHECK(citsy::is_known_transition("wave"));
    CHECK(citsy::is_known_transition("tunnel"));
    CHECK_FALSE(citsy::is_known_transition("explode"));
}

TEST_CASE("sound: pitch middle C is ~262 Hz", "[sound][phase3]") {
    citsy::Pitch p;
    p.beats = 1;
    p.note = 0;
    p.octave = 2;
    const double hz = citsy::pitch_frequency_hz(p);
    CHECK(hz > 250.0);
    CHECK(hz < 270.0);
}

TEST_CASE("parser: first-line title dialog", "[parser][phase3]") {
    constexpr std::string_view src = R"(hello world

# BITSY VERSION 8.15

! ROOM_FORMAT 1
)";
    auto g = citsy::parse(src);
    CHECK(g.title == "hello world");
    CHECK(g.title_dialog == "hello world");
    REQUIRE(g.dialogues.count("title"));
    CHECK(g.dialogues.at("title").content == "hello world");
}

TEST_CASE("parser: legacy ROOM_FORMAT 0 single-char rows", "[parser][phase3]") {
    std::string src = "! ROOM_FORMAT 0\nROOM 0\n";
    src += std::string(16, '0') + "\n";
    for (int i = 1; i < 16; ++i) src += std::string(16, '0') + "\n";
    auto g = citsy::parse(src);
    REQUIRE(g.rooms.count("0"));
    CHECK(g.rooms.at("0").tiles[0][0] == "0");
    CHECK(g.rooms.at("0").tiles[0][15] == "0");
}

TEST_CASE("dialog vm: {item} sets inventory", "[dialog][phase3][inventory]") {
    citsy::DialogVM vm;
    std::unordered_map<std::string, int> inv;
    inv["key"] = 1;
    citsy::DialogWorld w;
    w.item_count = [&](std::string_view id) {
        auto it = inv.find(std::string(id));
        return it == inv.end() ? 0 : it->second;
    };
    w.set_item = [&](std::string_view id, int n) { inv[std::string(id)] = n; };
    vm.start(R"({item "key" 0}"gone")", w);
    CHECK(vm.plain_text() == "gone");
    CHECK(inv["key"] == 0);
}

TEST_CASE("dialog vm: {property locked} round-trip", "[dialog][phase3]") {
    citsy::DialogVM vm;
    bool locked = false;
    citsy::DialogWorld w;
    w.get_property = [&](std::string_view name) {
        CHECK(name == "locked");
        return citsy::DialogValue::from_number(locked ? 1 : 0);
    };
    w.set_property = [&](std::string_view name, const citsy::DialogValue& v) {
        CHECK(name == "locked");
        locked = v.is_truthy();
    };
    vm.start("{property locked true}\"ok\"", w);
    CHECK(locked);
    vm.start("{property locked false}\"ok\"", w);
    CHECK_FALSE(locked);
}

TEST_CASE("engine: title dialog shows at start", "[engine][phase3]") {
    std::string src = "my title\n\n";
    src += game_src("");
    citsy::Engine engine(src);
    citsy::MockHost host;
    engine.start(host);
    engine.update(host);
    REQUIRE(engine.dialog_active());
    CHECK(engine.dialog_line() == "my title");
    tap(engine, host, citsy::Button::Ok);
    CHECK_FALSE(engine.dialog_active());
}

TEST_CASE("engine: {item} by name and locked exit", "[engine][phase3][inventory]") {
    std::string extra = "ITM 0 3,4\n";
    extra += "EXT 5,4 1 3,3 DLG DLG_L\n\n";
    extra += "ROOM 1\n";
    extra += kEmptyGrid;
    extra += "PAL 0\n";
    extra += R"(
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

DLG DLG_L
{
- {item "key"} > 0 ?
  {item "key" 0}
  {property locked false}
  "opened"
- else
  {property locked true}
  "locked"
}
)";
    auto src = game_src(extra);
    citsy::Engine engine(src);
    citsy::MockHost host;
    engine.start(host);

    // No key yet: exit is locked.
    tap(engine, host, citsy::Button::Right);
    REQUIRE(engine.dialog_active());
    CHECK(engine.dialog_line() == "locked");
    tap(engine, host, citsy::Button::Ok);
    CHECK(engine.current_room_id() == "0");
    CHECK(engine.avatar_x() == 5);

    // Back up, pick up key, try exit again.
    tap(engine, host, citsy::Button::Left);
    tap(engine, host, citsy::Button::Left);
    CHECK(engine.inventory_count("0") == 1);
    tap(engine, host, citsy::Button::Right);
    tap(engine, host, citsy::Button::Right);
    REQUIRE(engine.dialog_active());
    CHECK(engine.dialog_line() == "opened");
    tap(engine, host, citsy::Button::Ok);
    CHECK(engine.current_room_id() == "1");
    CHECK(engine.inventory_count("0") == 0);
}

TEST_CASE("fixture: phase3.bitsy parses 8.15 extras", "[fixture][phase3]") {
    namespace fs = std::filesystem;
    fs::path here = fs::path(__FILE__).parent_path();
    fs::path data = here.parent_path() / "data" / "phase3.bitsy";
    std::ifstream f(data);
    REQUIRE(f.is_open());
    std::ostringstream buf;
    buf << f.rdbuf();
    auto g = citsy::parse(buf.str());
    CHECK(g.version.major == 8);
    CHECK(g.version.minor == 15);
    REQUIRE(g.tunes.count("1"));
    REQUIRE(g.blips.count("1"));
    CHECK(g.rooms.at("0").tune_id == "1");
    CHECK(g.rooms.at("0").exits[0].transition_effect == "fade_w");
    CHECK(g.items.at("0").blip_id == "1");
}
