#include <catch2/catch_test_macros.hpp>

#include "src/dialog/linear.hpp"

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

TEST_CASE("dialog: other {tags} are stripped", "[dialog]") {
    auto pages = citsy::extract_dialog_pages(R"("Hi {name} there")");
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
