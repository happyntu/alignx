#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include "index/bai_reader.hpp"

namespace {

std::filesystem::path toy_bai_path() {
    return std::filesystem::path(TEST_DATA_DIR) / "toy_alignment.sorted.bam.bai";
}

TEST(BaiReader, ReadsToyIndexMetadata) {
    auto index = alignx::index::read_bai_index(toy_bai_path());
    ASSERT_TRUE(index) << index.error();

    ASSERT_EQ(index->reference_count(), 1);
    ASSERT_EQ(index->references.size(), 1);

    const auto& reference = index->references.front();
    ASSERT_EQ(reference.bins.size(), 1);
    EXPECT_EQ(reference.bins.front().id, 4681);
    ASSERT_EQ(reference.bins.front().chunks.size(), 1);
    EXPECT_LT(reference.bins.front().chunks.front().begin,
              reference.bins.front().chunks.front().end);

    ASSERT_EQ(reference.linear_offsets.size(), 1);
    EXPECT_EQ(reference.linear_offsets.front(), reference.bins.front().chunks.front().begin);

    ASSERT_TRUE(index->unplaced_unmapped_count.has_value());
    EXPECT_EQ(*index->unplaced_unmapped_count, 1);
}

TEST(BaiReader, RejectsInvalidMagic) {
    const auto path = std::filesystem::temp_directory_path() / "alignx_invalid_magic.bai";
    {
        std::ofstream output(path, std::ios::binary);
        constexpr char bytes[] = {'N', 'O', 'P', 'E', '\0', '\0', '\0', '\0'};
        output.write(bytes, sizeof(bytes));
    }

    auto index = alignx::index::read_bai_index(path);

    EXPECT_FALSE(index);
    EXPECT_NE(index.error().find("invalid BAI magic"), std::string::npos);
    std::filesystem::remove(path);
}

TEST(BaiReader, RejectsTruncatedIndex) {
    const auto path = std::filesystem::temp_directory_path() / "alignx_truncated.bai";
    {
        std::ofstream output(path, std::ios::binary);
        output.write("BAI\1", 4);
        output.put('\1');
    }

    auto index = alignx::index::read_bai_index(path);

    EXPECT_FALSE(index);
    EXPECT_NE(index.error().find("truncated"), std::string::npos);
    std::filesystem::remove(path);
}

} // namespace
