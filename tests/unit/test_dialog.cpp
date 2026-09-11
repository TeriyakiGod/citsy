#include <catch2/catch_test_macros.hpp>

#include "src/dialog/linear.hpp"
#include "src/dialog/script.hpp"

#include <algorithm>
#include <map>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace {

struct TestWorld {
    std::map<std::string, citsy::Value> vars;
    std::unordered_map<std::string, int> items;

    citsy::DialogWorld bind() {
        citsy::DialogWorld w;
        w.get_var = [this](std::string_view name) {
            auto it = vars.find(std::string(name));
            if (it == vars.end()) return citsy::Value::from_number(0);
            return it->second;
        };
        w.set_var = [this](std::string_view name, const citsy::DialogValue& v) {
            vars[std::string(name)] = v;
        };
        w.item_count = [this](std::string_view id) {
            auto it = items.find(std::string(id));
            return it == items.end() ? 0 : it->second;
        };
        w.set_item = [this](std::string_view id, int count) {
            items[std::string(id)] = count;
        };
        return w;
    }
};

citsy::DialogResult run(std::string_view src, TestWorld& tw) {
    auto script = citsy::parse_dialog_script(src);
    return citsy::run_dialog_script(script, tw.bind());
}

} // namespace

TEST_CASE("dialog: single quoted line", "[dialog]") {
    auto pages = citsy::extract_dialog_pages(R"("Hello, world!")");
    REQUIRE(pages.size() == 1);
    CHECK(pages[0] == "Hello, world!");
}

TEST_CASE("dialog: each quoted string is a page", "[dialog]") {
    auto pages = citsy::extract_dialog_pages("\"Hello!\"\n\"Nice to meet you.\"");
    REQUIRE(pages.size() == 2);
    CHECK(pages[0] == "Hello!");
    CHECK(pages[1] == "Nice to meet you.");
}

TEST_CASE("dialog: {p} splits a page", "[dialog]") {
    auto pages = citsy::extract_dialog_pages(R"("First{p}Second")");
    REQUIRE(pages.size() == 2);
    CHECK(pages[0] == "First");
    CHECK(pages[1] == "Second");
}

TEST_CASE("dialog: {br} becomes a newline", "[dialog]") {
    auto pages = citsy::extract_dialog_pages(R"("Hello{br}world")");
    REQUIRE(pages.size() == 1);
    CHECK(pages[0] == "Hello\nworld");
}

TEST_CASE("dialog: visual {tags} are omitted", "[dialog]") {
    auto pages = citsy::extract_dialog_pages(R"("Hi {wvy} there")");
    REQUIRE(pages.size() == 1);
    CHECK(pages[0] == "Hi  there");
}

TEST_CASE("dialog: triple-quoted block", "[dialog]") {
    auto pages = citsy::extract_dialog_pages("\"\"\"\nline one\nline two\n\"\"\"");
    REQUIRE(pages.size() == 1);
    CHECK(pages[0] == "line one\nline two");
}

TEST_CASE("dialog: unquoted fallback", "[dialog]") {
    auto pages = citsy::extract_dialog_pages("You found the treasure!");
    REQUIRE(pages.size() == 1);
    CHECK(pages[0] == "You found the treasure!");
}

TEST_CASE("dialog: empty source yields no pages", "[dialog]") {
    CHECK(citsy::extract_dialog_pages("").empty());
    CHECK(citsy::extract_dialog_pages("   \n").empty());
}

TEST_CASE("dialog: print interpolates a variable", "[dialog][script]") {
    TestWorld w;
    w.vars["name"] = citsy::Value::from_string("ada");
    auto r = run(R"("Hello {print name}!")", w);
    REQUIRE(r.pages.size() == 1);
    CHECK(r.pages[0] == "Hello ada!");
}

