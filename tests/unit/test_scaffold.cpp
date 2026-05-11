#include <gtest/gtest.h>

#include <filesystem>

#include "cli/version.hpp"

namespace {

TEST(Scaffold, VersionIsAvailable) {
    EXPECT_EQ(alignx::version(), "0.1.0");
}

TEST(Scaffold, ToyDataDirectoryExists) {
    ASSERT_TRUE(std::filesystem::exists(TEST_DATA_DIR));
    ASSERT_TRUE(std::filesystem::is_directory(TEST_DATA_DIR));
}

TEST(Scaffold, ToyAlignmentFixturesExist) {
    const auto toy_data_dir = std::filesystem::path(TEST_DATA_DIR);
    const auto sam_path = toy_data_dir / "toy_alignment.sam";
    const auto bam_path = toy_data_dir / "toy_alignment.sorted.bam";
    const auto bai_path = toy_data_dir / "toy_alignment.sorted.bam.bai";
    const auto csi_path = toy_data_dir / "toy_alignment.sorted.bam.csi";

    ASSERT_TRUE(std::filesystem::is_regular_file(sam_path));
    ASSERT_TRUE(std::filesystem::is_regular_file(bam_path));
    ASSERT_TRUE(std::filesystem::is_regular_file(bai_path));
    ASSERT_TRUE(std::filesystem::is_regular_file(csi_path));

    EXPECT_GT(std::filesystem::file_size(sam_path), 0);
    EXPECT_GT(std::filesystem::file_size(bam_path), 0);
    EXPECT_GT(std::filesystem::file_size(bai_path), 0);
    EXPECT_GT(std::filesystem::file_size(csi_path), 0);
}

} // namespace
