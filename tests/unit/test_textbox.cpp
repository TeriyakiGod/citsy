#include <catch2/catch_test_macros.hpp>

#include <citsy/engine.hpp>
#include <citsy/types.hpp>
#include "backends/mock/mock_host.hpp"
#include "src/dialog/script.hpp"
#include "src/font/font.hpp"

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

namespace {

citsy::TextboxLayout box(int w = 104, int h = 32) {
    citsy::TextboxLayout l;
    l.width = w;
    l.height = h;
    l.margin = 2;
    return l;
}

citsy::TextSpan span(std::string text, std::uint8_t fx = citsy::GlyphFx::None,
                     int color = -1) {
    citsy::TextSpan s;
    s.text = std::move(text);
    s.effects = fx;
    s.color = color;
    return s;
}

int count_eq(const std::vector<std::uint8_t>& pix, std::uint8_t v) {
    return static_cast<int>(std::count(pix.begin(), pix.end(), v));
}

bool is_rainbow(std::uint8_t p) {
    return p >= citsy::kTextboxRainbow0 &&
           p < citsy::kTextboxRainbow0 + citsy::kTextboxRainbowCount;
}

int rainbow_ink(const std::vector<std::uint8_t>& pix) {
    int n = 0;
    for (auto p : pix) if (is_rainbow(p)) ++n;
    return n;
}

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

std::string game_src(std::string extra, std::string title = {}) {
    std::string s;
    if (!title.empty()) s += title + "\n\n";
    s += "# BITSY VERSION 8.15\n\n! ROOM_FORMAT 1\n\n";
    s += std::string(kPal) + "\nROOM 0\n" + std::string(kEmptyGrid);
    s += "PAL 0\n" + extra + "\n" + std::string(kAvatar);
    return s;
}

void tap(citsy::Engine& e, citsy::MockHost& h, citsy::Button b) {
    h.set_button(b, true);
    e.update(h);
    h.set_button(b, false);
    e.update(h);
}

void dismiss_dialog(citsy::Engine& e, citsy::MockHost& h) {
    int guard = 32;
    while (e.dialog_active() && guard--) tap(e, h, citsy::Button::Ok);
    REQUIRE_FALSE(e.dialog_active());
}

} // namespace

// ---------------------------------------------------------------------------
// Background and default ink
// ---------------------------------------------------------------------------

TEST_CASE("textbox: background is always black", "[font][textbox]") {
    auto f = citsy::default_font();
    auto pix = citsy::render_textbox(f, {span("Hi")}, box());
    REQUIRE(pix.size() == 104u * 32u);
    for (auto p : pix) {
        CHECK((p == citsy::kTextboxBlack || p == citsy::kTextboxWhite));
    }
    CHECK(count_eq(pix, citsy::kTextboxBlack) > 1000);
    CHECK(pix.front() == citsy::kTextboxBlack);
    CHECK(pix.back() == citsy::kTextboxBlack);
}

TEST_CASE("textbox: default ink is white", "[font][textbox]") {
    auto f = citsy::default_font();
    auto pix = citsy::render_textbox(f, {span("ABC")}, box());
    CHECK(count_eq(pix, citsy::kTextboxWhite) > 20);
    CHECK(count_eq(pix, 2) == 0);
}

TEST_CASE("textbox: empty box is solid black", "[font][textbox]") {
    auto f = citsy::default_font();
    auto pix = citsy::render_textbox(f, {}, box(20, 12));
    REQUIRE(pix.size() == 20u * 12u);
    CHECK(count_eq(pix, citsy::kTextboxBlack) == 20 * 12);
}

TEST_CASE("textbox: {clr} uses the given palette index", "[font][textbox]") {
    auto f = citsy::default_font();
    auto pix = citsy::render_textbox(f, {span("Hi", citsy::GlyphFx::None, 1)}, box());
    CHECK(count_eq(pix, 1) > 10);
    CHECK(count_eq(pix, citsy::kTextboxWhite) == 0);
}

