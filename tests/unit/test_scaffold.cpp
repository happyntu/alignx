#include "cli/version.hpp"

#include <filesystem>

#include <gtest/gtest.h>

namespace {

TEST(Scaffold, VersionIsAvailable) {
  EXPECT_EQ(alignx::version(), "0.1.0");
}

TEST(Scaffold, ToyDataDirectoryExists) {
  ASSERT_TRUE(std::filesystem::exists(TEST_DATA_DIR));
  ASSERT_TRUE(std::filesystem::is_directory(TEST_DATA_DIR));
}

}  // namespace