TEST_CASE("dialog: assignment updates a variable", "[dialog][script]") {
    TestWorld w;
    w.vars["score"] = citsy::Value::from_number(0);
    auto r = run("{score = 5}\"{print score}\"", w);
    REQUIRE(r.pages.size() == 1);
    CHECK(r.pages[0] == "5");
    CHECK(w.vars["score"].as_number() == 5);
}

TEST_CASE("dialog: arithmetic assignment", "[dialog][script]") {
    TestWorld w;
    w.vars["n"] = citsy::Value::from_number(2);
    run("{n = n + 1}", w);
    CHECK(w.vars["n"].as_number() == 3);
}

TEST_CASE("dialog: branching list picks the matching arm", "[dialog][script]") {
    TestWorld w;
    w.vars["score"] = citsy::Value::from_number(1);
    auto r = run(R"({
  - score == 1 ?
    "one"
  - else ?
    "other"
})", w);
    REQUIRE(r.pages.size() == 1);
    CHECK(r.pages[0] == "one");

    w.vars["score"] = citsy::Value::from_number(0);
    auto script = citsy::parse_dialog_script(R"({
  - score == 1 ?
    "one"
  - else ?
    "other"
})");
    auto r2 = citsy::run_dialog_script(script, w.bind());
    REQUIRE(r2.pages.size() == 1);
    CHECK(r2.pages[0] == "other");
}

TEST_CASE("dialog: item get and set", "[dialog][script]") {
    TestWorld w;
    w.items["0"] = 2;
    auto r = run(R"("have {print {item "0"}}")", w);
    REQUIRE(r.pages.size() == 1);
    CHECK(r.pages[0] == "have 2");

    run(R"({item "0" 5})", w);
    CHECK(w.items["0"] == 5);

    run(R"({item "0" {{item "0"} - 1}})", w);
    CHECK(w.items["0"] == 4);
}

TEST_CASE("dialog: sequence advances then sticks on last", "[dialog][script]") {
    TestWorld w;
    auto script = citsy::parse_dialog_script(R"({sequence
  - "a"
  - "b"
  - "c"
})");
    CHECK(citsy::run_dialog_script(script, w.bind()).pages == std::vector<std::string>{"a"});
    CHECK(citsy::run_dialog_script(script, w.bind()).pages == std::vector<std::string>{"b"});
    CHECK(citsy::run_dialog_script(script, w.bind()).pages == std::vector<std::string>{"c"});
    CHECK(citsy::run_dialog_script(script, w.bind()).pages == std::vector<std::string>{"c"});
}

TEST_CASE("dialog: cycle wraps around", "[dialog][script]") {
    TestWorld w;
    auto script = citsy::parse_dialog_script(R"({cycle
  - "a"
  - "b"
})");
    CHECK(citsy::run_dialog_script(script, w.bind()).pages == std::vector<std::string>{"a"});
    CHECK(citsy::run_dialog_script(script, w.bind()).pages == std::vector<std::string>{"b"});
    CHECK(citsy::run_dialog_script(script, w.bind()).pages == std::vector<std::string>{"a"});
}

TEST_CASE("dialog: cycle items may end with a question mark", "[dialog][script]") {
    TestWorld w;
    auto script = citsy::parse_dialog_script(R"({cycle
  - this bottle world has everything we need
  - soil, water, air, light
  - if we left, could we survive?
  - sometimes I wish I could find a crack in the glass
})");
    CHECK(citsy::run_dialog_script(script, w.bind()).pages ==
          std::vector<std::string>{"this bottle world has everything we need"});
    CHECK(citsy::run_dialog_script(script, w.bind()).pages ==
          std::vector<std::string>{"soil, water, air, light"});
    CHECK(citsy::run_dialog_script(script, w.bind()).pages ==
          std::vector<std::string>{"if we left, could we survive?"});
    CHECK(citsy::run_dialog_script(script, w.bind()).pages ==
          std::vector<std::string>{"sometimes I wish I could find a crack in the glass"});
    CHECK(citsy::run_dialog_script(script, w.bind()).pages ==
          std::vector<std::string>{"this bottle world has everything we need"});
}

