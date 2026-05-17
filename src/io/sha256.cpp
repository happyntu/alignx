// Self-contained SHA-256 implementation based on RFC 6234 / FIPS 180-4.
// Public domain — no external dependencies.

#include "io/sha256.hpp"

#include <cstring>

namespace alignx::io {

namespace {

constexpr std::uint32_t kK[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4,
    0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe,
    0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f,
    0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
    0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc,
    0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
    0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116,
    0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7,
    0xc67178f2,
};

constexpr std::uint32_t rotr(std::uint32_t x, unsigned n) {
    return (x >> n) | (x << (32 - n));
}

constexpr std::uint32_t ch(std::uint32_t x, std::uint32_t y, std::uint32_t z) {
    return (x & y) ^ (~x & z);
}

constexpr std::uint32_t maj(std::uint32_t x, std::uint32_t y, std::uint32_t z) {
    return (x & y) ^ (x & z) ^ (y & z);
}

constexpr std::uint32_t big_sigma0(std::uint32_t x) {
    return rotr(x, 2) ^ rotr(x, 13) ^ rotr(x, 22);
}

constexpr std::uint32_t big_sigma1(std::uint32_t x) {
    return rotr(x, 6) ^ rotr(x, 11) ^ rotr(x, 25);
}

constexpr std::uint32_t small_sigma0(std::uint32_t x) {
    return rotr(x, 7) ^ rotr(x, 18) ^ (x >> 3);
}

constexpr std::uint32_t small_sigma1(std::uint32_t x) {
    return rotr(x, 17) ^ rotr(x, 19) ^ (x >> 10);
}

std::uint32_t load_be32(const unsigned char* p) {
    return (static_cast<std::uint32_t>(p[0]) << 24) |
           (static_cast<std::uint32_t>(p[1]) << 16) |
           (static_cast<std::uint32_t>(p[2]) << 8) |
           static_cast<std::uint32_t>(p[3]);
}

void store_be32(unsigned char* p, std::uint32_t v) {
    p[0] = static_cast<unsigned char>(v >> 24);
    p[1] = static_cast<unsigned char>(v >> 16);
    p[2] = static_cast<unsigned char>(v >> 8);
    p[3] = static_cast<unsigned char>(v);
}

struct Sha256State {
    std::uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                          0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    unsigned char buffer[64]{};
    std::size_t buffer_len = 0;
    std::uint64_t total_len = 0;
};

void process_block(Sha256State& state, const unsigned char* block) {
    std::uint32_t w[64];
    for (int i = 0; i < 16; ++i) {
        w[i] = load_be32(block + i * 4);
    }
    for (int i = 16; i < 64; ++i) {
        w[i] = small_sigma1(w[i - 2]) + w[i - 7] + small_sigma0(w[i - 15]) + w[i - 16];
    }

    std::uint32_t a = state.h[0], b = state.h[1], c = state.h[2], d = state.h[3];
    std::uint32_t e = state.h[4], f = state.h[5], g = state.h[6], hh = state.h[7];

    for (int i = 0; i < 64; ++i) {
        const std::uint32_t t1 = hh + big_sigma1(e) + ch(e, f, g) + kK[i] + w[i];
        const std::uint32_t t2 = big_sigma0(a) + maj(a, b, c);
        hh = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }

    state.h[0] += a;
    state.h[1] += b;
    state.h[2] += c;
    state.h[3] += d;
    state.h[4] += e;
    state.h[5] += f;
    state.h[6] += g;
    state.h[7] += hh;
}

} // namespace

std::array<unsigned char, 32> sha256(const unsigned char* data, std::size_t size) {
    Sha256State state;
    state.total_len = size;

    std::size_t offset = 0;
    while (offset + 64 <= size) {
        process_block(state, data + offset);
        offset += 64;
    }

    std::size_t remaining = size - offset;
    std::memcpy(state.buffer, data + offset, remaining);
    state.buffer_len = remaining;

    // Padding
    state.buffer[state.buffer_len++] = 0x80;
    if (state.buffer_len > 56) {
        std::memset(state.buffer + state.buffer_len, 0, 64 - state.buffer_len);
        process_block(state, state.buffer);
        state.buffer_len = 0;
    }
    std::memset(state.buffer + state.buffer_len, 0, 56 - state.buffer_len);

    const std::uint64_t bit_len = state.total_len * 8;
    state.buffer[56] = static_cast<unsigned char>(bit_len >> 56);
    state.buffer[57] = static_cast<unsigned char>(bit_len >> 48);
    state.buffer[58] = static_cast<unsigned char>(bit_len >> 40);
    state.buffer[59] = static_cast<unsigned char>(bit_len >> 32);
    state.buffer[60] = static_cast<unsigned char>(bit_len >> 24);
    state.buffer[61] = static_cast<unsigned char>(bit_len >> 16);
    state.buffer[62] = static_cast<unsigned char>(bit_len >> 8);
    state.buffer[63] = static_cast<unsigned char>(bit_len);
    process_block(state, state.buffer);

    std::array<unsigned char, 32> digest{};
    for (int i = 0; i < 8; ++i) {
        store_be32(digest.data() + i * 4, state.h[i]);
    }
    return digest;
}

} // namespace alignx::io
