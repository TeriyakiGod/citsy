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

class TestWorld final : public citsy::DialogWorld {
public:
    std::map<std::string, citsy::Value> vars;
    std::unordered_map<std::string, int> items;
    int rand_cursor = 0;
    std::vector<int> rand_seq;

    citsy::Value get_var(std::string_view name) const override {
        auto it = vars.find(std::string(name));
        if (it == vars.end()) return citsy::Value::number(0);
        return it->second;
    }
    void set_var(std::string_view name, citsy::Value v) override {
        vars[std::string(name)] = std::move(v);
    }
    int get_item(std::string_view id) const override {
        auto it = items.find(std::string(id));
        return it == items.end() ? 0 : it->second;
    }
    void set_item(std::string_view id, int count) override {
        items[std::string(id)] = count;
    }
    std::string resolve_room(std::string_view id) const override {
        return std::string(id);
    }
    int random_int(int n) override {
        if (n <= 1) return 0;
        if (!rand_seq.empty()) {
            int v = rand_seq[static_cast<std::size_t>(rand_cursor) % rand_seq.size()];
            ++rand_cursor;
            return v % n;
        }
        return 0;
    }
};

citsy::DialogResult run(std::string_view src, TestWorld& w) {
    auto script = citsy::parse_dialog_script(src);
    return citsy::run_dialog_script(script, w);
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
    w.vars["name"] = citsy::Value::string("ada");
    auto r = run(R"("Hello {print name}!")", w);
    REQUIRE(r.pages.size() == 1);
    CHECK(r.pages[0] == "Hello ada!");
}

TEST_CASE("dialog: assignment updates a variable", "[dialog][script]") {
    TestWorld w;
    w.vars["score"] = citsy::Value::number(0);
    auto r = run("{score = 5}\"{print score}\"", w);
    REQUIRE(r.pages.size() == 1);
    CHECK(r.pages[0] == "5");
    CHECK(w.vars["score"].as_number() == 5);
}

TEST_CASE("dialog: arithmetic assignment", "[dialog][script]") {
    TestWorld w;
    w.vars["n"] = citsy::Value::number(2);
    run("{n = n + 1}", w);
    CHECK(w.vars["n"].as_number() == 3);
}

TEST_CASE("dialog: branching list picks the matching arm", "[dialog][script]") {
    TestWorld w;
    w.vars["score"] = citsy::Value::number(1);
    auto r = run(R"({
  - score == 1 ?
    "one"
  - else ?
    "other"
})", w);
    REQUIRE(r.pages.size() == 1);
    CHECK(r.pages[0] == "one");

    w.vars["score"] = citsy::Value::number(0);
    auto script = citsy::parse_dialog_script(R"({
  - score == 1 ?
    "one"
  - else ?
    "other"
})");
    auto r2 = citsy::run_dialog_script(script, w);
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
    CHECK(citsy::run_dialog_script(script, w).pages == std::vector<std::string>{"a"});
    CHECK(citsy::run_dialog_script(script, w).pages == std::vector<std::string>{"b"});
    CHECK(citsy::run_dialog_script(script, w).pages == std::vector<std::string>{"c"});
    CHECK(citsy::run_dialog_script(script, w).pages == std::vector<std::string>{"c"});
}

TEST_CASE("dialog: cycle wraps around", "[dialog][script]") {
    TestWorld w;
    auto script = citsy::parse_dialog_script(R"({cycle
  - "a"
  - "b"
})");
    CHECK(citsy::run_dialog_script(script, w).pages == std::vector<std::string>{"a"});
    CHECK(citsy::run_dialog_script(script, w).pages == std::vector<std::string>{"b"});
    CHECK(citsy::run_dialog_script(script, w).pages == std::vector<std::string>{"a"});
}

TEST_CASE("dialog: shuffle visits every item before repeating", "[dialog][script]") {
    TestWorld w;
    w.rand_seq = {0, 0, 0};  // Fisher-Yates with j=0 each swap → reverse order
    auto script = citsy::parse_dialog_script(R"({shuffle
  - "a"
  - "b"
  - "c"
})");
    std::vector<std::string> seen;
    for (int i = 0; i < 3; ++i) {
        auto r = citsy::run_dialog_script(script, w);
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
    auto r2 = citsy::run_dialog_script(script, w);
    REQUIRE(r2.pages.size() == 1);
    CHECK(r2.pages[0] == "thanks");
}
