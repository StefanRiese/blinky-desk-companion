#include <doctest/doctest.h>

#include <string>
#include <vector>

#include "app_core/ota.h"

using core::OtaReceiver;

namespace {

std::string b64(const std::string &in)
{
    static constexpr char kAlpha[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    size_t i = 0;
    for (; i + 2 < in.size(); i += 3) {
        const uint32_t v = uint8_t(in[i]) << 16 | uint8_t(in[i + 1]) << 8 | uint8_t(in[i + 2]);
        out += {kAlpha[v >> 18], kAlpha[v >> 12 & 63], kAlpha[v >> 6 & 63], kAlpha[v & 63]};
    }
    if (i + 1 == in.size()) {
        const uint32_t v = uint8_t(in[i]) << 16;
        out += {kAlpha[v >> 18], kAlpha[v >> 12 & 63], '=', '='};
    } else if (i + 2 == in.size()) {
        const uint32_t v = uint8_t(in[i]) << 16 | uint8_t(in[i + 1]) << 8;
        out += {kAlpha[v >> 18], kAlpha[v >> 12 & 63], kAlpha[v >> 6 & 63], '='};
    }
    return out;
}

std::string sha_hex(const std::string &s)
{
    core::Sha256 h;
    h.update(reinterpret_cast<const uint8_t *>(s.data()), s.size());
    return core::Sha256::hex(h.finish());
}

struct Fake {
    std::string written;
    bool began = false, finished = false, aborted = false;
    OtaReceiver ota{{.begin =
                         [this](size_t) {
                             began = true;
                             written.clear();
                             return std::optional<std::string>();
                         },
                     .write =
                         [this](const uint8_t *d, size_t n) {
                             written.append(reinterpret_cast<const char *>(d), n);
                             return std::optional<std::string>();
                         },
                     .finish =
                         [this] {
                             finished = true;
                             return std::optional<std::string>();
                         },
                     .abort = [this] { aborted = true; }}};

    std::string in(const std::string &json)
    {
        JsonDocument doc;
        deserializeJson(doc, json);
        return ota.handle(doc.as<JsonVariantConst>());
    }
};

} // namespace

TEST_CASE("Sha256 and Base64")
{
    CHECK(sha_hex("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(sha_hex("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(sha_hex(std::string(1000, 'a')) == "41edece42d63e8d9bf515a9ba6932e1c20cbc9f5a5d134645adb5db1b9737ea3");
    CHECK(core::base64_decode("aGVsbG8=") == "hello");
    CHECK(core::base64_decode("aGVsbG8") == "hello");
    CHECK_FALSE(core::base64_decode("a$b="));
}

TEST_CASE("OtaReceiver: begin, blocks in order, end with the right SHA-256")
{
    Fake f;
    std::string image;
    for (int i = 0; i < 2500; i++) {
        image += static_cast<char>(i * 7);
    }
    CHECK(f.in(R"({"t":"ota","id":1,"data":"AA==","seq":0})").find("no update") != std::string::npos);
    CHECK(f.in(R"({"t":"ota","id":1,"begin":{"size":2500,"sha256":")" + sha_hex(image) + R"(","version":"0.2.0"}})") ==
          R"({"t":"ack","id":1})");
    CHECK(f.ota.active());
    CHECK(f.ota.version() == "0.2.0");
    int id = 2;
    for (size_t off = 0, seq = 0; off < image.size(); off += 1024, seq++) {
        const std::string block = image.substr(off, 1024);
        if (seq == 1) { // a block out of order is refused
            CHECK(f.in(R"({"t":"ota","id":99,"seq":5,"data":")" + b64(block) + R"("})").find("expected block 1") !=
                  std::string::npos);
        }
        CHECK(f.in(R"({"t":"ota","id":)" + std::to_string(id++) + R"(,"seq":)" + std::to_string(seq) + R"(,"data":")" +
                   b64(block) + R"("})")
                  .find("ack") != std::string::npos);
    }
    CHECK(f.ota.percent() == 100);
    CHECK(f.in(R"({"t":"ota","id":50,"end":true})") == R"({"t":"ack","id":50})");
    CHECK(f.written == image);
    CHECK(f.finished);
    CHECK(f.ota.finished());
    CHECK_FALSE(f.ota.active());
}

TEST_CASE("OtaReceiver: a damaged image is refused")
{
    Fake f;
    const std::string image = "firmware";
    f.in(R"({"t":"ota","id":1,"begin":{"size":8,"sha256":")" + sha_hex("FIRMWARE") + R"("}})");
    f.in(R"({"t":"ota","id":2,"seq":0,"data":")" + b64(image) + R"("})");
    CHECK(f.in(R"({"t":"ota","id":3,"end":true})").find("SHA-256 differs") != std::string::npos);
    CHECK_FALSE(f.finished);
    CHECK(f.aborted);
    CHECK(f.in(R"({"t":"ota","id":4,"begin":{"size":0,"sha256":"x"}})").find("err") != std::string::npos);
}