TEST_CASE("textbox: continue arrow is white", "[font][textbox]") {
    auto f = citsy::default_font();
    auto layout = box();
    layout.show_arrow = true;
    auto pix = citsy::render_textbox(f, {}, layout);
    CHECK(count_eq(pix, citsy::kTextboxWhite) >= 4);
}

// ---------------------------------------------------------------------------
// Rainbow
// ---------------------------------------------------------------------------

TEST_CASE("textbox: rainbow uses reserved hue indices", "[font][textbox][rbw]") {
    auto f = citsy::default_font();
    auto pix = citsy::render_textbox(
        f, {span("RAINBOW TEXT", citsy::GlyphFx::Rainbow)}, box());
    CHECK(rainbow_ink(pix) > 30);
    CHECK(count_eq(pix, citsy::kTextboxWhite) == 0);
}

TEST_CASE("textbox: rainbow is uniform within each character", "[font][textbox][rbw]") {
    auto f = citsy::default_font();
    auto pix = citsy::render_textbox(
        f, {span("W", citsy::GlyphFx::Rainbow)}, box());
    const int w = 104;
    std::uint8_t glyph_hue = 0;
    int hue_count = 0;
    for (int y = 0; y < 32; ++y) {
        for (int x = 0; x < w; ++x) {
            const auto p = pix[static_cast<std::size_t>(y * w + x)];
            if (!is_rainbow(p)) continue;
            if (hue_count == 0) glyph_hue = p;
            else CHECK(p == glyph_hue);
            ++hue_count;
        }
    }
    CHECK(hue_count > 1);
}

TEST_CASE("textbox: rainbow varies across characters", "[font][textbox][rbw]") {
    auto f = citsy::default_font();
    auto pix = citsy::render_textbox(
        f, {span("MMMMMMMM", citsy::GlyphFx::Rainbow)}, box());
    const int w = 104;
    const int h = 32;
    std::uint8_t left = 0, right = 0;
    int left_x = w, right_x = -1;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const auto p = pix[static_cast<std::size_t>(y * w + x)];
            if (!is_rainbow(p)) continue;
            if (x < left_x) { left_x = x; left = p; }
            if (x > right_x) { right_x = x; right = p; }
        }
    }
    REQUIRE(right_x > left_x);
    CHECK(left != right);
}

TEST_CASE("textbox: rainbow scrolls right over time", "[font][textbox][rbw]") {
    CHECK(citsy::rainbow_index(0, 0) != citsy::rainbow_index(1, 0));
    CHECK(citsy::rainbow_index(0, 0) ==
          citsy::rainbow_index(2, citsy::kTextboxRainbowTimeMs));
    CHECK(citsy::rainbow_index(0, 0) !=
          citsy::rainbow_index(0, citsy::kTextboxRainbowTimeMs));

    auto f = citsy::default_font();
    auto a_layout = box();
    a_layout.time_ms = 0;
    auto b_layout = box();
    b_layout.time_ms = 200;
    const auto sp = span("GRADIENT", citsy::GlyphFx::Rainbow);
    auto a = citsy::render_textbox(f, {sp}, a_layout);
    auto b = citsy::render_textbox(f, {sp}, b_layout);
    CHECK(a != b);
}

TEST_CASE("install_textbox_colors fills reserved slots", "[font][textbox]") {
    std::vector<citsy::Color> pal{{10, 20, 30}, {40, 50, 60}, {70, 80, 90}};
    citsy::install_textbox_colors(pal);
    REQUIRE(pal.size() >= 256);
    CHECK(pal[0].r == 10);
    CHECK(pal[citsy::kTextboxBlack] == citsy::Color{0, 0, 0});
    CHECK(pal[citsy::kTextboxWhite] == citsy::Color{255, 255, 255});
    CHECK(pal[citsy::kTextboxRainbow0] == citsy::Color{128, 237, 18});
    CHECK(pal[citsy::kTextboxRainbow0 + 8] == citsy::Color{128, 18, 237});
    CHECK(citsy::rainbow_index(0, 0) == citsy::kTextboxRainbow0);
}

