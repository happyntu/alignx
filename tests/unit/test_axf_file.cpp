#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "format/axf_file.hpp"

namespace {

std::filesystem::path temp_path(std::string_view name) {
    return std::filesystem::temp_directory_path() / name;
}

std::vector<unsigned char> bytes(std::string_view text) {
    return std::vector<unsigned char>(text.begin(), text.end());
}

alignx::format::AxfFile make_file() {
    alignx::format::AxfFile file;
    file.references.push_back({.name = "chrToy", .length = 1000});
    file.blocks.push_back(
        {.ref_id = 0,
         .start_pos = 100,
         .end_pos = 200,
         .record_count = 2,
         .payload = bytes(
             "read001\t0\tchrToy\t101\t60\t10M\t*\t0\t0\tACGTACGTAA\tFFFFFFFFFF\tNM:i:0\n"
             "read002\t16\tchrToy\t151\t50\t5M1I4M\t*\t0\t0\tTTTTACGGGA\tFFFFFFFFFF\tNM:i:1\n")});
    return file;
}

TEST(AxfFile, WriteReadRoundTrip) {
    const auto path = temp_path("alignx_roundtrip.axf");
    const auto file = make_file();

    auto write = alignx::format::write_axf_file(file, path);
    ASSERT_TRUE(write) << write.error();

    auto read = alignx::format::read_axf_file(path);
    ASSERT_TRUE(read) << read.error();

    ASSERT_EQ(read->references.size(), 1);
    EXPECT_EQ(read->references[0].name, "chrToy");
    EXPECT_EQ(read->references[0].length, 1000);

    ASSERT_EQ(read->blocks.size(), 1);
    EXPECT_EQ(read->blocks[0].ref_id, 0);
    EXPECT_EQ(read->blocks[0].start_pos, 100);
    EXPECT_EQ(read->blocks[0].end_pos, 200);
    EXPECT_EQ(read->blocks[0].record_count, 2);
    EXPECT_EQ(read->blocks[0].payload, file.blocks[0].payload);

    auto hits = read->query_blocks(0, 150, 160);
    ASSERT_TRUE(hits) << hits.error();
    ASSERT_EQ(hits->size(), 1);
    EXPECT_EQ(hits->at(0)->payload, file.blocks[0].payload);

    hits = read->query_blocks(0, 200, 220);
    ASSERT_TRUE(hits) << hits.error();
    EXPECT_TRUE(hits->empty());

    std::filesystem::remove(path);
}

TEST(AxfFile, RejectsInvalidMagic) {
    const auto path = temp_path("alignx_bad_magic.axf");
    {
        std::ofstream output(path, std::ios::binary);
        output << "not an axf";
    }

    auto read = alignx::format::read_axf_file(path);
    ASSERT_FALSE(read);
    EXPECT_NE(read.error().find("invalid AXF magic"), std::string::npos);

    std::filesystem::remove(path);
}

TEST(AxfFile, RejectsInvalidBlocks) {
    auto file = make_file();
    file.blocks[0].end_pos = file.blocks[0].start_pos;

    const auto path = temp_path("alignx_invalid_block.axf");
    auto write = alignx::format::write_axf_file(file, path);

    ASSERT_FALSE(write);
    EXPECT_NE(write.error().find("start_pos < end_pos"), std::string::npos);
}

} // namespace
