#include <gtest/gtest.h>

#include <cstdint>
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

std::vector<unsigned char> read_bytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    input.seekg(0, std::ios::end);
    const auto size = input.tellg();
    input.seekg(0, std::ios::beg);
    std::vector<unsigned char> data(static_cast<std::size_t>(size));
    input.read(reinterpret_cast<char*>(data.data()), size);
    return data;
}

void write_bytes(const std::filesystem::path& path, const std::vector<unsigned char>& data) {
    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char*>(data.data()),
                 static_cast<std::streamsize>(data.size()));
}

std::uint64_t read_u64_at(const std::vector<unsigned char>& data, std::size_t offset) {
    std::uint64_t value = 0;
    for (std::size_t byte = 0; byte < sizeof(value); ++byte) {
        value |= static_cast<std::uint64_t>(data.at(offset + byte)) << (byte * 8U);
    }
    return value;
}

void write_u32_at(std::vector<unsigned char>& data, std::size_t offset, std::uint32_t value) {
    for (std::size_t byte = 0; byte < sizeof(value); ++byte) {
        data.at(offset + byte) = static_cast<unsigned char>((value >> (byte * 8U)) & 0xFFU);
    }
}

void write_u64_at(std::vector<unsigned char>& data, std::size_t offset, std::uint64_t value) {
    for (std::size_t byte = 0; byte < sizeof(value); ++byte) {
        data.at(offset + byte) = static_cast<unsigned char>((value >> (byte * 8U)) & 0xFFU);
    }
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

TEST(AxfFile, ReadsIndexMetadataWithoutPayloadObjects) {
    const auto path = temp_path("alignx_metadata.axf");
    const auto file = make_file();

    auto write = alignx::format::write_axf_file(file, path);
    ASSERT_TRUE(write) << write.error();

    auto metadata = alignx::format::read_axf_index_metadata(path);
    ASSERT_TRUE(metadata) << metadata.error();

    ASSERT_EQ(metadata->references.size(), 1);
    EXPECT_EQ(metadata->references[0].name, "chrToy");
    EXPECT_EQ(metadata->references[0].length, 1000);

    ASSERT_EQ(metadata->blocks.size(), 1);
    EXPECT_EQ(metadata->blocks[0].ref_id, 0);
    EXPECT_EQ(metadata->blocks[0].start_pos, 100);
    EXPECT_EQ(metadata->blocks[0].end_pos, 200);
    EXPECT_EQ(metadata->blocks[0].record_count, 2);
    EXPECT_EQ(metadata->blocks[0].payload_length, file.blocks[0].payload.size());
    EXPECT_GT(metadata->blocks[0].payload_offset, 0);

    auto hits = metadata->query_blocks(0, 150, 160);
    ASSERT_TRUE(hits) << hits.error();
    ASSERT_EQ(hits->size(), 1);
    EXPECT_EQ(hits->at(0)->payload_offset, metadata->blocks[0].payload_offset);

    hits = metadata->query_blocks(0, 200, 220);
    ASSERT_TRUE(hits) << hits.error();
    EXPECT_TRUE(hits->empty());

    std::filesystem::remove(path);
}

TEST(AxfFile, ReadsBlockPayloadFromIndexEntry) {
    const auto path = temp_path("alignx_metadata_payload_read.axf");
    const auto file = make_file();

    auto write = alignx::format::write_axf_file(file, path);
    ASSERT_TRUE(write) << write.error();

    auto metadata = alignx::format::read_axf_index_metadata(path);
    ASSERT_TRUE(metadata) << metadata.error();
    ASSERT_EQ(metadata->blocks.size(), 1);

    auto payload = alignx::format::read_axf_block_payload(path, metadata->blocks[0]);
    ASSERT_TRUE(payload) << payload.error();
    EXPECT_EQ(*payload, file.blocks[0].payload);

    std::filesystem::remove(path);
}

TEST(AxfFile, PayloadReaderRejectsRangeOutsideFile) {
    const auto path = temp_path("alignx_metadata_payload_read_bad_range.axf");
    auto write = alignx::format::write_axf_file(make_file(), path);
    ASSERT_TRUE(write) << write.error();

    alignx::format::AxfBlockIndexEntry block{.payload_offset = 1'000'000, .payload_length = 1};
    auto payload = alignx::format::read_axf_block_payload(path, block);

    ASSERT_FALSE(payload);
    EXPECT_NE(payload.error().find("payload points outside file"), std::string::npos);

    std::filesystem::remove(path);
}

TEST(AxfFile, MetadataRejectsInvalidBlockReference) {
    const auto path = temp_path("alignx_metadata_bad_ref.axf");
    auto write = alignx::format::write_axf_file(make_file(), path);
    ASSERT_TRUE(write) << write.error();

    auto data = read_bytes(path);
    const std::uint64_t index_offset = read_u64_at(data, 20);
    write_u32_at(data, static_cast<std::size_t>(index_offset), 1);
    write_bytes(path, data);

    auto metadata = alignx::format::read_axf_index_metadata(path);
    ASSERT_FALSE(metadata);
    EXPECT_NE(metadata.error().find("reference id out of range"), std::string::npos);

    std::filesystem::remove(path);
}

TEST(AxfFile, MetadataRejectsPayloadRangeOutsideFile) {
    const auto path = temp_path("alignx_metadata_bad_payload_range.axf");
    auto write = alignx::format::write_axf_file(make_file(), path);
    ASSERT_TRUE(write) << write.error();

    auto data = read_bytes(path);
    const std::uint64_t index_offset = read_u64_at(data, 20);
    constexpr std::size_t payload_offset_field_offset = 16;
    write_u64_at(data, static_cast<std::size_t>(index_offset) + payload_offset_field_offset,
                 static_cast<std::uint64_t>(data.size() + 1));
    write_bytes(path, data);

    auto metadata = alignx::format::read_axf_index_metadata(path);
    ASSERT_FALSE(metadata);
    EXPECT_NE(metadata.error().find("payload points outside file"), std::string::npos);

    std::filesystem::remove(path);
}

TEST(AxfFile, MetadataRejectsPayloadRangeOverlappingIndex) {
    const auto path = temp_path("alignx_metadata_payload_overlaps_index.axf");
    auto write = alignx::format::write_axf_file(make_file(), path);
    ASSERT_TRUE(write) << write.error();

    auto data = read_bytes(path);
    const std::uint64_t index_offset = read_u64_at(data, 20);
    constexpr std::size_t payload_offset_field_offset = 16;
    constexpr std::size_t payload_length_field_offset = 24;
    write_u64_at(data, static_cast<std::size_t>(index_offset) + payload_offset_field_offset,
                 index_offset);
    write_u64_at(data, static_cast<std::size_t>(index_offset) + payload_length_field_offset, 1);
    write_bytes(path, data);

    auto metadata = alignx::format::read_axf_index_metadata(path);
    ASSERT_FALSE(metadata);
    EXPECT_NE(metadata.error().find("payload overlaps index"), std::string::npos);

    std::filesystem::remove(path);
}

} // namespace
