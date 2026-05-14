#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string_view>
#include <vector>

#include "format/axf1_file.hpp"

namespace {

constexpr std::size_t kIndexOffsetFieldOffset = 20;
constexpr std::size_t kVersionFieldOffset = 4;
constexpr std::size_t kHeaderSize = 28;
constexpr std::size_t kReferenceBytes = 12;
constexpr std::size_t kMetadataOffset = kHeaderSize + kReferenceBytes;
constexpr std::size_t kMetadataSubsetFlagOffset = kMetadataOffset;
constexpr std::size_t kMetadataSourcePathLengthOffset = kMetadataOffset + 1;
constexpr std::size_t kMetadataConversionRegionLengthOffset =
    kMetadataSourcePathLengthOffset + sizeof(std::uint32_t);
constexpr std::size_t kIndexRecordCountOffset = 12;
constexpr std::size_t kIndexChunkOffsetOffset = 16;
constexpr std::size_t kChunkStartPosOffset = 4;
constexpr std::size_t kChunkRecordCountOffset = 12;
constexpr std::size_t kChunkHeaderSize = 18;
constexpr std::size_t kColumnEntrySize = 20;
constexpr std::size_t kColumnIdOffset = 0;
constexpr std::size_t kColumnLengthOffset = 12;

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

std::uint64_t read_index_offset(const std::vector<unsigned char>& data) {
    return read_u64_at(data, kIndexOffsetFieldOffset);
}

std::uint64_t read_first_chunk_offset(const std::vector<unsigned char>& data) {
    return read_u64_at(data,
                       static_cast<std::size_t>(read_index_offset(data)) + kIndexChunkOffsetOffset);
}

std::size_t column_entry_offset(const std::vector<unsigned char>& data, std::size_t column_index) {
    return static_cast<std::size_t>(read_first_chunk_offset(data)) + kChunkHeaderSize +
           column_index * kColumnEntrySize;
}

template <typename Mutator>
void expect_v2_metadata_corruption_rejected(std::string_view label, Mutator mutate,
                                            std::string_view expected_error) {
    const auto path = temp_path(label);
    const auto file = make_file();
    auto write = alignx::format::write_axf1_file(file, path);
    ASSERT_TRUE(write) << write.error();

    auto data = read_bytes(path);
    mutate(data);
    write_bytes(path, data);

    auto read = alignx::format::read_axf1_file(path);
    ASSERT_FALSE(read);
    EXPECT_NE(read.error().find(expected_error), std::string::npos) << read.error();

    auto metadata = alignx::format::read_axf1_index_metadata(path);
    ASSERT_FALSE(metadata);
    EXPECT_NE(metadata.error().find(expected_error), std::string::npos) << metadata.error();

    std::filesystem::remove(path);
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

TEST(Axf1File, WriteReadRoundTripPreservesMetadata) {
    const auto path = temp_path("alignx_axf1_metadata_roundtrip");
    auto file = make_file();
    file.metadata = {
        .source_path = "/data/input.bam", .conversion_region = "chrToy:101-160", .is_subset = true};

    auto write = alignx::format::write_axf1_file(file, path);
    ASSERT_TRUE(write) << write.error();

    auto read = alignx::format::read_axf1_file(path);
    ASSERT_TRUE(read) << read.error();

    EXPECT_EQ(read->metadata.source_path, "/data/input.bam");
    EXPECT_EQ(read->metadata.conversion_region, "chrToy:101-160");
    EXPECT_TRUE(read->metadata.is_subset);

    auto metadata = alignx::format::read_axf1_index_metadata(path);
    ASSERT_TRUE(metadata) << metadata.error();
    EXPECT_EQ(metadata->metadata.source_path, "/data/input.bam");
    EXPECT_EQ(metadata->metadata.conversion_region, "chrToy:101-160");
    EXPECT_TRUE(metadata->metadata.is_subset);

    std::filesystem::remove(path);
}

TEST(Axf1File, ReadsIndexMetadataWithoutDecodingChunks) {
    const auto path = temp_path("alignx_axf1_metadata");
    const auto file = make_file();

    auto write = alignx::format::write_axf1_file(file, path);
    ASSERT_TRUE(write) << write.error();

    auto metadata = alignx::format::read_axf1_index_metadata(path);
    ASSERT_TRUE(metadata) << metadata.error();

    ASSERT_EQ(metadata->references.size(), 1);
    EXPECT_EQ(metadata->metadata.source_path, "");
    EXPECT_EQ(metadata->metadata.conversion_region, "");
    EXPECT_FALSE(metadata->metadata.is_subset);
    EXPECT_EQ(metadata->references[0].name, "chrToy");
    EXPECT_EQ(metadata->references[0].length, 1000);

    ASSERT_EQ(metadata->chunks.size(), 1);
    EXPECT_EQ(metadata->chunks[0].ref_id, 0);
    EXPECT_EQ(metadata->chunks[0].start_pos, 100);
    EXPECT_EQ(metadata->chunks[0].end_pos, 160);
    EXPECT_EQ(metadata->chunks[0].record_count, 2);
    EXPECT_GT(metadata->chunks[0].chunk_offset, 0);
    EXPECT_GT(metadata->chunks[0].chunk_length, 0);

    auto hits = metadata->query_chunks(0, 150, 151);
    ASSERT_TRUE(hits) << hits.error();
    ASSERT_EQ(hits->size(), 1);
    EXPECT_EQ(hits->at(0)->record_count, 2);

    hits = metadata->query_chunks(0, 160, 170);
    ASSERT_TRUE(hits) << hits.error();
    EXPECT_TRUE(hits->empty());

    std::filesystem::remove(path);
}

TEST(Axf1File, ReadsLegacyV1WithoutMetadata) {
    const auto path = temp_path("alignx_axf1_legacy_v1");
    const auto file = make_file();
    auto write = alignx::format::write_axf1_file(file, path);
    ASSERT_TRUE(write) << write.error();

    auto data = read_bytes(path);
    constexpr std::size_t kDefaultV2MetadataSize = 9;
    const std::uint64_t old_index_offset = read_index_offset(data);
    const std::uint64_t old_chunk_offset = read_first_chunk_offset(data);

    write_u32_at(data, kVersionFieldOffset, 1);
    write_u64_at(data, kIndexOffsetFieldOffset, old_index_offset - kDefaultV2MetadataSize);
    data.erase(data.begin() + static_cast<std::ptrdiff_t>(kMetadataOffset),
               data.begin() +
                   static_cast<std::ptrdiff_t>(kMetadataOffset + kDefaultV2MetadataSize));
    write_u64_at(data,
                 static_cast<std::size_t>(old_index_offset - kDefaultV2MetadataSize) +
                     kIndexChunkOffsetOffset,
                 old_chunk_offset - kDefaultV2MetadataSize);
    write_bytes(path, data);

    auto read = alignx::format::read_axf1_file(path);
    ASSERT_TRUE(read) << read.error();
    EXPECT_FALSE(read->metadata.is_subset);
    EXPECT_EQ(read->metadata.source_path, "");
    EXPECT_EQ(read->metadata.conversion_region, "");
    ASSERT_EQ(read->chunks.size(), 1);
    ASSERT_EQ(read->chunks[0].records.size(), 2);

    auto metadata = alignx::format::read_axf1_index_metadata(path);
    ASSERT_TRUE(metadata) << metadata.error();
    EXPECT_FALSE(metadata->metadata.is_subset);
    EXPECT_EQ(metadata->metadata.source_path, "");
    EXPECT_EQ(metadata->metadata.conversion_region, "");

    std::filesystem::remove(path);
}

TEST(Axf1File, RejectsInvalidMetadataSubsetFlag) {
    expect_v2_metadata_corruption_rejected(
        "alignx_axf1_bad_metadata_subset_flag",
        [](std::vector<unsigned char>& data) { data.at(kMetadataSubsetFlagOffset) = 2; },
        "invalid AXF1 subset metadata flag");
}

TEST(Axf1File, RejectsTruncatedMetadataSourcePath) {
    expect_v2_metadata_corruption_rejected(
        "alignx_axf1_bad_metadata_source_path",
        [](std::vector<unsigned char>& data) {
            write_u32_at(data, kMetadataSourcePathLengthOffset, 1'000'000);
        },
        "truncated AXF1 metadata");
}

TEST(Axf1File, RejectsTruncatedMetadataConversionRegion) {
    expect_v2_metadata_corruption_rejected(
        "alignx_axf1_bad_metadata_conversion_region",
        [](std::vector<unsigned char>& data) {
            write_u32_at(data, kMetadataConversionRegionLengthOffset, 1'000'000);
        },
        "truncated AXF1 metadata");
}

TEST(Axf1File, RejectsMetadataOverlappingIndex) {
    expect_v2_metadata_corruption_rejected(
        "alignx_axf1_metadata_overlaps_index",
        [](std::vector<unsigned char>& data) {
            write_u64_at(data, kIndexOffsetFieldOffset, kMetadataOffset + 1);
        },
        "AXF1 metadata overlaps index");
}

TEST(Axf1FileReader, OpensAndReadsQueryChunk) {
    const auto path = temp_path("alignx_axf1_file_reader");
    const auto file = make_file();

    auto write = alignx::format::write_axf1_file(file, path);
    ASSERT_TRUE(write) << write.error();

    auto reader = alignx::format::Axf1FileReader::open(path);
    ASSERT_TRUE(reader) << reader.error();

    ASSERT_EQ(reader->index().references.size(), 1);
    ASSERT_EQ(reader->index().chunks.size(), 1);

    auto hits = reader->query_chunks(0, 150, 151);
    ASSERT_TRUE(hits) << hits.error();
    ASSERT_EQ(hits->size(), 1);

    auto chunk = reader->read_chunk(*hits->at(0));
    ASSERT_TRUE(chunk) << chunk.error();
    ASSERT_EQ(chunk->records.size(), 2);
    EXPECT_EQ(chunk->records[0].qname, "read001");
    EXPECT_EQ(chunk->records[1].qname, "read002");

    std::filesystem::remove(path);
}

TEST(Axf1FileReader, ReadsSelectedChunkColumns) {
    const auto path = temp_path("alignx_axf1_file_reader_columns");
    const auto file = make_file();

    auto write = alignx::format::write_axf1_file(file, path);
    ASSERT_TRUE(write) << write.error();

    auto reader = alignx::format::Axf1FileReader::open(path);
    ASSERT_TRUE(reader) << reader.error();

    auto hits = reader->query_chunks(0, 150, 151);
    ASSERT_TRUE(hits) << hits.error();
    ASSERT_EQ(hits->size(), 1);

    auto chunk = reader->read_chunk_columns(
        *hits->at(0), {alignx::format::Axf1ColumnId::pos, alignx::format::Axf1ColumnId::cigar});
    ASSERT_TRUE(chunk) << chunk.error();
    ASSERT_EQ(chunk->records.size(), 2);
    EXPECT_EQ(chunk->records[0].pos, 100);
    EXPECT_EQ(chunk->records[0].cigar, "10M");
    EXPECT_EQ(chunk->records[0].qname, "");
    EXPECT_EQ(chunk->records[1].pos, 150);
    EXPECT_EQ(chunk->records[1].cigar, "5M1I4M");
    EXPECT_EQ(chunk->records[1].quality, "");

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
    write_u16_at(data, column_entry_offset(data, 0) + kColumnIdOffset,
                 static_cast<std::uint16_t>(alignx::format::Axf1ColumnId::mapq));
    write_bytes(path, data);

    auto read = alignx::format::read_axf1_file(path);
    ASSERT_FALSE(read);
    EXPECT_NE(read.error().find("missing required column"), std::string::npos);

    std::filesystem::remove(path);
}

TEST(Axf1File, RejectsDuplicateRequiredColumn) {
    const auto path = temp_path("alignx_axf1_duplicate_column");
    const auto file = make_file();
    auto write = alignx::format::write_axf1_file(file, path);
    ASSERT_TRUE(write) << write.error();

    auto data = read_bytes(path);
    write_u16_at(data, column_entry_offset(data, 10) + kColumnIdOffset,
                 static_cast<std::uint16_t>(alignx::format::Axf1ColumnId::qname));
    write_bytes(path, data);

    auto read = alignx::format::read_axf1_file(path);
    ASSERT_FALSE(read);
    EXPECT_NE(read.error().find("duplicate AXF1 required column"), std::string::npos);

    std::filesystem::remove(path);
}

TEST(Axf1File, RejectsUnknownColumn) {
    const auto path = temp_path("alignx_axf1_unknown_column");
    const auto file = make_file();
    auto write = alignx::format::write_axf1_file(file, path);
    ASSERT_TRUE(write) << write.error();

    auto data = read_bytes(path);
    write_u16_at(data, column_entry_offset(data, 10) + kColumnIdOffset, 99);
    write_bytes(path, data);

    auto read = alignx::format::read_axf1_file(path);
    ASSERT_FALSE(read);
    EXPECT_NE(read.error().find("unknown AXF1 column id"), std::string::npos);

    std::filesystem::remove(path);
}

TEST(Axf1File, RejectsColumnPayloadOutsideChunk) {
    const auto path = temp_path("alignx_axf1_bad_column_range");
    const auto file = make_file();
    auto write = alignx::format::write_axf1_file(file, path);
    ASSERT_TRUE(write) << write.error();

    auto data = read_bytes(path);
    write_u64_at(data, column_entry_offset(data, 0) + kColumnLengthOffset, 1'000'000);
    write_bytes(path, data);

    auto read = alignx::format::read_axf1_file(path);
    ASSERT_FALSE(read);
    EXPECT_NE(read.error().find("column payload points outside chunk"), std::string::npos);

    std::filesystem::remove(path);
}

TEST(Axf1File, RejectsChunkMetadataMismatchWithIndex) {
    const auto path = temp_path("alignx_axf1_chunk_metadata_mismatch");
    const auto file = make_file();
    auto write = alignx::format::write_axf1_file(file, path);
    ASSERT_TRUE(write) << write.error();

    auto data = read_bytes(path);
    write_u32_at(
        data, static_cast<std::size_t>(read_first_chunk_offset(data)) + kChunkStartPosOffset, 101);
    write_bytes(path, data);

    auto read = alignx::format::read_axf1_file(path);
    ASSERT_FALSE(read);
    EXPECT_NE(read.error().find("chunk metadata does not match index"), std::string::npos);

    std::filesystem::remove(path);
}

TEST(Axf1File, RejectsColumnValueCountMismatch) {
    const auto path = temp_path("alignx_axf1_value_count_mismatch");
    const auto file = make_file();
    auto write = alignx::format::write_axf1_file(file, path);
    ASSERT_TRUE(write) << write.error();

    auto data = read_bytes(path);
    write_u32_at(data, static_cast<std::size_t>(read_index_offset(data)) + kIndexRecordCountOffset,
                 3);
    write_u32_at(
        data, static_cast<std::size_t>(read_first_chunk_offset(data)) + kChunkRecordCountOffset, 3);
    write_bytes(path, data);

    auto read = alignx::format::read_axf1_file(path);
    ASSERT_FALSE(read);
    EXPECT_NE(read.error().find("column value count mismatch"), std::string::npos);

    std::filesystem::remove(path);
}

TEST(Axf1File, RejectsColumnTrailingBytes) {
    const auto path = temp_path("alignx_axf1_column_trailing_bytes");
    const auto file = make_file();
    auto write = alignx::format::write_axf1_file(file, path);
    ASSERT_TRUE(write) << write.error();

    auto data = read_bytes(path);
    const std::size_t qname_length_offset = column_entry_offset(data, 0) + kColumnLengthOffset;
    write_u64_at(data, qname_length_offset, read_u64_at(data, qname_length_offset) + 1);
    write_bytes(path, data);

    auto read = alignx::format::read_axf1_file(path);
    ASSERT_FALSE(read);
    EXPECT_NE(read.error().find("string column has trailing bytes"), std::string::npos);

    std::filesystem::remove(path);
}

TEST(Axf1File, RejectsChunkPayloadOutsideFile) {
    const auto path = temp_path("alignx_axf1_bad_chunk_range");
    const auto file = make_file();
    auto write = alignx::format::write_axf1_file(file, path);
    ASSERT_TRUE(write) << write.error();

    auto data = read_bytes(path);
    write_u64_at(data, static_cast<std::size_t>(read_index_offset(data)) + kIndexChunkOffsetOffset,
                 1'000'000);
    write_bytes(path, data);

    auto read = alignx::format::read_axf1_file(path);
    ASSERT_FALSE(read);
    EXPECT_NE(read.error().find("chunk points outside file"), std::string::npos);

    std::filesystem::remove(path);
}