TEST_CASE("dialog: shuffle visits every item before repeating", "[dialog][script]") {
    TestWorld w;
    auto script = citsy::parse_dialog_script(R"({shuffle
  - "a"
  - "b"
  - "c"
})");
    std::vector<std::string> seen;
    for (int i = 0; i < 3; ++i) {
        auto r = citsy::run_dialog_script(script, w.bind());
        REQUIRE(r.pages.size() == 1);
        seen.push_back(r.pages[0]);
    }
    std::sort(seen.begin(), seen.end());
    CHECK(seen == std::vector<std::string>{"a", "b", "c"});
}

TEST_CASE("dialog: {end} flags the game over", "[dialog][script]") {
    TestWorld w;
    auto r = run(R"("bye"{end})", w);
    REQUIRE(r.pages.size() == 1);
    CHECK(r.pages[0] == "bye");
    CHECK(r.end_game);
}

TEST_CASE("dialog: {exit} queues a warp", "[dialog][script]") {
    TestWorld w;
    auto r = run(R"({exit "1" 3 4})", w);
    REQUIRE(r.exit.has_value());
    CHECK(r.exit->room_id == "1");
    CHECK(r.exit->x == 3);
    CHECK(r.exit->y == 4);
}

TEST_CASE("dialog: blank line in a list item is a page break", "[dialog][script]") {
    TestWorld w;
    auto r = run(R"({sequence
  - hello

    world
})", w);
    REQUIRE(r.pages.size() == 2);
    CHECK(r.pages[0] == "hello");
    CHECK(r.pages[1] == "world");
}

TEST_CASE("dialog: item comparison in a nested branch", "[dialog][script]") {
    TestWorld w;
    w.items["Spores"] = 2;
    auto src = R"({
      - {item "Spores"} < 6 ?
        need water
      - else ?
        thanks
    })";
    auto r = run(src, w);
    REQUIRE(r.pages.size() == 1);
    CHECK(r.pages[0] == "need water");

    w.items["Spores"] = 6;
    auto script = citsy::parse_dialog_script(src);
    auto r2 = citsy::run_dialog_script(script, w.bind());
    REQUIRE(r2.pages.size() == 1);
    CHECK(r2.pages[0] == "thanks");
}

TEST_CASE("dialog: single newline in a list item is a line break", "[dialog][script]") {
    TestWorld w;
    auto r = run(R"({sequence
  - hello
    world
})", w);
    REQUIRE(r.pages.size() == 1);
    CHECK(r.pages[0] == "hello\nworld");
}

TEST_CASE("dialog: nested branch inside sequence is one arm", "[dialog][script]") {
    TestWorld w;
    w.items["Spores"] = 2;
    auto script = citsy::parse_dialog_script(R"({sequence
  - first
  - {
      - {item "Spores"} < 6 ?
        need water
      - else ?
        thanks
    }
})");
    CHECK(citsy::run_dialog_script(script, w.bind()).pages ==
          std::vector<std::string>{"first"});
    CHECK(citsy::run_dialog_script(script, w.bind()).pages ==
          std::vector<std::string>{"need water"});

    w.items["Spores"] = 6;
    CHECK(citsy::run_dialog_script(script, w.bind()).pages ==
          std::vector<std::string>{"thanks"});
}

