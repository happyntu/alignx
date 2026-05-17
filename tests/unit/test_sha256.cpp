#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <string_view>

#include "io/sha256.hpp"

namespace alignx::io {

TEST(SHA256, EmptyInput) {
    auto digest = sha256(""); // NIST: e3b0c44298fc1c149afbf4c8996fb924...
    EXPECT_EQ(digest[0], 0xe3);
    EXPECT_EQ(digest[1], 0xb0);
    EXPECT_EQ(digest[2], 0xc4);
    EXPECT_EQ(digest[3], 0x42);
    EXPECT_EQ(digest[28], 0x78);
    EXPECT_EQ(digest[29], 0x52);
    EXPECT_EQ(digest[30], 0xb8);
    EXPECT_EQ(digest[31], 0x55);
}

TEST(SHA256, KnownVector_abc) {
    // NIST: ba7816bf 8f01cfea 414140de 5dae2223 b00361a3 96177a9c b410ff61 f20015ad
    auto digest = sha256("abc");
    EXPECT_EQ(digest[0], 0xba);
    EXPECT_EQ(digest[1], 0x78);
    EXPECT_EQ(digest[2], 0x16);
    EXPECT_EQ(digest[3], 0xbf);
    EXPECT_EQ(digest[4], 0x8f);
    EXPECT_EQ(digest[5], 0x01);
    EXPECT_EQ(digest[6], 0xcf);
    EXPECT_EQ(digest[7], 0xea);
    EXPECT_EQ(digest[28], 0xf2);
    EXPECT_EQ(digest[29], 0x00);
    EXPECT_EQ(digest[30], 0x15);
    EXPECT_EQ(digest[31], 0xad);
}

TEST(SHA256, Deterministic) {
    auto d1 = sha256("ACGTACGTACGTACGT");
    auto d2 = sha256("ACGTACGTACGTACGT");
    EXPECT_EQ(d1, d2);
}

TEST(SHA256, DifferentInputsDiffer) {
    auto d1 = sha256("ACGT");
    auto d2 = sha256("TGCA");
    EXPECT_NE(d1, d2);
}

TEST(SHA256, LongerThanOneBlock) {
    // 80 bytes > 64-byte block size
    std::string input(80, 'A');
    auto digest = sha256(input);
    auto digest2 = sha256(input);
    EXPECT_EQ(digest, digest2);
    // Sanity: not all zeros
    bool all_zero = true;
    for (auto b : digest) {
        if (b != 0) { all_zero = false; break; }
    }
    EXPECT_FALSE(all_zero);
}

} // namespace alignx::io