// ---------------------------------------------------------------------------
// Combined effects
// ---------------------------------------------------------------------------

TEST_CASE("textbox: wavy and rainbow apply together", "[font][textbox][fx]") {
    auto f = citsy::default_font();
    const auto both = static_cast<std::uint8_t>(
        citsy::GlyphFx::Wavy | citsy::GlyphFx::Rainbow);
    auto layout = box();
    layout.time_ms = 80;
    auto rainbow = citsy::render_textbox(
        f, {span("WAVYRBW", citsy::GlyphFx::Rainbow)}, layout);
    auto combo = citsy::render_textbox(f, {span("WAVYRBW", both)}, layout);
    CHECK(rainbow != combo);
    CHECK(rainbow_ink(combo) > 20);
}

TEST_CASE("textbox: shaky wavy and rainbow apply together", "[font][textbox][fx]") {
    auto f = citsy::default_font();
    const auto all = static_cast<std::uint8_t>(
        citsy::GlyphFx::Wavy | citsy::GlyphFx::Shaky | citsy::GlyphFx::Rainbow);
    auto layout = box();
    layout.time_ms = 120;
    auto base = citsy::render_textbox(f, {span("SHAKE")}, layout);
    auto combo = citsy::render_textbox(f, {span("SHAKE", all)}, layout);
    CHECK(base != combo);
    CHECK(rainbow_ink(combo) > 10);
    CHECK(count_eq(combo, citsy::kTextboxWhite) == 0);
}

TEST_CASE("textbox: rainbow overrides {clr} when both are on", "[font][textbox][fx]") {
    auto f = citsy::default_font();
    auto pix = citsy::render_textbox(
        f, {span("Hi", citsy::GlyphFx::Rainbow, 1)}, box());
    CHECK(count_eq(pix, 1) == 0);
    CHECK(rainbow_ink(pix) > 10);
}

TEST_CASE("dialog vm: stacked effect tags combine", "[dialog][textbox][fx]") {
    citsy::DialogVM vm;
    vm.start("\"{wvy}{rbw}Hi{/rbw}{/wvy}\"", {});
    REQUIRE_FALSE(vm.spans().empty());
    CHECK(vm.spans()[0].text == "Hi");
    CHECK(vm.spans()[0].effects ==
          (citsy::GlyphFx::Wavy | citsy::GlyphFx::Rainbow));
    CHECK(vm.spans()[0].color == -1);
}

TEST_CASE("dialog vm: later tag adds an effect without dropping the first",
          "[dialog][textbox][fx]") {
    citsy::DialogVM vm;
    vm.start("\"{wvy}A{shk}B{/shk}C{/wvy}D\"", {});
    REQUIRE(vm.spans().size() >= 4);
    CHECK(vm.spans()[0].text == "A");
    CHECK(vm.spans()[0].effects == citsy::GlyphFx::Wavy);
    CHECK(vm.spans()[1].text == "B");
    CHECK(vm.spans()[1].effects == (citsy::GlyphFx::Wavy | citsy::GlyphFx::Shaky));
    CHECK(vm.spans()[2].text == "C");
    CHECK(vm.spans()[2].effects == citsy::GlyphFx::Wavy);
    CHECK(vm.spans()[3].text == "D");
    CHECK(vm.spans()[3].effects == citsy::GlyphFx::None);
}

TEST_CASE("dialog vm: default color is unset (white) until {clr}",
          "[dialog][textbox]") {
    citsy::DialogVM vm;
    vm.start("\"plain{clr1}ink{/clr1}plain\"", {});
    REQUIRE(vm.spans().size() >= 3);
    CHECK(vm.spans()[0].color == -1);
    CHECK(vm.spans()[1].color == 1);
    CHECK(vm.spans()[2].color == -1);
}

TEST_CASE("dialog vm: {rbw} and {clr2} can stack", "[dialog][textbox][fx]") {
    citsy::DialogVM vm;
    vm.start("\"{rbw}{clr2}X\"", {});
    REQUIRE_FALSE(vm.spans().empty());
    CHECK(vm.spans()[0].effects == citsy::GlyphFx::Rainbow);
    CHECK(vm.spans()[0].color == 2);
}

