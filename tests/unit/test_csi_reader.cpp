#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include "index/csi_reader.hpp"

namespace {

std::filesystem::path toy_csi_path() {
    return std::filesystem::path(TEST_DATA_DIR) / "toy_alignment.sorted.bam.csi";
}

TEST(CsiReader, ReadsToyIndexMetadata) {
    auto index = alignx::index::read_csi_index(toy_csi_path());
    ASSERT_TRUE(index) << index.error();

    EXPECT_EQ(index->min_shift, 14);
    EXPECT_EQ(index->depth, 5);
    EXPECT_TRUE(index->aux.empty());
    ASSERT_EQ(index->reference_count(), 1);
    ASSERT_EQ(index->references.size(), 1);

    const auto& reference = index->references.front();
    ASSERT_EQ(reference.bins.size(), 1);
    EXPECT_EQ(reference.bins.front().id, 4681);
    ASSERT_EQ(reference.bins.front().chunks.size(), 1);
    EXPECT_EQ(reference.bins.front().loffset, reference.bins.front().chunks.front().begin);
    EXPECT_LT(reference.bins.front().chunks.front().begin,
              reference.bins.front().chunks.front().end);

    ASSERT_TRUE(index->unplaced_unmapped_count.has_value());
    EXPECT_EQ(*index->unplaced_unmapped_count, 1);
}

TEST(CsiReader, RejectsInvalidMagic) {
    const auto path = std::filesystem::temp_directory_path() / "alignx_invalid_magic.csi";
    {
        std::ofstream output(path, std::ios::binary);
        constexpr char bytes[] = {'N',  'O',  'P',  'E',  '\0', '\0', '\0', '\0', '\0', '\0',
                                  '\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0', '\0'};
        output.write(bytes, sizeof(bytes));
    }

    auto index = alignx::index::read_csi_index(path);

    EXPECT_FALSE(index);
    EXPECT_NE(index.error().find("invalid CSI magic"), std::string::npos);
    std::filesystem::remove(path);
}

TEST(CsiReader, RejectsTruncatedIndex) {
    const auto path = std::filesystem::temp_directory_path() / "alignx_truncated.csi";
    {
        std::ofstream output(path, std::ios::binary);
        output.write("CSI\1", 4);
        output.put('\1');
    }

    auto index = alignx::index::read_csi_index(path);

    EXPECT_FALSE(index);
    EXPECT_NE(index.error().find("truncated"), std::string::npos);
    std::filesystem::remove(path);
}

} // namespace
