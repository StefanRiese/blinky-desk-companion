#include "app_core/ota.h"

#include <algorithm>
#include <cstring>

#include "app_core/protocol.h"

namespace core {
namespace {

constexpr std::array<uint32_t, 64> kK = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

uint32_t rotr(uint32_t x, int n)
{
    return (x >> n) | (x << (32 - n));
}

std::string err(JsonVariantConst id, std::string_view msg)
{
    return proto::err(id.is<int>() ? std::optional<int>(id.as<int>()) : std::nullopt, "ota", msg);
}

std::string ack(JsonVariantConst id)
{
    return id.is<int>() ? proto::ack(id.as<int>()) : std::string();
}

bool is_hex64(std::string_view s)
{
    if (s.size() != 64) {
        return false;
    }
    for (char c : s) {
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) {
            return false;
        }
    }
    return true;
}

} // namespace

Sha256::Sha256()
    : h_{0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19}
{
}

void Sha256::block(const uint8_t *p)
{
    std::array<uint32_t, 64> w{};
    for (int i = 0; i < 16; i++) {
        w[i] = uint32_t{p[4 * i]} << 24 | uint32_t{p[4 * i + 1]} << 16 | uint32_t{p[4 * i + 2]} << 8 | p[4 * i + 3];
    }
    for (int i = 16; i < 64; i++) {
        const uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        const uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    auto [a, b, c, d, e, f, g, h] = h_;
    for (int i = 0; i < 64; i++) {
        const uint32_t t1 = h + (rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25)) + ((e & f) ^ (~e & g)) + kK[i] + w[i];
        const uint32_t t2 = (rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }
    const std::array<uint32_t, 8> add{a, b, c, d, e, f, g, h};
    for (int i = 0; i < 8; i++) {
        h_[i] += add[i];
    }
}

void Sha256::update(const uint8_t *data, size_t len)
{
    total_ += len;
    while (len > 0) {
        const size_t n = std::min(len, buf_.size() - used_);
        std::memcpy(buf_.data() + used_, data, n);
        used_ += n;
        data += n;
        len -= n;
        if (used_ == buf_.size()) {
            block(buf_.data());
            used_ = 0;
        }
    }
}

std::array<uint8_t, 32> Sha256::finish()
{
    const uint64_t bits = total_ * 8;
    const uint8_t one = 0x80, zero = 0;
    update(&one, 1);
    while (used_ != 56) {
        update(&zero, 1);
    }
    for (int i = 7; i >= 0; i--) {
        const uint8_t b = static_cast<uint8_t>(bits >> (8 * i));
        update(&b, 1);
    }
    std::array<uint8_t, 32> out{};
    for (int i = 0; i < 8; i++) {
        for (int j = 0; j < 4; j++) {
            out[4 * i + j] = static_cast<uint8_t>(h_[i] >> (24 - 8 * j));
        }
    }
    return out;
}

std::string Sha256::hex(const std::array<uint8_t, 32> &digest)
{
    static constexpr char kHex[] = "0123456789abcdef";
    std::string s;
    for (uint8_t b : digest) {
        s += kHex[b >> 4];
        s += kHex[b & 15];
    }
    return s;
}

std::optional<std::string> base64_decode(std::string_view in)
{
    auto val = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1;
    };
    while (!in.empty() && in.back() == '=') {
        in.remove_suffix(1);
    }
    if (in.size() % 4 == 1) {
        return std::nullopt;
    }
    std::string out;
    out.reserve(in.size() * 3 / 4);
    uint32_t acc = 0;
    int bits = 0;
    for (char c : in) {
        const int v = val(c);
        if (v < 0) {
            return std::nullopt;
        }
        acc = acc << 6 | static_cast<uint32_t>(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out += static_cast<char>((acc >> bits) & 0xFF);
        }
    }
    return out;
}

std::string OtaReceiver::handle(JsonVariantConst m)
{
    const JsonVariantConst id = m["id"];
    if (m["abort"] | false) {
        cancel();
        return ack(id);
    }
    if (m["begin"].is<JsonObjectConst>()) {
        const JsonObjectConst b = m["begin"];
        const size_t size = b["size"] | 0u;
        const std::string sha = b["sha256"] | "";
        if (size == 0 || !is_hex64(sha)) {
            return err(id, "begin needs size and sha256");
        }
        cancel();
        if (auto e = hooks_.begin(size)) {
            return err(id, *e);
        }
        active_ = true;
        finished_ = false;
        size_ = size;
        received_ = 0;
        next_seq_ = 0;
        sha_ = sha;
        version_ = b["version"] | "";
        hash_ = Sha256();
        return ack(id);
    }
    if (!active_) {
        return err(id, "no update in progress");
    }
    if (m["data"].is<const char *>()) {
        if ((m["seq"] | -1) != next_seq_) {
            return err(id, "expected block " + std::to_string(next_seq_));
        }
        const auto data = base64_decode(m["data"].as<const char *>());
        if (!data || data->empty() || data->size() > kMaxBlock || received_ + data->size() > size_) {
            return err(id, "bad block");
        }
        const auto *p = reinterpret_cast<const uint8_t *>(data->data());
        if (auto e = hooks_.write(p, data->size())) {
            cancel();
            return err(id, *e);
        }
        hash_.update(p, data->size());
        received_ += data->size();
        next_seq_++;
        return ack(id);
    }
    if (m["end"] | false) {
        if (received_ != size_) {
            return err(id, "got " + std::to_string(received_) + " of " + std::to_string(size_) + " bytes");
        }
        if (Sha256::hex(hash_.finish()) != sha_) {
            cancel();
            return err(id, "SHA-256 differs: the image was damaged on the way");
        }
        if (auto e = hooks_.finish()) {
            active_ = false;
            return err(id, *e);
        }
        active_ = false;
        finished_ = true;
        return ack(id);
    }
    return err(id, "unknown ota message");
}

void OtaReceiver::cancel()
{
    if (active_ && hooks_.abort) {
        hooks_.abort();
    }
    active_ = false;
}

} // namespace core
