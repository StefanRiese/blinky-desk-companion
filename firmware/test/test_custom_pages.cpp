#include <doctest/doctest.h>

#include "app_core/custom_pages.h"

namespace {

bool in(core::CustomPages &p, const char *json, uint32_t now = 0)
{
    JsonDocument doc;
    deserializeJson(doc, json);
    return p.handle(doc.as<JsonVariantConst>(), now);
}

} // namespace

TEST_CASE("CustomPages: store, replace, delete, expire")
{
    core::CustomPages p;
    CHECK(in(p, R"({"t":"page","name":"ci","title":"GitLab","ttl":600,"items":[)"
                R"({"w":"icon+text","icon":"ok","text":"main"},{"w":"value","label":"MRs","value":2},)"
                R"({"w":"gauge","value":1},{"w":"bar","label":"disk","value":91,"max":100,"warn":80,"crit":90},)"
                R"({"w":"list","label":"next","lines":["a","b","c","d","e","f","g"]},)"
                R"({"w":"sparkline","label":"load","values":[1,2,"x",3]}]})",
             1000));
    const core::CustomPage *ci = p.get("ci");
    REQUIRE(ci != nullptr);
    CHECK(ci->title == "GitLab");
    REQUIRE(ci->items.size() == 5); // the unknown "gauge" is skipped
    CHECK(std::get<double>(ci->items[1].value) == 2);
    CHECK(ci->items[2].crit == 90.0);
    CHECK(ci->items[3].lines.size() == 6);
    CHECK(ci->items[4].values.size() == 3);

    CHECK(in(p, R"({"t":"page","name":"btc","items":[{"w":"value","label":"EUR","value":"61 000"}]})"));
    CHECK(p.get("btc")->title == "btc"); // no title: the name
    CHECK(p.names() == std::vector<std::string>{"btc", "ci"});

    CHECK_FALSE(p.expire(1000 + 599'000));
    CHECK(p.expire(1000 + 600'000)); // the ttl ran out
    CHECK(p.get("ci") == nullptr);
    CHECK(in(p, R"({"t":"page","name":"btc","del":true})"));
    CHECK(p.names().empty());
    CHECK_FALSE(in(p, R"({"t":"page","name":"btc","del":true})"));
    CHECK_FALSE(in(p, R"({"t":"page","items":[]})")); // no name

    for (int i = 0; i < 9; i++) {
        in(p, (R"({"t":"page","name":"p)" + std::to_string(i) + R"(","items":[]})").c_str());
    }
    CHECK(p.names().size() == core::CustomPages::kMaxPages);
}