TEST_CASE("dialog: mossland gardener sequence", "[dialog][script]") {
    TestWorld w;
    w.items["Spores"] = 0;
    constexpr std::string_view src = R"("""
{sequence
  - I tend the moss
    
    would you help me {clr3}water {printItem "Can"}{clr3} the spores?
  - if we take care, the moss will feed our children's children
  - if we are greedy, we will deplete the soil and starve
  - the spores will go dormant, awaiting better conditions
    someday they will reawaken and grow again
    the moss will survive our carelessness, but we may not
  - {
      - {item "Spores"} < 6 ?
        the spores need watering
      - else ?
        thanks for your help - you did a {wvy}moss-some{wvy} job
    }
}
""")";
    auto script = citsy::parse_dialog_script(src);

    auto p0 = citsy::run_dialog_script(script, w.bind());
    REQUIRE(p0.pages.size() == 2);
    CHECK(p0.pages[0] == "I tend the moss");
    CHECK(p0.pages[1].find("would you help me") != std::string::npos);
    CHECK(p0.pages[1].find("water") != std::string::npos);
    CHECK(p0.pages[1].find("the spores?") != std::string::npos);

    CHECK(citsy::run_dialog_script(script, w.bind()).pages ==
          std::vector<std::string>{
              "if we take care, the moss will feed our children's children"});
    CHECK(citsy::run_dialog_script(script, w.bind()).pages ==
          std::vector<std::string>{
              "if we are greedy, we will deplete the soil and starve"});

    auto p3 = citsy::run_dialog_script(script, w.bind());
    REQUIRE(p3.pages.size() == 1);
    CHECK(p3.pages[0].find("the spores will go dormant") != std::string::npos);
    CHECK(p3.pages[0].find("someday they will reawaken") != std::string::npos);
    CHECK(p3.pages[0].find("the moss will survive") != std::string::npos);
    CHECK(p3.pages[0].find('\n') != std::string::npos);

    CHECK(citsy::run_dialog_script(script, w.bind()).pages ==
          std::vector<std::string>{"the spores need watering"});

    w.items["Spores"] = 6;
    auto last = citsy::run_dialog_script(script, w.bind());
    REQUIRE(last.pages.size() == 1);
    CHECK(last.pages[0].find("moss-some") != std::string::npos);
    CHECK(last.pages[0].find("thanks for your help") != std::string::npos);
}

TEST_CASE("dialog: gardener tags colour, printItem, and wavy", "[dialog][script]") {
    TestWorld w;
    citsy::Item can;
    can.id = "1";
    can.name = "Can";
    can.frames.push_back({});
    can.frames.back().fill(1);

    auto world = w.bind();
    world.find_item = [&](std::string_view id) -> const citsy::Item* {
        return id == "Can" || id == "1" ? &can : nullptr;
    };

    citsy::DialogVM vm;
    vm.start(R"("""
{sequence
  - I tend the moss
    
    would you help me {clr3}water {printItem "Can"}{clr3} the spores?
}
""")", world, "SPR_1");
    CHECK(vm.plain_text() == "I tend the moss");
    REQUIRE(vm.continue_page());

    bool saw_water_clr3 = false;
    bool saw_can = false;
    bool saw_spores_default = false;
    for (const auto& sp : vm.spans()) {
        if (sp.is_drawing) {
            saw_can = true;
            continue;
        }
        if (sp.text.find("water") != std::string::npos) {
            CHECK(sp.color == 3);
            saw_water_clr3 = true;
        }
        if (sp.text.find("spores") != std::string::npos) {
            CHECK(sp.color == -1);
            saw_spores_default = true;
        }
    }
    CHECK(saw_water_clr3);
    CHECK(saw_can);
    CHECK(saw_spores_default);

    w.items["Spores"] = 6;
    vm.start(R"("""
{sequence
  - skip
  - {
      - {item "Spores"} < 6 ?
        need
      - else ?
        thanks for your help - you did a {wvy}moss-some{wvy} job
    }
}
""")", world, "SPR_wvy");
    CHECK(vm.plain_text() == "skip");
    vm.start(R"("""
{sequence
  - skip
  - {
      - {item "Spores"} < 6 ?
        need
      - else ?
        thanks for your help - you did a {wvy}moss-some{wvy} job
    }
}
""")", world, "SPR_wvy");
    bool saw_wavy = false;
    for (const auto& sp : vm.spans()) {
        if (sp.text.find("moss-some") != std::string::npos) {
            CHECK(sp.effects == citsy::GlyphFx::Wavy);
            saw_wavy = true;
        }
    }
    CHECK(saw_wavy);
}