// ---------------------------------------------------------------------------
// Word wrap
// ---------------------------------------------------------------------------

int ink_on_row_range(const std::vector<std::uint8_t>& pix, int w, int y0, int y1) {
    int n = 0;
    for (int y = y0; y < y1; ++y) {
        for (int x = 0; x < w; ++x) {
            if (pix[static_cast<std::size_t>(y * w + x)] == citsy::kTextboxWhite) ++n;
        }
    }
    return n;
}

TEST_CASE("textbox: whole words wrap to the next row", "[font][textbox][wrap]") {
    auto f = citsy::default_font();
    auto layout = box(53, 32);  // 8 glyphs / row
    auto pages = citsy::paginate_spans(f, {span("HELLO WORLD")}, layout);
    REQUIRE(pages.size() == 1);
    CHECK(citsy::spans_to_plain(pages[0]) == "HELLO WORLD");
    auto pix = citsy::render_textbox(f, pages[0], layout);
    CHECK(ink_on_row_range(pix, 53, 2, 10) > 0);
    CHECK(ink_on_row_range(pix, 53, 11, 19) > 0);
    // Character wrap would paint "WO" on row 1 at x≈38. Word wrap keeps WORLD
    // intact on row 2, so that cell on row 1 stays empty.
    bool split_on_row1 = false;
    for (int y = 2; y < 10; ++y) {
        for (int x = 38; x < 50; ++x) {
            if (pix[static_cast<std::size_t>(y * 53 + x)] == citsy::kTextboxWhite)
                split_on_row1 = true;
        }
    }
    CHECK_FALSE(split_on_row1);
}

TEST_CASE("textbox: leftover whole words go to the next screen",
          "[font][textbox][wrap]") {
    auto f = citsy::default_font();
    auto layout = box(53, 32);
    auto pages = citsy::paginate_spans(f, {span("HELLO WORLD AGAIN TODAY")}, layout);
    REQUIRE(pages.size() == 2);
    CHECK(citsy::spans_to_plain(pages[0]).find("WORLD") != std::string::npos);
    CHECK(citsy::spans_to_plain(pages[0]).find("AGAIN") != std::string::npos);
    CHECK(citsy::spans_to_plain(pages[0]).find("TODAY") == std::string::npos);
    CHECK(citsy::spans_to_plain(pages[1]).find("TODAY") != std::string::npos);
}

TEST_CASE("textbox: {br} still uses the next row of the same screen",
          "[font][textbox][wrap]") {
    auto f = citsy::default_font();
    auto pages = citsy::paginate_spans(f, {span("HI\nTHERE")}, box());
    REQUIRE(pages.size() == 1);
    auto pix = citsy::render_textbox(f, pages[0], box());
    CHECK(ink_on_row_range(pix, 104, 2, 10) > 0);
    CHECK(ink_on_row_range(pix, 104, 11, 19) > 0);
}

// ---------------------------------------------------------------------------
// Pagination
// ---------------------------------------------------------------------------

TEST_CASE("paginate: extra lines become another screen", "[font][textbox][page]") {
    auto f = citsy::default_font();
    auto pages = citsy::paginate_spans(f, {span("A\nB\nC\nD")}, box());
    REQUIRE(pages.size() == 2);
    CHECK(citsy::spans_to_plain(pages[0]).find('D') == std::string::npos);
    CHECK(citsy::spans_to_plain(pages[1]).find('D') != std::string::npos);
}

TEST_CASE("paginate: an unbreakable run uses extra rows then another screen",
          "[font][textbox][page]") {
    auto f = citsy::default_font();
    auto pages = citsy::paginate_spans(f, {span(std::string(80, 'A'))}, box());
    REQUIRE(pages.size() >= 2);
    std::size_t total = 0;
    for (const auto& page : pages) {
        total += citsy::spans_to_plain(page).size();
    }
    CHECK(total == 80);
}

