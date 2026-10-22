// =============================================================================
//  common/src/crypto.cpp
//  SHA-1 / SHA-256 / Base64 / 随机盐 的实现。
// =============================================================================
#include "im/common/crypto.hpp"

#include <random>
#include <vector>

namespace im::crypto {
namespace {

constexpr std::uint32_t rotl32(std::uint32_t v, int n) noexcept {
    return (v << n) | (v >> (32 - n));
}

/// 按 MD 结构补位：0x80 + 若干 0x00 + 64 位大端比特长度，使总长为 64 的倍数
std::string padMessage(std::string_view data) {
    std::string msg(data);
    const std::uint64_t bit_len = static_cast<std::uint64_t>(data.size()) * 8ull;

    msg.push_back(static_cast<char>(0x80));
    while (msg.size() % 64 != 56) {
        msg.push_back('\0');
    }
    for (int i = 7; i >= 0; --i) {
        msg.push_back(static_cast<char>((bit_len >> (i * 8)) & 0xFFu));
    }
    return msg;
}

inline std::uint32_t be32(const std::uint8_t* p) noexcept {
    return (static_cast<std::uint32_t>(p[0]) << 24)
         | (static_cast<std::uint32_t>(p[1]) << 16)
         | (static_cast<std::uint32_t>(p[2]) << 8)
         |  static_cast<std::uint32_t>(p[3]);
}

inline std::uint32_t be32(const std::string& s, std::size_t off) noexcept {
    return be32(reinterpret_cast<const std::uint8_t*>(s.data()) + off);
}

void sha1Block(std::uint32_t h[5], const std::string& block, std::size_t off) {
    std::uint32_t w[80];
    for (int i = 0; i < 16; ++i) {
        w[i] = be32(block, off + static_cast<std::size_t>(i) * 4);
    }
    for (int i = 16; i < 80; ++i) {
        w[i] = rotl32(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    }

    std::uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
    for (int i = 0; i < 80; ++i) {
        std::uint32_t f = 0;
        std::uint32_t k = 0;
        if (i < 20) {
            f = (b & c) | ((~b) & d);
            k = 0x5A827999u;
        } else if (i < 40) {
            f = b ^ c ^ d;
            k = 0x6ED9EBA1u;
        } else if (i < 60) {
            f = (b & c) | (b & d) | (c & d);
            k = 0x8F1BBCDCu;
        } else {
            f = b ^ c ^ d;
            k = 0xCA62C1D6u;
        }
        const std::uint32_t tmp = rotl32(a, 5) + f + e + k + w[i];
        e = d;
        d = c;
        c = rotl32(b, 30);
        b = a;
        a = tmp;
    }
    h[0] += a;
    h[1] += b;
    h[2] += c;
    h[3] += d;
    h[4] += e;
}

constexpr std::uint32_t kSha256K[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
};

inline std::uint32_t rotr32(std::uint32_t v, int n) noexcept {
    return (v >> n) | (v << (32 - n));
}

void sha256Block(std::uint32_t h[8], const std::string& block, std::size_t off) {
    std::uint32_t w[64];
    for (int i = 0; i < 16; ++i) {
        w[i] = be32(block, off + static_cast<std::size_t>(i) * 4);
    }
    for (int i = 16; i < 64; ++i) {
        const std::uint32_t s0 = rotr32(w[i - 15], 7) ^ rotr32(w[i - 15], 18) ^ (w[i - 15] >> 3);
        const std::uint32_t s1 = rotr32(w[i - 2], 17) ^ rotr32(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    std::uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
    std::uint32_t e = h[4], f = h[5], g = h[6], k = h[7];

    for (int i = 0; i < 64; ++i) {
        const std::uint32_t s1 = rotr32(e, 6) ^ rotr32(e, 11) ^ rotr32(e, 25);
        const std::uint32_t ch = (e & f) ^ ((~e) & g);
        const std::uint32_t t1 = k + s1 + ch + kSha256K[i] + w[i];
        const std::uint32_t s0 = rotr32(a, 2) ^ rotr32(a, 13) ^ rotr32(a, 22);
        const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        const std::uint32_t t2 = s0 + maj;

        k = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }

    h[0] += a;
    h[1] += b;
    h[2] += c;
    h[3] += d;
    h[4] += e;
    h[5] += f;
    h[6] += g;
    h[7] += k;
}

}  // namespace

std::array<std::uint8_t, 20> sha1(std::string_view data) {
    const std::string msg = padMessage(data);
    std::uint32_t h[5] = {0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u};

    for (std::size_t off = 0; off < msg.size(); off += 64) {
        sha1Block(h, msg, off);
    }

    std::array<std::uint8_t, 20> out{};
    for (int i = 0; i < 5; ++i) {
        out[static_cast<std::size_t>(i) * 4 + 0] = static_cast<std::uint8_t>((h[i] >> 24) & 0xFFu);
        out[static_cast<std::size_t>(i) * 4 + 1] = static_cast<std::uint8_t>((h[i] >> 16) & 0xFFu);
        out[static_cast<std::size_t>(i) * 4 + 2] = static_cast<std::uint8_t>((h[i] >> 8) & 0xFFu);
        out[static_cast<std::size_t>(i) * 4 + 3] = static_cast<std::uint8_t>(h[i] & 0xFFu);
    }
    return out;
}

std::string sha1Raw(std::string_view data) {
    const auto digest = sha1(data);
    return std::string(reinterpret_cast<const char*>(digest.data()), digest.size());
}

std::string sha1Hex(std::string_view data) {
    static constexpr char kHex[] = "0123456789abcdef";
    const auto digest = sha1(data);
    std::string out;
    out.reserve(digest.size() * 2);
    for (std::uint8_t b : digest) {
        out.push_back(kHex[(b >> 4) & 0x0Fu]);
        out.push_back(kHex[b & 0x0Fu]);
    }
    return out;
}

std::string hmacSha1Hex(std::string_view key, std::string_view data) {
    constexpr std::size_t kBlock = 64;

    std::string real_key(key);
    if (real_key.size() > kBlock) {
        real_key = sha1Raw(real_key);
    }
    real_key.resize(kBlock, '\0');

    std::string inner(kBlock, '\0');
    std::string outer(kBlock, '\0');
    for (std::size_t i = 0; i < kBlock; ++i) {
        inner[i] = static_cast<char>(static_cast<unsigned char>(real_key[i]) ^ 0x36u);
        outer[i] = static_cast<char>(static_cast<unsigned char>(real_key[i]) ^ 0x5Cu);
    }

    inner.append(data);
    const std::string inner_hash = sha1Raw(inner);
    outer.append(inner_hash);

    static constexpr char kHex[] = "0123456789abcdef";
    const auto digest = sha1(outer);
    std::string out;
    out.reserve(digest.size() * 2);
    for (std::uint8_t b : digest) {
        out.push_back(kHex[(b >> 4) & 0x0Fu]);
        out.push_back(kHex[b & 0x0Fu]);
    }
    return out;
}

std::array<std::uint8_t, 32> sha256(std::string_view data) {
    const std::string msg = padMessage(data);
    std::uint32_t h[8] = {
        0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
        0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u,
    };

    for (std::size_t off = 0; off < msg.size(); off += 64) {
        sha256Block(h, msg, off);
    }

    std::array<std::uint8_t, 32> out{};
    for (int i = 0; i < 8; ++i) {
        out[static_cast<std::size_t>(i) * 4 + 0] = static_cast<std::uint8_t>((h[i] >> 24) & 0xFFu);
        out[static_cast<std::size_t>(i) * 4 + 1] = static_cast<std::uint8_t>((h[i] >> 16) & 0xFFu);
        out[static_cast<std::size_t>(i) * 4 + 2] = static_cast<std::uint8_t>((h[i] >> 8) & 0xFFu);
        out[static_cast<std::size_t>(i) * 4 + 3] = static_cast<std::uint8_t>(h[i] & 0xFFu);
    }
    return out;
}

std::string sha256Hex(std::string_view data) {
    static constexpr char kHex[] = "0123456789abcdef";
    const auto digest = sha256(data);
    std::string out;
    out.reserve(digest.size() * 2);
    for (std::uint8_t b : digest) {
        out.push_back(kHex[(b >> 4) & 0x0Fu]);
        out.push_back(kHex[b & 0x0Fu]);
    }
    return out;
}

std::string base64(std::string_view data) {
    static constexpr char kTable[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    std::string out;
    out.reserve(((data.size() + 2) / 3) * 4);

    std::size_t i = 0;
    while (i + 3 <= data.size()) {
        const auto b0 = static_cast<std::uint8_t>(data[i]);
        const auto b1 = static_cast<std::uint8_t>(data[i + 1]);
        const auto b2 = static_cast<std::uint8_t>(data[i + 2]);
        out.push_back(kTable[b0 >> 2]);
        out.push_back(kTable[((b0 & 0x03u) << 4) | (b1 >> 4)]);
        out.push_back(kTable[((b1 & 0x0Fu) << 2) | (b2 >> 6)]);
        out.push_back(kTable[b2 & 0x3Fu]);
        i += 3;
    }

    const std::size_t rem = data.size() - i;
    if (rem == 1) {
        const auto b0 = static_cast<std::uint8_t>(data[i]);
        out.push_back(kTable[b0 >> 2]);
        out.push_back(kTable[(b0 & 0x03u) << 4]);
        out.push_back('=');
        out.push_back('=');
    } else if (rem == 2) {
        const auto b0 = static_cast<std::uint8_t>(data[i]);
        const auto b1 = static_cast<std::uint8_t>(data[i + 1]);
        out.push_back(kTable[b0 >> 2]);
        out.push_back(kTable[((b0 & 0x03u) << 4) | (b1 >> 4)]);
        out.push_back(kTable[(b1 & 0x0Fu) << 2]);
        out.push_back('=');
    }
    return out;
}

std::string randomHex(std::size_t bytes) {
    static thread_local std::mt19937_64 rng{std::random_device{}()};
    static constexpr char kHex[] = "0123456789abcdef";

    std::string out;
    out.reserve(bytes * 2);
    for (std::size_t i = 0; i < bytes; ++i) {
        const auto v = static_cast<std::uint8_t>(rng() & 0xFFu);
        out.push_back(kHex[(v >> 4) & 0x0Fu]);
        out.push_back(kHex[v & 0x0Fu]);
    }
    return out;
}

}  // namespace im::crypto
