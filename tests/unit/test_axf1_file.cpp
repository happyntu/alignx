#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string_view>
#include <vector>

#include "format/axf1_file.hpp"

namespace {

std::filesystem::path temp_path(std::string_view label) {
    const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
           (std::string(label) + "_" + std::to_string(suffix) + ".axf1");
}

alignx::format::Axf1File make_file() {
    alignx::format::Axf1File file;
    file.references.push_back({.name = "chrToy", .length = 1000});
    file.chunks.push_back({.ref_id = 0,
                           .start_pos = 100,
                           .end_pos = 160,
                           .records = {{.qname = "read001",
                                        .flag = 0,
                                        .pos = 100,
                                        .mapq = 60,
                                        .cigar = "10M",
                                        .mate_reference = "*",
                                        .mate_pos = 0,
                                        .template_length = 0,
                                        .sequence = "ACGTACGTAA",
                                        .quality = "FFFFFFFFFF",
                                        .tags = "NM:i:0"},
                                       {.qname = "read002",
                                        .flag = 16,
                                        .pos = 150,
                                        .mapq = 50,
                                        .cigar = "5M1I4M",
                                        .mate_reference = "*",
                                        .mate_pos = 0,
                                        .template_length = 0,
                                        .sequence = "TTTTACGGGA",
                                        .quality = "FFFFFFFFFF",
                                        .tags = "NM:i:1"}}});
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

void write_u16_at(std::vector<unsigned char>& data, std::size_t offset, std::uint16_t value) {
    for (std::size_t byte = 0; byte < sizeof(value); ++byte) {
        data.at(offset + byte) = static_cast<unsigned char>((value >> (byte * 8U)) & 0xFFU);
    }
}

void write_u64_at(std::vector<unsigned char>& data, std::size_t offset, std::uint64_t value) {
    for (std::size_t byte = 0; byte < sizeof(value); ++byte) {
        data.at(offset + byte) = static_cast<unsigned char>((value >> (byte * 8U)) & 0xFFU);
    }
}

} // namespace

TEST(Axf1File, WriteReadRoundTrip) {
    const auto path = temp_path("alignx_axf1_roundtrip");
    const auto file = make_file();

    auto write = alignx::format::write_axf1_file(file, path);
    ASSERT_TRUE(write) << write.error();

    auto read = alignx::format::read_axf1_file(path);
    ASSERT_TRUE(read) << read.error();

    ASSERT_EQ(read->references.size(), 1);
    EXPECT_EQ(read->references[0].name, "chrToy");
    EXPECT_EQ(read->references[0].length, 1000);

    ASSERT_EQ(read->chunks.size(), 1);
    EXPECT_EQ(read->chunks[0].ref_id, 0);
    EXPECT_EQ(read->chunks[0].start_pos, 100);
    EXPECT_EQ(read->chunks[0].end_pos, 160);

    ASSERT_EQ(read->chunks[0].records.size(), 2);
    EXPECT_EQ(read->chunks[0].records[0].qname, "read001");
    EXPECT_EQ(read->chunks[0].records[0].flag, 0);
    EXPECT_EQ(read->chunks[0].records[0].pos, 100);
    EXPECT_EQ(read->chunks[0].records[0].mapq, 60);
    EXPECT_EQ(read->chunks[0].records[0].cigar, "10M");
    EXPECT_EQ(read->chunks[0].records[0].sequence, "ACGTACGTAA");
    EXPECT_EQ(read->chunks[0].records[0].quality, "FFFFFFFFFF");
    EXPECT_EQ(read->chunks[0].records[0].tags, "NM:i:0");
    EXPECT_EQ(read->chunks[0].records[1].qname, "read002");
    EXPECT_EQ(read->chunks[0].records[1].flag, 16);
    EXPECT_EQ(read->chunks[0].records[1].pos, 150);
    EXPECT_EQ(read->chunks[0].records[1].mapq, 50);
    EXPECT_EQ(read->chunks[0].records[1].cigar, "5M1I4M");
    EXPECT_EQ(read->chunks[0].records[1].tags, "NM:i:1");

    std::filesystem::remove(path);
}

TEST(Axf1File, RejectsInvalidMagic) {
    const auto path = temp_path("alignx_axf1_bad_magic");
    {
        std::ofstream output(path, std::ios::binary);
        output << "not axf1";
    }

    auto read = alignx::format::read_axf1_file(path);
    ASSERT_FALSE(read);
    EXPECT_NE(read.error().find("invalid AXF1 magic"), std::string::npos);

    std::filesystem::remove(path);
}

TEST(Axf1File, RejectsInvalidChunkInterval) {
    auto file = make_file();
    file.chunks[0].end_pos = file.chunks[0].start_pos;

    const auto path = temp_path("alignx_axf1_invalid_chunk");
    auto write = alignx::format::write_axf1_file(file, path);

    ASSERT_FALSE(write);
    EXPECT_NE(write.error().find("start_pos < end_pos"), std::string::npos);
}

TEST(Axf1File, RejectsMissingRequiredColumn) {
    const auto path = temp_path("alignx_axf1_missing_column");
    const auto file = make_file();
    auto write = alignx::format::write_axf1_file(file, path);
    ASSERT_TRUE(write) << write.error();

    auto data = read_bytes(path);
    constexpr std::size_t index_offset_field_offset = 20;
    const std::uint64_t index_offset = read_u64_at(data, index_offset_field_offset);
    constexpr std::size_t chunk_offset_field_offset = 16;
    const std::uint64_t chunk_offset =
        read_u64_at(data, static_cast<std::size_t>(index_offset) + chunk_offset_field_offset);
    constexpr std::size_t chunk_header_size = 18;
    write_u16_at(data, static_cast<std::size_t>(chunk_offset) + chunk_header_size, 99);
    write_bytes(path, data);

    auto read = alignx::format::read_axf1_file(path);
    ASSERT_FALSE(read);
    EXPECT_NE(read.error().find("missing required column"), std::string::npos);

    std::filesystem::remove(path);
}

TEST(Axf1File, RejectsColumnPayloadOutsideChunk) {
    const auto path = temp_path("alignx_axf1_bad_column_range");
    const auto file = make_file();
    auto write = alignx::format::write_axf1_file(file, path);
    ASSERT_TRUE(write) << write.error();

    auto data = read_bytes(path);
    constexpr std::size_t index_offset_field_offset = 20;
    const std::uint64_t index_offset = read_u64_at(data, index_offset_field_offset);
    constexpr std::size_t chunk_offset_field_offset = 16;
    const std::uint64_t chunk_offset =
        read_u64_at(data, static_cast<std::size_t>(index_offset) + chunk_offset_field_offset);
    constexpr std::size_t first_column_length_offset = 18 + 2 + 2 + 8;
    write_u64_at(data, static_cast<std::size_t>(chunk_offset) + first_column_length_offset,
                 1'000'000);
    write_bytes(path, data);

    auto read = alignx::format::read_axf1_file(path);
    ASSERT_FALSE(read);
    EXPECT_NE(read.error().find("column payload points outside chunk"), std::string::npos);

    std::filesystem::remove(path);
}

TEST(Axf1File, RejectsChunkPayloadOutsideFile) {
    const auto path = temp_path("alignx_axf1_bad_chunk_range");
    const auto file = make_file();
    auto write = alignx::format::write_axf1_file(file, path);
    ASSERT_TRUE(write) << write.error();

    auto data = read_bytes(path);
    constexpr std::size_t index_offset_field_offset = 20;
    const std::uint64_t index_offset = read_u64_at(data, index_offset_field_offset);
    constexpr std::size_t chunk_offset_field_offset = 16;
    write_u64_at(data, static_cast<std::size_t>(index_offset) + chunk_offset_field_offset,
                 1'000'000);
    write_bytes(path, data);

    auto read = alignx::format::read_axf1_file(path);
    ASSERT_FALSE(read);
    EXPECT_NE(read.error().find("chunk points outside file"), std::string::npos);

    std::filesystem::remove(path);
}
