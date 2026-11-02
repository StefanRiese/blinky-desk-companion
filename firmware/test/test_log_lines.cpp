// Tests for core::LogLines and parse_log_line: ESP log output joined into whole lines
// (the WiFi driver prints one line in two calls) and split into level, tag and message.
#include <doctest/doctest.h>

#include <string>
#include <vector>

#include "app_core/log_lines.h"

namespace {

std::vector<std::string> feed_all(core::LogLines &l, std::initializer_list<std::string_view> chunks)
{
    std::vector<std::string> out;
    for (auto c : chunks) {
        l.feed(c, [&](std::string_view line) { out.emplace_back(line); });
    }
    return out;
}

} // namespace

TEST_CASE("LogLines joins a line printed in two calls")
{
    core::LogLines l;
    auto out = feed_all(l, {"I (5758) wifi:", "new:<6,0>, old:<1,0>\n"});
    REQUIRE(out.size() == 1);
    CHECK(out[0] == "I (5758) wifi:new:<6,0>, old:<1,0>");
    CHECK_FALSE(l.pending());
}

TEST_CASE("LogLines splits several lines, drops empty ones and keeps the rest")
{
    core::LogLines l;
    auto out = feed_all(l, {"I (1) a: one\r\n\nI (2) b: two\nI (3) c:", " three"});
    REQUIRE(out.size() == 2);
    CHECK(out[0] == "I (1) a: one");
    CHECK(out[1] == "I (2) b: two");
    CHECK(l.pending());
    std::string rest;
    l.flush([&](std::string_view line) { rest = line; });
    CHECK(rest == "I (3) c: three");
    CHECK_FALSE(l.pending());
}

TEST_CASE("LogLines hands out a very long line in pieces")
{
    core::LogLines l;
    const std::string big(core::LogLines::kMaxLine + 10, 'x');
    auto out = feed_all(l, {big, "\n"});
    REQUIRE(out.size() == 2);
    CHECK(out[0].size() == core::LogLines::kMaxLine);
    CHECK(out[1].size() == 10);
}

TEST_CASE("parse_log_line reads level, tag and message")
{
    auto l = core::parse_log_line("\033[0;32mI (1234) net: got ip 192.168.1.23\033[0m");
    REQUIRE(l);
    CHECK(l->level == 'I');
    CHECK(l->tag == "net");
    CHECK(l->msg == "got ip 192.168.1.23");

    // Joined WiFi line: colour codes in the middle, no space after the colon.
    l = core::parse_log_line("\033[0;32mI (5758) wifi:\033[0mnew:<6,0>, old:<1,0>\033[0m");
    REQUIRE(l);
    CHECK(l->tag == "wifi");
    CHECK(l->msg == "new:<6,0>, old:<1,0>");

    l = core::parse_log_line("W (9) boot: a: b");
    REQUIRE(l);
    CHECK(l->level == 'W');
    CHECK(l->tag == "boot");
    CHECK(l->msg == "a: b");

    l = core::parse_log_line("E (10) no tag here");
    REQUIRE(l);
    CHECK(l->level == 'E');
    CHECK(l->tag.empty());
    CHECK(l->msg == "no tag here");

    l = core::parse_log_line("plain text");
    REQUIRE(l);
    CHECK(l->level == 'I');
    CHECK(l->msg == "plain text");

    CHECK_FALSE(core::parse_log_line("\033[0m \r"));
}