TEST_CASE("paginate: leftover words become another screen", "[font][textbox][page]") {
    auto f = citsy::default_font();
    auto layout = box(53, 32);
    auto pages = citsy::paginate_spans(
        f, {span("one two three four five six seven")}, layout);
    REQUIRE(pages.size() >= 2);
    CHECK(citsy::spans_to_plain(pages[0]).find("seven") == std::string::npos);
    CHECK(citsy::spans_to_plain(pages.back()).find("seven") != std::string::npos);
}

TEST_CASE("paginate: short dialog stays on one screen", "[font][textbox][page]") {
    auto f = citsy::default_font();
    auto pages = citsy::paginate_spans(f, {span("Hi")}, box());
    REQUIRE(pages.size() == 1);
    CHECK(citsy::spans_to_plain(pages[0]) == "Hi");
}

TEST_CASE("paginate: preserves effects across a page break",
          "[font][textbox][page]") {
    auto f = citsy::default_font();
    auto pages = citsy::paginate_spans(
        f, {span(std::string(80, 'A'), citsy::GlyphFx::Rainbow | citsy::GlyphFx::Wavy)},
        box());
    REQUIRE(pages.size() >= 2);
    for (const auto& page : pages) {
        REQUIRE_FALSE(page.empty());
        CHECK(page.front().effects ==
              (citsy::GlyphFx::Rainbow | citsy::GlyphFx::Wavy));
    }
}

// ---------------------------------------------------------------------------
// Engine integration
// ---------------------------------------------------------------------------

TEST_CASE("engine: long dialog splits across screens", "[engine][textbox][page]") {
    const std::string title(80, 'A');
    citsy::Engine engine(game_src("", title));
    citsy::MockHost host;
    engine.start(host);
    engine.update(host);
    REQUIRE(engine.dialog_active());
    std::string seen;
    int screens = 0;
    while (engine.dialog_active()) {
        const auto chunk = std::string(engine.dialog_line());
        REQUIRE_FALSE(chunk.empty());
        REQUIRE(chunk.find('\n') == std::string::npos);
        seen += chunk;
        ++screens;
        tap(engine, host, citsy::Button::Ok);
        REQUIRE(screens < 20);
    }
    CHECK(seen == title);
    CHECK(screens >= 2);
}

TEST_CASE("engine: short dialog still dismisses on one Ok",
          "[engine][textbox][page]") {
    citsy::Engine engine(game_src("", "hi"));
    citsy::MockHost host;
    engine.start(host);
    engine.update(host);
    REQUIRE(engine.dialog_active());
    CHECK(engine.dialog_line() == "hi");
    tap(engine, host, citsy::Button::Ok);
    CHECK_FALSE(engine.dialog_active());
}

TEST_CASE("engine: {p} still starts a new dialog page after screens",
          "[engine][textbox][page]") {
    citsy::Engine engine(game_src("", "\"one{p}two\""));
    citsy::MockHost host;
    engine.start(host);
    engine.update(host);
    REQUIRE(engine.dialog_active());
    CHECK(engine.dialog_line() == "one");
    tap(engine, host, citsy::Button::Ok);
    REQUIRE(engine.dialog_active());
    CHECK(engine.dialog_line() == "two");
    tap(engine, host, citsy::Button::Ok);
    CHECK_FALSE(engine.dialog_active());
}

TEST_CASE("engine: open dialog installs black white and rainbow palette slots",
          "[engine][textbox]") {
    citsy::Engine engine(game_src("", "hello"));
    citsy::MockHost host;
    engine.start(host);
    engine.update(host);
    auto* snap = host.last_snapshot();
    REQUIRE(snap != nullptr);
    REQUIRE(snap->textbox_visible);
    REQUIRE(snap->palette.size() >= 256);
    CHECK(snap->palette[citsy::kTextboxBlack] == citsy::Color{0, 0, 0});
    CHECK(snap->palette[citsy::kTextboxWhite] == citsy::Color{255, 255, 255});
    CHECK(snap->palette[citsy::kTextboxRainbow0] == citsy::Color{128, 237, 18});
    CHECK(count_eq(snap->textbox_pixels, citsy::kTextboxBlack) > 100);
    CHECK(count_eq(snap->textbox_pixels, citsy::kTextboxWhite) > 10);
}

