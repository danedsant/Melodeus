#include "library/sha1.hpp"

#include <cstring>

namespace melodeus::library {

namespace {

std::uint32_t rotl(std::uint32_t v, int n) {
    return (v << n) | (v >> (32 - n));
}

} // namespace

std::array<std::uint8_t, 20> sha1(const void* data, std::size_t size) {
    std::uint32_t h0 = 0x67452301u;
    std::uint32_t h1 = 0xEFCDAB89u;
    std::uint32_t h2 = 0x98BADCFEu;
    std::uint32_t h3 = 0x10325476u;
    std::uint32_t h4 = 0xC3D2E1F0u;

    const auto* bytes = static_cast<const std::uint8_t*>(data);
    const std::size_t total = size;

    std::uint8_t block[64]{};
    std::size_t offset = 0;
    const auto process_block = [&](const std::uint8_t* p) {
        std::uint32_t w[80];
        for (int i = 0; i < 16; ++i) {
            w[i] = (static_cast<std::uint32_t>(p[i * 4]) << 24) |
                   (static_cast<std::uint32_t>(p[i * 4 + 1]) << 16) |
                   (static_cast<std::uint32_t>(p[i * 4 + 2]) << 8) |
                   static_cast<std::uint32_t>(p[i * 4 + 3]);
        }
        for (int i = 16; i < 80; ++i) {
            w[i] = rotl(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        }
        std::uint32_t a = h0;
        std::uint32_t b = h1;
        std::uint32_t c = h2;
        std::uint32_t d = h3;
        std::uint32_t e = h4;
        for (int i = 0; i < 80; ++i) {
            std::uint32_t f;
            std::uint32_t k;
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
            const std::uint32_t temp = rotl(a, 5) + f + e + k + w[i];
            e = d;
            d = c;
            c = rotl(b, 30);
            b = a;
            a = temp;
        }
        h0 += a;
        h1 += b;
        h2 += c;
        h3 += d;
        h4 += e;
    };

    // Bloques de datos.
    while (offset + 64 <= total) {
        process_block(bytes + offset);
        offset += 64;
    }
    // Último bloque con padding y longitud en bits (big-endian).
    std::memset(block, 0, sizeof(block));
    const std::size_t rem = total - offset;
    std::memcpy(block, bytes + offset, rem);
    block[rem] = 0x80;
    if (rem >= 56) {
        process_block(block);
        std::memset(block, 0, sizeof(block));
    }
    const std::uint64_t bits = static_cast<std::uint64_t>(total) * 8u;
    for (int i = 0; i < 8; ++i) {
        block[63 - i] = static_cast<std::uint8_t>((bits >> (i * 8)) & 0xFFu);
    }
    process_block(block);

    std::array<std::uint8_t, 20> out{};
    const std::uint32_t hs[5] = {h0, h1, h2, h3, h4};
    for (int i = 0; i < 5; ++i) {
        out[static_cast<std::size_t>(i * 4)] = static_cast<std::uint8_t>(hs[i] >> 24);
        out[static_cast<std::size_t>(i * 4 + 1)] = static_cast<std::uint8_t>(hs[i] >> 16);
        out[static_cast<std::size_t>(i * 4 + 2)] = static_cast<std::uint8_t>(hs[i] >> 8);
        out[static_cast<std::size_t>(i * 4 + 3)] = static_cast<std::uint8_t>(hs[i]);
    }
    return out;
}

} // namespace melodeus::library