TEST_CASE("engine: rainbow dialog paints reserved indices",
          "[engine][textbox][rbw]") {
    citsy::Engine engine(game_src("", "\"{rbw}RAINBOW TEXT\""));
    citsy::MockHost host;
    engine.start(host);
    engine.update(host);
    auto* snap = host.last_snapshot();
    REQUIRE(snap != nullptr);
    CHECK(rainbow_ink(snap->textbox_pixels) > 20);
    CHECK(count_eq(snap->textbox_pixels, citsy::kTextboxWhite) <= 4);
}

TEST_CASE("engine: rainbow animation changes pixels over time",
          "[engine][textbox][rbw]") {
    citsy::Engine engine(game_src("", "\"{rbw}RAINBOW\""));
    citsy::MockHost host;
    host.dt_ms = 50;
    engine.start(host);
    engine.update(host);
    auto* first = host.last_snapshot();
    REQUIRE(first != nullptr);
    const auto a = first->textbox_pixels;
    for (int i = 0; i < 4; ++i) engine.update(host);
    auto* later = host.last_snapshot();
    REQUIRE(later != nullptr);
    CHECK(later->textbox_pixels != a);
}

TEST_CASE("engine: cycle dialog advances on each visit, including questions",
          "[engine][textbox][dialog]") {
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
DLG SPR_4

DLG SPR_4
"""
{cycle
  - one
  - two
  - survive?
}
"""
)";
    citsy::Engine engine(game_src(extra));
    citsy::MockHost host;
    engine.start(host);

    tap(engine, host, citsy::Button::Right);
    REQUIRE(engine.dialog_active());
    CHECK(engine.dialog_line() == "one");
    dismiss_dialog(engine, host);

    tap(engine, host, citsy::Button::Right);
    REQUIRE(engine.dialog_active());
    CHECK(engine.dialog_line() == "two");
    dismiss_dialog(engine, host);

    tap(engine, host, citsy::Button::Right);
    REQUIRE(engine.dialog_active());
    CHECK(engine.dialog_line() == "survive?");
    dismiss_dialog(engine, host);

    tap(engine, host, citsy::Button::Right);
    REQUIRE(engine.dialog_active());
    CHECK(engine.dialog_line() == "one");
    dismiss_dialog(engine, host);
}

TEST_CASE("engine: sequence with nested item branch", "[engine][textbox][dialog]") {
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
NAME gardener
POS 0 5,4
DLG SPR_1

ITM 0
00000000
00011000
00111100
01111110
01111110
00111100
00011000
00000000
NAME Can

DLG SPR_1
"""
{sequence
  - I tend the moss
    
    would you help me {clr3}water {printItem "Can"}{clr3} the spores?
  - next
  - {
      - {item "Spores"} < 6 ?
        the spores need watering
      - else ?
        thanks for your help - you did a {wvy}moss-some{wvy} job
    }
}
"""
)";
    citsy::Engine engine(game_src(extra));
    citsy::MockHost host;
    engine.start(host);

    tap(engine, host, citsy::Button::Right);
    REQUIRE(engine.dialog_active());
    CHECK(engine.dialog_line() == "I tend the moss");
    tap(engine, host, citsy::Button::Ok);
    REQUIRE(engine.dialog_active());
    CHECK(std::string(engine.dialog_line()).find("would you help me") != std::string::npos);
    dismiss_dialog(engine, host);

    tap(engine, host, citsy::Button::Right);
    REQUIRE(engine.dialog_active());
    CHECK(engine.dialog_line() == "next");
    dismiss_dialog(engine, host);

    tap(engine, host, citsy::Button::Right);
    REQUIRE(engine.dialog_active());
    CHECK(engine.dialog_line() == "the spores need watering");
    dismiss_dialog(engine, host);
}
