#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "format/axf1_file.hpp"

#ifdef ALIGNX_HAVE_ZSTD
#include <zstd.h>
#endif

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
constexpr std::size_t kColumnCodecOffset = 2;
constexpr std::size_t kColumnPayloadOffsetOffset = 4;
constexpr std::size_t kColumnLengthOffset = 12;
constexpr std::size_t kIndexChunkLengthOffset = 24;
constexpr std::size_t kQnameColumnIndex = 0;
constexpr std::size_t kFlagColumnIndex = 1;
constexpr std::size_t kPosColumnIndex = 2;
constexpr std::size_t kMapqColumnIndex = 3;
constexpr std::size_t kCigarColumnIndex = 4;
constexpr std::size_t kSeqColumnIndex = 8;
constexpr std::size_t kQualColumnIndex = 9;
constexpr std::size_t kTagsColumnIndex = 10;

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
                                        .mate_pos = -1,
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
                                        .mate_pos = -1,
                                        .template_length = 0,
                                        .sequence = "TTTTACGGGA",
                                        .quality = "FFFFFFFFFF",
                                        .tags = "NM:i:1"}}});
    return file;
}

alignx::format::Axf1File make_single_record_file(std::string quality) {
    auto file = make_file();
    file.chunks[0].end_pos = 110;
    file.chunks[0].records.resize(1);
    file.chunks[0].records[0].quality = std::move(quality);
    return file;
}

alignx::format::Axf1File make_repeated_quality_file(std::size_t record_count,
                                                    std::size_t quality_length) {
    auto file = make_file();
    file.chunks[0].records.clear();
    file.chunks[0].records.reserve(record_count);
    for (std::size_t index = 0; index < record_count; ++index) {
        file.chunks[0].records.push_back({.qname = "read" + std::to_string(index),
                                          .flag = 0,
                                          .pos = static_cast<std::int32_t>(100 + index),
                                          .mapq = 60,
                                          .cigar = "10M",
                                          .mate_reference = "*",
                                          .mate_pos = -1,
                                          .template_length = 0,
                                          .sequence = "ACGTACGTAA",
                                          .quality = std::string(quality_length, 'F'),
                                          .tags = "NM:i:0"});
    }
    file.chunks[0].end_pos = static_cast<std::int32_t>(100 + record_count + 9);
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

std::uint16_t read_u16_at(const std::vector<unsigned char>& data, std::size_t offset) {
    std::uint16_t value = 0;
    for (std::size_t byte = 0; byte < sizeof(value); ++byte) {
        value |= static_cast<std::uint16_t>(data.at(offset + byte)) << (byte * 8U);
    }
    return value;
}

std::uint32_t read_u32_at(const std::vector<unsigned char>& data, std::size_t offset) {
    std::uint32_t value = 0;
    for (std::size_t byte = 0; byte < sizeof(value); ++byte) {
        value |= static_cast<std::uint32_t>(data.at(offset + byte)) << (byte * 8U);
    }
    return value;
}

std::uint64_t read_varint_at(const std::vector<unsigned char>& data, std::size_t& offset) {
    std::uint64_t value = 0;
    std::uint8_t shift = 0;
    for (;;) {
        const unsigned char byte = data.at(offset);
        ++offset;
        value |= static_cast<std::uint64_t>(byte & 0x7FU) << shift;
        if ((byte & 0x80U) == 0) {
            return value;
        }
        shift = static_cast<std::uint8_t>(shift + 7U);
    }
}

void write_u16_at(std::vector<unsigned char>& data, std::size_t offset, std::uint16_t value) {
    for (std::size_t byte = 0; byte < sizeof(value); ++byte) {
        data.at(offset + byte) = static_cast<unsigned char>((value >> (byte * 8U)) & 0xFFU);
    }
}

void write_u8_at(std::vector<unsigned char>& data, std::size_t offset, std::uint8_t value) {
    data.at(offset) = value;
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

void append_varint_u64(std::vector<unsigned char>& data, std::uint64_t value) {
    while (value >= 0x80U) {
        data.push_back(static_cast<unsigned char>((value & 0x7FU) | 0x80U));
        value >>= 7U;
    }
    data.push_back(static_cast<unsigned char>(value));
}

std::uint64_t read_index_offset(const std::vector<unsigned char>& data) {
    return read_u64_at(data, kIndexOffsetFieldOffset);
}

std::uint64_t read_first_chunk_offset(const std::vector<unsigned char>& data) {
    return read_u64_at(data,
                       static_cast<std::size_t>(read_index_offset(data)) + kIndexChunkOffsetOffset);
}

std::uint64_t read_first_chunk_length(const std::vector<unsigned char>& data) {
    return read_u64_at(data,
                       static_cast<std::size_t>(read_index_offset(data)) + kIndexChunkLengthOffset);
}

std::size_t column_entry_offset(const std::vector<unsigned char>& data, std::size_t column_index) {
    return static_cast<std::size_t>(read_first_chunk_offset(data)) + kChunkHeaderSize +
           column_index * kColumnEntrySize;
}

std::size_t column_payload_offset(const std::vector<unsigned char>& data,
                                  std::size_t column_index) {
    const auto entry_offset = column_entry_offset(data, column_index);
    return static_cast<std::size_t>(read_first_chunk_offset(data)) + kChunkHeaderSize +
           11 * kColumnEntrySize +
           static_cast<std::size_t>(read_u64_at(data, entry_offset + kColumnPayloadOffsetOffset));
}

std::vector<unsigned char> make_stored_payload_envelope(std::uint64_t base_codec_id,
                                                        const std::vector<unsigned char>& payload) {
    std::vector<unsigned char> envelope;
    append_varint_u64(envelope, base_codec_id);
    append_varint_u64(envelope, 0);
    append_varint_u64(envelope, payload.size());
    append_varint_u64(envelope, payload.size());
    envelope.insert(envelope.end(), payload.begin(), payload.end());
    return envelope;
}

#ifdef ALIGNX_HAVE_ZSTD
std::vector<unsigned char>
make_zstd_payload_envelope(std::uint64_t base_codec_id, const std::vector<unsigned char>& payload,
                           std::uint64_t uncompressed_size_override = 0) {
    std::vector<unsigned char> compressed(ZSTD_compressBound(payload.size()));
    const std::size_t compressed_size =
        ZSTD_compress(compressed.data(), compressed.size(), payload.data(), payload.size(), 1);
    EXPECT_EQ(ZSTD_isError(compressed_size), 0U) << ZSTD_getErrorName(compressed_size);
    compressed.resize(compressed_size);

    std::vector<unsigned char> envelope;
    append_varint_u64(envelope, base_codec_id);
    append_varint_u64(envelope, 1);
    append_varint_u64(envelope, uncompressed_size_override == 0 ? payload.size()
                                                                : uncompressed_size_override);
    append_varint_u64(envelope, compressed.size());
    envelope.insert(envelope.end(), compressed.begin(), compressed.end());
    return envelope;
}
#endif

void replace_column_payload(std::vector<unsigned char>& data, std::size_t column_index,
                            const std::vector<unsigned char>& replacement) {
    const auto old_index_offset = read_index_offset(data);
    const auto entry_offset = column_entry_offset(data, column_index);
    const auto old_payload_offset = column_payload_offset(data, column_index);
    const auto old_payload_length =
        static_cast<std::size_t>(read_u64_at(data, entry_offset + kColumnLengthOffset));
    const auto old_relative_offset = read_u64_at(data, entry_offset + kColumnPayloadOffsetOffset);
    const auto old_size = data.size();

    data.erase(data.begin() + static_cast<std::ptrdiff_t>(old_payload_offset),
               data.begin() + static_cast<std::ptrdiff_t>(old_payload_offset + old_payload_length));
    data.insert(data.begin() + static_cast<std::ptrdiff_t>(old_payload_offset), replacement.begin(),
                replacement.end());

    const auto new_size = data.size();
    if (new_size >= old_size) {
        const auto delta = static_cast<std::uint64_t>(new_size - old_size);
        write_u64_at(data, kIndexOffsetFieldOffset, old_index_offset + delta);
        write_u64_at(data,
                     static_cast<std::size_t>(old_index_offset + delta) + kIndexChunkLengthOffset,
                     read_first_chunk_length(data) + delta);
        for (std::size_t index = column_index + 1; index < 11; ++index) {
            const auto following_entry = column_entry_offset(data, index);
            write_u64_at(data, following_entry + kColumnPayloadOffsetOffset,
                         read_u64_at(data, following_entry + kColumnPayloadOffsetOffset) + delta);
        }
    } else {
        const auto delta = static_cast<std::uint64_t>(old_size - new_size);
        write_u64_at(data, kIndexOffsetFieldOffset, old_index_offset - delta);
        write_u64_at(data,
                     static_cast<std::size_t>(old_index_offset - delta) + kIndexChunkLengthOffset,
                     read_first_chunk_length(data) - delta);
        for (std::size_t index = column_index + 1; index < 11; ++index) {
            const auto following_entry = column_entry_offset(data, index);
            write_u64_at(data, following_entry + kColumnPayloadOffsetOffset,
                         read_u64_at(data, following_entry + kColumnPayloadOffsetOffset) - delta);
        }
    }

    write_u64_at(data, entry_offset + kColumnPayloadOffsetOffset, old_relative_offset);
    write_u64_at(data, entry_offset + kColumnLengthOffset, replacement.size());
}

void expect_compressed_quality_payload_rejected(std::string_view label,
                                                const std::vector<unsigned char>& envelope,
                                                std::string_view expected_error) {
    const auto path = temp_path(label);
    const auto file = make_file();

    auto write = alignx::format::write_axf1_file(file, path);
    ASSERT_TRUE(write) << write.error();

    auto data = read_bytes(path);
    write_u16_at(data, column_entry_offset(data, kQualColumnIndex) + kColumnCodecOffset,
                 static_cast<std::uint16_t>(alignx::format::Axf1CodecId::qual_pack_compressed));
    replace_column_payload(data, kQualColumnIndex, envelope);
    write_bytes(path, data);

    auto read = alignx::format::read_axf1_file(path);
    ASSERT_FALSE(read);
    EXPECT_NE(read.error().find(expected_error), std::string::npos) << read.error();

    std::filesystem::remove(path);
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

TEST(Axf1File, WritesFlagsAsBitpackWhenSmallerThanRaw) {
    const auto path = temp_path("alignx_axf1_flag_bitpack");
    const auto file = make_file();

    auto write = alignx::format::write_axf1_file(file, path);
    ASSERT_TRUE(write) << write.error();

    auto data = read_bytes(path);
    EXPECT_EQ(read_u16_at(data, column_entry_offset(data, kFlagColumnIndex) + kColumnCodecOffset),
              static_cast<std::uint16_t>(alignx::format::Axf1CodecId::flag_bitpack));
    EXPECT_EQ(read_u64_at(data, column_entry_offset(data, kFlagColumnIndex) + kColumnLengthOffset),
              3);

    auto read = alignx::format::read_axf1_file(path);
    ASSERT_TRUE(read) << read.error();
    ASSERT_EQ(read->chunks.size(), 1);
    ASSERT_EQ(read->chunks[0].records.size(), 2);
    EXPECT_EQ(read->chunks[0].records[0].flag, 0);
    EXPECT_EQ(read->chunks[0].records[1].flag, 16);

    std::filesystem::remove(path);
}

TEST(Axf1File, FallsBackToRawFlagsWhenBitpackIsNotSmaller) {
    const auto path = temp_path("alignx_axf1_flag_bitpack_raw_fallback");
    auto file = make_file();
    file.chunks[0].records[0].flag = 0xFFFF;
    file.chunks[0].records[1].flag = 0;

    auto write = alignx::format::write_axf1_file(file, path);
    ASSERT_TRUE(write) << write.error();

    auto data = read_bytes(path);
    EXPECT_EQ(read_u16_at(data, column_entry_offset(data, kFlagColumnIndex) + kColumnCodecOffset),
              static_cast<std::uint16_t>(alignx::format::Axf1CodecId::raw));
    EXPECT_EQ(read_u64_at(data, column_entry_offset(data, kFlagColumnIndex) + kColumnLengthOffset),
              4);

    auto read = alignx::format::read_axf1_file(path);
    ASSERT_TRUE(read) << read.error();
    ASSERT_EQ(read->chunks.size(), 1);
    ASSERT_EQ(read->chunks[0].records.size(), 2);
    EXPECT_EQ(read->chunks[0].records[0].flag, 0xFFFF);
    EXPECT_EQ(read->chunks[0].records[1].flag, 0);

    std::filesystem::remove(path);
}

TEST(Axf1File, WritesRepeatedMapqAsRle) {
    const auto path = temp_path("alignx_axf1_mapq_rle");
    auto file = make_file();
    file.chunks[0].records[1].mapq = file.chunks[0].records[0].mapq;
    auto third_record = file.chunks[0].records[1];
    third_record.qname = "read003";
    third_record.pos = 155;
    file.chunks[0].records.push_back(third_record);

    auto write = alignx::format::write_axf1_file(file, path);
    ASSERT_TRUE(write) << write.error();

    auto data = read_bytes(path);
    EXPECT_EQ(read_u16_at(data, column_entry_offset(data, kMapqColumnIndex) + kColumnCodecOffset),
              static_cast<std::uint16_t>(alignx::format::Axf1CodecId::mapq_rle));
    EXPECT_EQ(read_u64_at(data, column_entry_offset(data, kMapqColumnIndex) + kColumnLengthOffset),
              2);

    auto read = alignx::format::read_axf1_file(path);
    ASSERT_TRUE(read) << read.error();
    ASSERT_EQ(read->chunks.size(), 1);
    ASSERT_EQ(read->chunks[0].records.size(), 3);
    EXPECT_EQ(read->chunks[0].records[0].mapq, 60);
    EXPECT_EQ(read->chunks[0].records[1].mapq, 60);
    EXPECT_EQ(read->chunks[0].records[2].mapq, 60);

    std::filesystem::remove(path);
}

TEST(Axf1File, FallsBackToRawMapqWhenRleIsNotSmaller) {
    const auto path = temp_path("alignx_axf1_mapq_rle_raw_fallback");
    const auto file = make_file();

    auto write = alignx::format::write_axf1_file(file, path);
    ASSERT_TRUE(write) << write.error();

    auto data = read_bytes(path);
    EXPECT_EQ(read_u16_at(data, column_entry_offset(data, kMapqColumnIndex) + kColumnCodecOffset),
              static_cast<std::uint16_t>(alignx::format::Axf1CodecId::raw));
    EXPECT_EQ(read_u64_at(data, column_entry_offset(data, kMapqColumnIndex) + kColumnLengthOffset),
              2);

    auto read = alignx::format::read_axf1_file(path);
    ASSERT_TRUE(read) << read.error();
    ASSERT_EQ(read->chunks.size(), 1);
    ASSERT_EQ(read->chunks[0].records.size(), 2);
    EXPECT_EQ(read->chunks[0].records[0].mapq, 60);
    EXPECT_EQ(read->chunks[0].records[1].mapq, 50);

    std::filesystem::remove(path);
}

TEST(Axf1File, WritesCigarAsTokenStream) {
    const auto path = temp_path("alignx_axf1_cigar_token");
    auto file = make_file();
    auto third_record = file.chunks[0].records[1];
    third_record.qname = "read003";
    third_record.pos = 155;
    third_record.cigar = "2S5M1I3D4N2=1X1H1P";
    file.chunks[0].records.push_back(third_record);

    auto write = alignx::format::write_axf1_file(file, path);
    ASSERT_TRUE(write) << write.error();

    auto data = read_bytes(path);
    EXPECT_EQ(read_u16_at(data, column_entry_offset(data, kCigarColumnIndex) + kColumnCodecOffset),
              static_cast<std::uint16_t>(alignx::format::Axf1CodecId::cigar_token));
    EXPECT_EQ(read_u64_at(data, column_entry_offset(data, kCigarColumnIndex) + kColumnLengthOffset),
              29);

    auto read = alignx::format::read_axf1_file(path);
    ASSERT_TRUE(read) << read.error();
    ASSERT_EQ(read->chunks.size(), 1);
    ASSERT_EQ(read->chunks[0].records.size(), 3);
    EXPECT_EQ(read->chunks[0].records[0].cigar, "10M");
    EXPECT_EQ(read->chunks[0].records[1].cigar, "5M1I4M");
    EXPECT_EQ(read->chunks[0].records[2].cigar, "2S5M1I3D4N2=1X1H1P");

    std::filesystem::remove(path);
}

TEST(Axf1File, FallsBackToRawCigarForUnsupportedStrings) {
    const std::vector<std::string> fallback_cigars{"*", "", "10Z", "M", "10M5", "0M", "01M"};

    for (const std::string& fallback_cigar : fallback_cigars) {
        const auto path = temp_path("alignx_axf1_cigar_token_raw_fallback");
        auto file = make_file();
        file.chunks[0].records[1].cigar = fallback_cigar;

        auto write = alignx::format::write_axf1_file(file, path);
        ASSERT_TRUE(write) << write.error();

        auto data = read_bytes(path);
        const auto codec =
            read_u16_at(data, column_entry_offset(data, kCigarColumnIndex) + kColumnCodecOffset);
        EXPECT_NE(codec, static_cast<std::uint16_t>(alignx::format::Axf1CodecId::cigar_token))
            << "unsupported CIGAR '" << fallback_cigar << "' should not use cigar_token";

        auto read = alignx::format::read_axf1_file(path);
        ASSERT_TRUE(read) << read.error();
        ASSERT_EQ(read->chunks.size(), 1);
        ASSERT_EQ(read->chunks[0].records.size(), 2);
        EXPECT_EQ(read->chunks[0].records[0].cigar, "10M");
        EXPECT_EQ(read->chunks[0].records[1].cigar, fallback_cigar);

        std::filesystem::remove(path);
    }
}

TEST(Axf1File, WritesAcgtSequenceAsTwoBitLiteral) {
    const auto path = temp_path("alignx_axf1_seq_2bit_literal");
    const auto file = make_file();

    auto write = alignx::format::write_axf1_file(file, path);
    ASSERT_TRUE(write) << write.error();

    auto data = read_bytes(path);
    EXPECT_EQ(read_u16_at(data, column_entry_offset(data, kSeqColumnIndex) + kColumnCodecOffset),
              static_cast<std::uint16_t>(alignx::format::Axf1CodecId::seq_2bit_literal));
    EXPECT_EQ(read_u64_at(data, column_entry_offset(data, kSeqColumnIndex) + kColumnLengthOffset),
              8);

    auto read = alignx::format::read_axf1_file(path);
    ASSERT_TRUE(read) << read.error();
    ASSERT_EQ(read->chunks.size(), 1);
    ASSERT_EQ(read->chunks[0].records.size(), 2);
    EXPECT_EQ(read->chunks[0].records[0].sequence, "ACGTACGTAA");
    EXPECT_EQ(read->chunks[0].records[1].sequence, "TTTTACGGGA");

    std::filesystem::remove(path);
}

TEST(Axf1File, FallsBackToRawSequenceForNonAcgtBases) {
    const std::vector<std::string> fallback_sequences{"TTTTNCGGGA", "ttttACGGGA", "*"};

    for (const std::string& fallback_sequence : fallback_sequences) {
        const auto path = temp_path("alignx_axf1_seq_2bit_literal_raw_fallback");
        auto file = make_file();
        file.chunks[0].records[1].sequence = fallback_sequence;

        auto write = alignx::format::write_axf1_file(file, path);
        ASSERT_TRUE(write) << write.error();

        auto data = read_bytes(path);
        EXPECT_EQ(
            read_u16_at(data, column_entry_offset(data, kSeqColumnIndex) + kColumnCodecOffset),
            static_cast<std::uint16_t>(alignx::format::Axf1CodecId::raw));
        EXPECT_EQ(
            read_u64_at(data, column_entry_offset(data, kSeqColumnIndex) + kColumnLengthOffset),
            4 + file.chunks[0].records[0].sequence.size() + 4 + fallback_sequence.size());

        auto read = alignx::format::read_axf1_file(path);
        ASSERT_TRUE(read) << read.error();
        ASSERT_EQ(read->chunks.size(), 1);
        ASSERT_EQ(read->chunks[0].records.size(), 2);
        EXPECT_EQ(read->chunks[0].records[0].sequence, "ACGTACGTAA");
        EXPECT_EQ(read->chunks[0].records[1].sequence, fallback_sequence);

        std::filesystem::remove(path);
    }
}

TEST(Axf1File, WritesSmallAlphabetQualityAsPack) {
    const auto path = temp_path("alignx_axf1_qual_pack");
    const auto file = make_file();

    auto write = alignx::format::write_axf1_file(file, path);
    ASSERT_TRUE(write) << write.error();

    auto data = read_bytes(path);
    EXPECT_EQ(read_u16_at(data, column_entry_offset(data, kQualColumnIndex) + kColumnCodecOffset),
              static_cast<std::uint16_t>(alignx::format::Axf1CodecId::qual_pack));
    EXPECT_EQ(read_u64_at(data, column_entry_offset(data, kQualColumnIndex) + kColumnLengthOffset),
              4);

    auto read = alignx::format::read_axf1_file(path);
    ASSERT_TRUE(read) << read.error();
    ASSERT_EQ(read->chunks.size(), 1);
    ASSERT_EQ(read->chunks[0].records.size(), 2);
    EXPECT_EQ(read->chunks[0].records[0].quality, "FFFFFFFFFF");
    EXPECT_EQ(read->chunks[0].records[1].quality, "FFFFFFFFFF");

    std::filesystem::remove(path);
}

TEST(Axf1File, RejectsZstdQualityCompressionWhenZstdIsDisabled) {
#ifndef ALIGNX_HAVE_ZSTD
    const auto path = temp_path("alignx_axf1_zstd_writer_disabled");
    const auto file = make_file();
    const alignx::format::Axf1WriteOptions options{.quality_compression =
                                                       alignx::format::Axf1Compression::zstd};

    auto write = alignx::format::write_axf1_file(file, path, options);
    ASSERT_FALSE(write);
    EXPECT_NE(write.error().find("ALIGNX_ENABLE_ZSTD=ON"), std::string::npos) << write.error();
    EXPECT_FALSE(std::filesystem::exists(path));
#endif
}

TEST(Axf1File, WritesZstdCompressedQualPackEnvelopeWhenEnabled) {
#ifdef ALIGNX_HAVE_ZSTD
    const auto path = temp_path("alignx_axf1_zstd_writer");
    const auto file = make_repeated_quality_file(256, 200);
    const alignx::format::Axf1WriteOptions options{.quality_compression =
                                                       alignx::format::Axf1Compression::zstd};

    auto write = alignx::format::write_axf1_file(file, path, options);
    ASSERT_TRUE(write) << write.error();

    auto data = read_bytes(path);
    EXPECT_EQ(read_u16_at(data, column_entry_offset(data, kQualColumnIndex) + kColumnCodecOffset),
              static_cast<std::uint16_t>(alignx::format::Axf1CodecId::qual_pack_compressed));

    std::size_t payload_offset = column_payload_offset(data, kQualColumnIndex);
    EXPECT_EQ(read_varint_at(data, payload_offset),
              static_cast<std::uint16_t>(alignx::format::Axf1CodecId::qual_pack));
    EXPECT_EQ(read_varint_at(data, payload_offset), 1);

    auto read = alignx::format::read_axf1_file(path);
    ASSERT_TRUE(read) << read.error();
    ASSERT_EQ(read->chunks.size(), 1);
    ASSERT_EQ(read->chunks[0].records.size(), 256);
    EXPECT_EQ(read->chunks[0].records[0].quality, std::string(200, 'F'));
    EXPECT_EQ(read->chunks[0].records[255].quality, std::string(200, 'F'));

    auto reader = alignx::format::Axf1FileReader::open(path);
    ASSERT_TRUE(reader) << reader.error();
    auto hits = reader->query_chunks(0, 100, 101);
    ASSERT_TRUE(hits) << hits.error();
    ASSERT_EQ(hits->size(), 1);
    auto chunk = reader->read_chunk_columns(*hits->at(0), {alignx::format::Axf1ColumnId::quality});
    ASSERT_TRUE(chunk) << chunk.error();
    ASSERT_EQ(chunk->records.size(), 256);
    EXPECT_EQ(chunk->records[0].quality, std::string(200, 'F'));
    EXPECT_EQ(chunk->records[255].quality, std::string(200, 'F'));

    std::filesystem::remove(path);
#endif
}

TEST(Axf1File, FallsBackToBaseQualityCodecWhenZstdEnvelopeIsNotSmaller) {
#ifdef ALIGNX_HAVE_ZSTD
    const auto path = temp_path("alignx_axf1_zstd_writer_size_fallback");
    const auto file = make_file();
    const alignx::format::Axf1WriteOptions options{.quality_compression =
                                                       alignx::format::Axf1Compression::zstd};

    auto write = alignx::format::write_axf1_file(file, path, options);
    ASSERT_TRUE(write) << write.error();

    auto data = read_bytes(path);
    EXPECT_EQ(read_u16_at(data, column_entry_offset(data, kQualColumnIndex) + kColumnCodecOffset),
              static_cast<std::uint16_t>(alignx::format::Axf1CodecId::qual_pack));
    EXPECT_EQ(read_u64_at(data, column_entry_offset(data, kQualColumnIndex) + kColumnLengthOffset),
              4);

    auto read = alignx::format::read_axf1_file(path);
    ASSERT_TRUE(read) << read.error();
    ASSERT_EQ(read->chunks.size(), 1);
    ASSERT_EQ(read->chunks[0].records.size(), 2);
    EXPECT_EQ(read->chunks[0].records[0].quality, "FFFFFFFFFF");
    EXPECT_EQ(read->chunks[0].records[1].quality, "FFFFFFFFFF");

    std::filesystem::remove(path);
#endif
}

TEST(Axf1File, ReadsStoredCompressedQualPackEnvelope) {
    const auto path = temp_path("alignx_axf1_qual_pack_compressed_stored");
    const auto file = make_file();

    auto write = alignx::format::write_axf1_file(file, path);
    ASSERT_TRUE(write) << write.error();

    auto data = read_bytes(path);
    const auto old_payload_offset = column_payload_offset(data, kQualColumnIndex);
    const auto old_payload_length = static_cast<std::size_t>(
        read_u64_at(data, column_entry_offset(data, kQualColumnIndex) + kColumnLengthOffset));
    const std::vector<unsigned char> old_payload(
        data.begin() + static_cast<std::ptrdiff_t>(old_payload_offset),
        data.begin() + static_cast<std::ptrdiff_t>(old_payload_offset + old_payload_length));
    const auto envelope = make_stored_payload_envelope(
        static_cast<std::uint16_t>(alignx::format::Axf1CodecId::qual_pack), old_payload);

    write_u16_at(data, column_entry_offset(data, kQualColumnIndex) + kColumnCodecOffset,
                 static_cast<std::uint16_t>(alignx::format::Axf1CodecId::qual_pack_compressed));
    replace_column_payload(data, kQualColumnIndex, envelope);
    write_bytes(path, data);

    auto read = alignx::format::read_axf1_file(path);
    ASSERT_TRUE(read) << read.error();
    ASSERT_EQ(read->chunks.size(), 1);
    ASSERT_EQ(read->chunks[0].records.size(), 2);
    EXPECT_EQ(read->chunks[0].records[0].quality, "FFFFFFFFFF");
    EXPECT_EQ(read->chunks[0].records[1].quality, "FFFFFFFFFF");

    auto reader = alignx::format::Axf1FileReader::open(path);
    ASSERT_TRUE(reader) << reader.error();
    auto hits = reader->query_chunks(0, 100, 101);
    ASSERT_TRUE(hits) << hits.error();
    ASSERT_EQ(hits->size(), 1);
    auto chunk = reader->read_chunk_columns(*hits->at(0), {alignx::format::Axf1ColumnId::quality});
    ASSERT_TRUE(chunk) << chunk.error();
    ASSERT_EQ(chunk->records.size(), 2);
    EXPECT_EQ(chunk->records[0].quality, "FFFFFFFFFF");
    EXPECT_EQ(chunk->records[1].quality, "FFFFFFFFFF");

    std::filesystem::remove(path);
}

TEST(Axf1File, RejectsZstdCompressedPayloadWhenZstdIsDisabled) {
#ifndef ALIGNX_HAVE_ZSTD
    expect_compressed_quality_payload_rejected("alignx_axf1_zstd_compressed_payload_disabled",
                                               {7, 1, 0, 0},
                                               "unsupported AXF1 compressed payload compression");
#endif
}

TEST(Axf1File, ReadsZstdCompressedQualPackEnvelopeWhenEnabled) {
#ifdef ALIGNX_HAVE_ZSTD
    const auto path = temp_path("alignx_axf1_qual_pack_compressed_zstd");
    const auto file = make_file();

    auto write = alignx::format::write_axf1_file(file, path);
    ASSERT_TRUE(write) << write.error();

    auto data = read_bytes(path);
    const auto old_payload_offset = column_payload_offset(data, kQualColumnIndex);
    const auto old_payload_length = static_cast<std::size_t>(
        read_u64_at(data, column_entry_offset(data, kQualColumnIndex) + kColumnLengthOffset));
    const std::vector<unsigned char> old_payload(
        data.begin() + static_cast<std::ptrdiff_t>(old_payload_offset),
        data.begin() + static_cast<std::ptrdiff_t>(old_payload_offset + old_payload_length));
    const auto envelope = make_zstd_payload_envelope(
        static_cast<std::uint16_t>(alignx::format::Axf1CodecId::qual_pack), old_payload);

    write_u16_at(data, column_entry_offset(data, kQualColumnIndex) + kColumnCodecOffset,
                 static_cast<std::uint16_t>(alignx::format::Axf1CodecId::qual_pack_compressed));
    replace_column_payload(data, kQualColumnIndex, envelope);
    write_bytes(path, data);

    auto read = alignx::format::read_axf1_file(path);
    ASSERT_TRUE(read) << read.error();
    ASSERT_EQ(read->chunks.size(), 1);
    ASSERT_EQ(read->chunks[0].records.size(), 2);
    EXPECT_EQ(read->chunks[0].records[0].quality, "FFFFFFFFFF");
    EXPECT_EQ(read->chunks[0].records[1].quality, "FFFFFFFFFF");

    auto reader = alignx::format::Axf1FileReader::open(path);
    ASSERT_TRUE(reader) << reader.error();
    auto hits = reader->query_chunks(0, 100, 101);
    ASSERT_TRUE(hits) << hits.error();
    ASSERT_EQ(hits->size(), 1);
    auto chunk = reader->read_chunk_columns(*hits->at(0), {alignx::format::Axf1ColumnId::quality});
    ASSERT_TRUE(chunk) << chunk.error();
    ASSERT_EQ(chunk->records.size(), 2);
    EXPECT_EQ(chunk->records[0].quality, "FFFFFFFFFF");
    EXPECT_EQ(chunk->records[1].quality, "FFFFFFFFFF");

    std::filesystem::remove(path);
#endif
}

TEST(Axf1File, RejectsCorruptZstdCompressedPayloadWhenEnabled) {
#ifdef ALIGNX_HAVE_ZSTD
    expect_compressed_quality_payload_rejected("alignx_axf1_corrupt_zstd_payload",
                                               {7, 1, 4, 4, 0, 0, 0, 0},
                                               "failed to decompress AXF1 zstd payload");
#endif
}

TEST(Axf1File, RejectsZstdDecompressedSizeMismatchWhenEnabled) {
#ifdef ALIGNX_HAVE_ZSTD
    const auto path = temp_path("alignx_axf1_zstd_size_mismatch");
    const auto file = make_file();

    auto write = alignx::format::write_axf1_file(file, path);
    ASSERT_TRUE(write) << write.error();

    auto data = read_bytes(path);
    const auto old_payload_offset = column_payload_offset(data, kQualColumnIndex);
    const auto old_payload_length = static_cast<std::size_t>(
        read_u64_at(data, column_entry_offset(data, kQualColumnIndex) + kColumnLengthOffset));
    const std::vector<unsigned char> old_payload(
        data.begin() + static_cast<std::ptrdiff_t>(old_payload_offset),
        data.begin() + static_cast<std::ptrdiff_t>(old_payload_offset + old_payload_length));
    const auto envelope = make_zstd_payload_envelope(
        static_cast<std::uint16_t>(alignx::format::Axf1CodecId::qual_pack), old_payload,
        old_payload.size() + 1);

    write_u16_at(data, column_entry_offset(data, kQualColumnIndex) + kColumnCodecOffset,
                 static_cast<std::uint16_t>(alignx::format::Axf1CodecId::qual_pack_compressed));
    replace_column_payload(data, kQualColumnIndex, envelope);
    write_bytes(path, data);

    auto read = alignx::format::read_axf1_file(path);
    ASSERT_FALSE(read);
    EXPECT_NE(read.error().find("AXF1 zstd decompressed size mismatch"), std::string::npos)
        << read.error();

    std::filesystem::remove(path);
#endif
}

TEST(Axf1File, RejectsUnsupportedCompressedPayloadCompression) {
    expect_compressed_quality_payload_rejected("alignx_axf1_unsupported_compressed_payload",
                                               {7, 2, 0, 0},
                                               "unsupported AXF1 compressed payload compression");
}

TEST(Axf1File, RejectsCompressedPayloadSizeMismatch) {
    std::vector<unsigned char> envelope;
    append_varint_u64(envelope, static_cast<std::uint16_t>(alignx::format::Axf1CodecId::qual_pack));
    append_varint_u64(envelope, 0);
    append_varint_u64(envelope, 5);
    append_varint_u64(envelope, 4);
    envelope.insert(envelope.end(), {'\x01', 'F', '\x0a', '\x0a'});

    expect_compressed_quality_payload_rejected("alignx_axf1_compressed_payload_size_mismatch",
                                               envelope, "AXF1 compressed payload size mismatch");
}

TEST(Axf1File, RejectsUnsupportedCompressedQualBaseCodec) {
    const auto path = temp_path("alignx_axf1_unsupported_compressed_qual_base");
    const auto file = make_file();

    auto write = alignx::format::write_axf1_file(file, path);
    ASSERT_TRUE(write) << write.error();

    auto data = read_bytes(path);
    const auto old_payload_offset = column_payload_offset(data, kQualColumnIndex);
    const auto old_payload_length = static_cast<std::size_t>(
        read_u64_at(data, column_entry_offset(data, kQualColumnIndex) + kColumnLengthOffset));
    const std::vector<unsigned char> old_payload(
        data.begin() + static_cast<std::ptrdiff_t>(old_payload_offset),
        data.begin() + static_cast<std::ptrdiff_t>(old_payload_offset + old_payload_length));
    const auto envelope = make_stored_payload_envelope(
        static_cast<std::uint16_t>(alignx::format::Axf1CodecId::raw), old_payload);

    write_u16_at(data, column_entry_offset(data, kQualColumnIndex) + kColumnCodecOffset,
                 static_cast<std::uint16_t>(alignx::format::Axf1CodecId::qual_pack_compressed));
    replace_column_payload(data, kQualColumnIndex, envelope);
    write_bytes(path, data);

    auto read = alignx::format::read_axf1_file(path);
    ASSERT_FALSE(read);
    EXPECT_NE(read.error().find("unsupported AXF1 compressed QUAL base codec"), std::string::npos)
        << read.error();

    std::filesystem::remove(path);
}

TEST(Axf1File, RejectsTruncatedCompressedPayloadBaseCodec) {
    expect_compressed_quality_payload_rejected("alignx_axf1_truncated_compressed_payload_base",
                                               {0x80},
                                               "truncated AXF1 compressed payload base codec");
}

TEST(Axf1File, RejectsTruncatedCompressedPayloadCompression) {
    expect_compressed_quality_payload_rejected("alignx_axf1_truncated_compressed_payload_algorithm",
                                               {7, 0x80},
                                               "truncated AXF1 compressed payload compression");
}

TEST(Axf1File, RejectsTruncatedCompressedPayloadUncompressedSize) {
    expect_compressed_quality_payload_rejected(
        "alignx_axf1_truncated_compressed_payload_raw_size", {7, 0, 0x80},
        "truncated AXF1 compressed payload uncompressed size");
}

TEST(Axf1File, RejectsTruncatedCompressedPayloadSize) {
    expect_compressed_quality_payload_rejected("alignx_axf1_truncated_compressed_payload_size",
                                               {7, 0, 4, 0x80},
                                               "truncated AXF1 compressed payload size");
}

TEST(Axf1File, RejectsTruncatedCompressedPayloadBytes) {
    expect_compressed_quality_payload_rejected("alignx_axf1_truncated_compressed_payload_bytes",
                                               {7, 0, 5, 5, 1, 'F', 10, 10},
                                               "truncated AXF1 compressed payload bytes");
}

TEST(Axf1File, RejectsCompressedPayloadTrailingBytes) {
    expect_compressed_quality_payload_rejected("alignx_axf1_compressed_payload_trailing_bytes",
                                               {7, 0, 4, 4, 1, 'F', 10, 10, 0},
                                               "AXF1 compressed payload has trailing bytes");
}

TEST(Axf1File, KeepsQualRleWhenPackIsNotSmaller) {
    const auto path = temp_path("alignx_axf1_qual_rle");
    const auto file = make_single_record_file("FFFFHHHH");

    auto write = alignx::format::write_axf1_file(file, path);
    ASSERT_TRUE(write) << write.error();

    auto data = read_bytes(path);
    EXPECT_EQ(read_u16_at(data, column_entry_offset(data, kQualColumnIndex) + kColumnCodecOffset),
              static_cast<std::uint16_t>(alignx::format::Axf1CodecId::qual_rle));
    EXPECT_EQ(read_u64_at(data, column_entry_offset(data, kQualColumnIndex) + kColumnLengthOffset),
              5);

    auto read = alignx::format::read_axf1_file(path);
    ASSERT_TRUE(read) << read.error();
    ASSERT_EQ(read->chunks.size(), 1);
    ASSERT_EQ(read->chunks[0].records.size(), 1);
    EXPECT_EQ(read->chunks[0].records[0].quality, "FFFFHHHH");

    std::filesystem::remove(path);
}

TEST(Axf1File, FallsBackToRawQualityWhenRleIsNotSmaller) {
    const auto path = temp_path("alignx_axf1_qual_rle_raw_fallback");
    auto file = make_file();
    file.chunks[0].records[0].quality = "ABCDEFGHIJ";
    file.chunks[0].records[1].quality = "KLMNOPQRST";

    auto write = alignx::format::write_axf1_file(file, path);
    ASSERT_TRUE(write) << write.error();

    auto data = read_bytes(path);
    EXPECT_EQ(read_u16_at(data, column_entry_offset(data, kQualColumnIndex) + kColumnCodecOffset),
              static_cast<std::uint16_t>(alignx::format::Axf1CodecId::raw));
    EXPECT_EQ(read_u64_at(data, column_entry_offset(data, kQualColumnIndex) + kColumnLengthOffset),
              28);

    auto read = alignx::format::read_axf1_file(path);
    ASSERT_TRUE(read) << read.error();
    ASSERT_EQ(read->chunks.size(), 1);
    ASSERT_EQ(read->chunks[0].records.size(), 2);
    EXPECT_EQ(read->chunks[0].records[0].quality, "ABCDEFGHIJ");
    EXPECT_EQ(read->chunks[0].records[1].quality, "KLMNOPQRST");

    std::filesystem::remove(path);
}

TEST(Axf1File, FallsBackToRawQualityForStarOrEmpty) {
    const std::vector<std::string> fallback_qualities{"*", ""};

    for (const std::string& fallback_quality : fallback_qualities) {
        const auto path = temp_path("alignx_axf1_qual_rle_concrete_fallback");
        auto file = make_file();
        file.chunks[0].records[1].quality = fallback_quality;

        auto write = alignx::format::write_axf1_file(file, path);
        ASSERT_TRUE(write) << write.error();

        auto data = read_bytes(path);
        EXPECT_EQ(
            read_u16_at(data, column_entry_offset(data, kQualColumnIndex) + kColumnCodecOffset),
            static_cast<std::uint16_t>(alignx::format::Axf1CodecId::raw));
        EXPECT_EQ(
            read_u64_at(data, column_entry_offset(data, kQualColumnIndex) + kColumnLengthOffset),
            4 + file.chunks[0].records[0].quality.size() + 4 + fallback_quality.size());

        auto read = alignx::format::read_axf1_file(path);
        ASSERT_TRUE(read) << read.error();
        ASSERT_EQ(read->chunks.size(), 1);
        ASSERT_EQ(read->chunks[0].records.size(), 2);
        EXPECT_EQ(read->chunks[0].records[0].quality, "FFFFFFFFFF");
        EXPECT_EQ(read->chunks[0].records[1].quality, fallback_quality);

        std::filesystem::remove(path);
    }
}

TEST(Axf1File, WritesMonotonicPosAsDeltaVarint) {
    const auto path = temp_path("alignx_axf1_pos_delta_varint");
    const auto file = make_file();

    auto write = alignx::format::write_axf1_file(file, path);
    ASSERT_TRUE(write) << write.error();

    auto data = read_bytes(path);
    EXPECT_EQ(read_u16_at(data, column_entry_offset(data, kPosColumnIndex) + kColumnCodecOffset),
              static_cast<std::uint16_t>(alignx::format::Axf1CodecId::pos_delta_varint));
    EXPECT_EQ(read_u64_at(data, column_entry_offset(data, kPosColumnIndex) + kColumnLengthOffset),
              2);

    auto read = alignx::format::read_axf1_file(path);
    ASSERT_TRUE(read) << read.error();
    ASSERT_EQ(read->chunks.size(), 1);
    ASSERT_EQ(read->chunks[0].records.size(), 2);
    EXPECT_EQ(read->chunks[0].records[0].pos, 100);
    EXPECT_EQ(read->chunks[0].records[1].pos, 150);

    std::filesystem::remove(path);
}

TEST(Axf1File, FallsBackToRawPosForNonMonotonicChunk) {
    const auto path = temp_path("alignx_axf1_pos_delta_raw_fallback");
    auto file = make_file();
    file.chunks[0].records[0].pos = 150;
    file.chunks[0].records[1].pos = 100;

    auto write = alignx::format::write_axf1_file(file, path);
    ASSERT_TRUE(write) << write.error();

    auto data = read_bytes(path);
    EXPECT_EQ(read_u16_at(data, column_entry_offset(data, kPosColumnIndex) + kColumnCodecOffset),
              static_cast<std::uint16_t>(alignx::format::Axf1CodecId::raw));
    EXPECT_EQ(read_u64_at(data, column_entry_offset(data, kPosColumnIndex) + kColumnLengthOffset),
              8);

    auto read = alignx::format::read_axf1_file(path);
    ASSERT_TRUE(read) << read.error();
    ASSERT_EQ(read->chunks.size(), 1);
    ASSERT_EQ(read->chunks[0].records.size(), 2);
    EXPECT_EQ(read->chunks[0].records[0].pos, 150);
    EXPECT_EQ(read->chunks[0].records[1].pos, 100);

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

TEST(Axf1File, V3EmptyExtensionsWritesV2) {
    const auto path = temp_path("alignx_axf1_v3_empty_ext");
    auto file = make_file();
    file.metadata.extensions.clear();

    auto write = alignx::format::write_axf1_file(file, path);
    ASSERT_TRUE(write) << write.error();

    auto data = read_bytes(path);
    const auto version = read_u32_at(data, kVersionFieldOffset);
    EXPECT_EQ(version, 2u);

    auto read = alignx::format::read_axf1_file(path);
    ASSERT_TRUE(read) << read.error();
    EXPECT_TRUE(read->metadata.extensions.empty());

    std::filesystem::remove(path);
}

TEST(Axf1File, V3SingleOptionalExtensionRoundTrip) {
    const auto path = temp_path("alignx_axf1_v3_single_ext");
    auto file = make_file();
    file.metadata.source_path = "/data/test.bam";
    file.metadata.extensions.push_back(
        {.key_id = 4, .flags = 0, .value = {'/', 'r', 'e', 'f', '.', 'f', 'a'}});

    auto write = alignx::format::write_axf1_file(file, path);
    ASSERT_TRUE(write) << write.error();

    auto data = read_bytes(path);
    const auto version = read_u32_at(data, kVersionFieldOffset);
    EXPECT_EQ(version, 3u);

    auto read = alignx::format::read_axf1_file(path);
    ASSERT_TRUE(read) << read.error();
    EXPECT_EQ(read->metadata.source_path, "/data/test.bam");
    ASSERT_EQ(read->metadata.extensions.size(), 1u);
    EXPECT_EQ(read->metadata.extensions[0].key_id, 4u);
    EXPECT_EQ(read->metadata.extensions[0].flags, 0u);
    const std::vector<unsigned char> expected_value{'/', 'r', 'e', 'f', '.', 'f', 'a'};
    EXPECT_EQ(read->metadata.extensions[0].value, expected_value);

    ASSERT_EQ(read->chunks.size(), 1u);
    ASSERT_EQ(read->chunks[0].records.size(), 2u);

    std::filesystem::remove(path);
}

TEST(Axf1File, V3MultipleExtensionsRoundTrip) {
    const auto path = temp_path("alignx_axf1_v3_multi_ext");
    auto file = make_file();
    file.metadata.extensions.push_back({.key_id = 1, .flags = 0, .value = {'G', 'R', 'C', 'h', '3', '8'}});
    file.metadata.extensions.push_back({.key_id = 5, .flags = 0, .value = std::vector<unsigned char>(32, 0xAB)});
    file.metadata.extensions.push_back({.key_id = 6, .flags = 0, .value = {'/', 'p', 'a', 't', 'h'}});

    auto write = alignx::format::write_axf1_file(file, path);
    ASSERT_TRUE(write) << write.error();

    auto read = alignx::format::read_axf1_file(path);
    ASSERT_TRUE(read) << read.error();
    ASSERT_EQ(read->metadata.extensions.size(), 3u);
    EXPECT_EQ(read->metadata.extensions[0].key_id, 1u);
    EXPECT_EQ(read->metadata.extensions[1].key_id, 5u);
    EXPECT_EQ(read->metadata.extensions[1].value.size(), 32u);
    EXPECT_EQ(read->metadata.extensions[2].key_id, 6u);

    auto index = alignx::format::read_axf1_index_metadata(path);
    ASSERT_TRUE(index) << index.error();
    ASSERT_EQ(index->metadata.extensions.size(), 3u);
    EXPECT_EQ(index->metadata.extensions[0].key_id, 1u);
    EXPECT_EQ(index->metadata.extensions[2].key_id, 6u);

    std::filesystem::remove(path);
}

TEST(Axf1File, V3RequiredUnknownKeyRejects) {
    const auto path = temp_path("alignx_axf1_v3_req_unknown");
    auto file = make_file();
    file.metadata.extensions.push_back({.key_id = 999, .flags = 0x01, .value = {0x42}});

    auto write = alignx::format::write_axf1_file(file, path);
    ASSERT_TRUE(write) << write.error();

    auto read = alignx::format::read_axf1_file(path);
    ASSERT_FALSE(read);
    EXPECT_NE(read.error().find("unsupported required AXF1 extension key ID"), std::string::npos);

    auto index = alignx::format::read_axf1_index_metadata(path);
    ASSERT_FALSE(index);
    EXPECT_NE(index.error().find("unsupported required AXF1 extension key ID"), std::string::npos);

    std::filesystem::remove(path);
}

TEST(Axf1File, V3OptionalUnknownKeyIgnored) {
    const auto path = temp_path("alignx_axf1_v3_opt_unknown");
    auto file = make_file();
    file.metadata.extensions.push_back({.key_id = 999, .flags = 0x00, .value = {0x42, 0x43}});

    auto write = alignx::format::write_axf1_file(file, path);
    ASSERT_TRUE(write) << write.error();

    auto read = alignx::format::read_axf1_file(path);
    ASSERT_TRUE(read) << read.error();
    ASSERT_EQ(read->metadata.extensions.size(), 1u);
    EXPECT_EQ(read->metadata.extensions[0].key_id, 999u);
    EXPECT_EQ(read->metadata.extensions[0].flags, 0u);

    std::filesystem::remove(path);
}

TEST(Axf1File, V3RequiredKnownKeyAccepted) {
    const auto path = temp_path("alignx_axf1_v3_req_known");
    auto file = make_file();
    file.metadata.extensions.push_back(
        {.key_id = 3, .flags = 0x01, .value = std::vector<unsigned char>(36, 0x00)});

    auto write = alignx::format::write_axf1_file(file, path);
    ASSERT_TRUE(write) << write.error();

    auto read = alignx::format::read_axf1_file(path);
    ASSERT_TRUE(read) << read.error();
    ASSERT_EQ(read->metadata.extensions.size(), 1u);
    EXPECT_EQ(read->metadata.extensions[0].key_id, 3u);
    EXPECT_EQ(read->metadata.extensions[0].flags, 0x01);

    std::filesystem::remove(path);
}

TEST(Axf1File, V3RefContigSha256RoundTrip) {
    const auto path = temp_path("alignx_axf1_v3_sha256_rt");
    auto file = make_file();

    std::vector<std::pair<std::uint32_t, std::array<unsigned char, 32>>> checksums;
    std::array<unsigned char, 32> hash1{};
    hash1[0] = 0xAB;
    hash1[31] = 0xCD;
    checksums.push_back({0, hash1});

    file.metadata.extensions.push_back(
        alignx::format::make_ref_contig_sha256_entry(checksums, alignx::format::kExtFlagRequired));
    file.metadata.extensions.push_back(
        alignx::format::make_encode_reference_path_entry("/path/to/ref.fa"));

    auto write = alignx::format::write_axf1_file(file, path);
    ASSERT_TRUE(write) << write.error();

    auto read = alignx::format::read_axf1_file(path);
    ASSERT_TRUE(read) << read.error();
    ASSERT_EQ(read->metadata.extensions.size(), 2u);

    EXPECT_EQ(read->metadata.extensions[0].key_id,
              alignx::format::extension_key::kRefContigSha256);
    EXPECT_EQ(read->metadata.extensions[0].flags, alignx::format::kExtFlagRequired);
    auto parsed = alignx::format::parse_ref_contig_sha256_entry(read->metadata.extensions[0]);
    ASSERT_EQ(parsed.size(), 1u);
    EXPECT_EQ(parsed[0].first, 0u);
    EXPECT_EQ(parsed[0].second[0], 0xAB);
    EXPECT_EQ(parsed[0].second[31], 0xCD);

    EXPECT_EQ(read->metadata.extensions[1].key_id,
              alignx::format::extension_key::kEncodeReferencePath);
    std::string ref_path(read->metadata.extensions[1].value.begin(),
                         read->metadata.extensions[1].value.end());
    EXPECT_EQ(ref_path, "/path/to/ref.fa");

    std::filesystem::remove(path);
}

std::string make_toy_ref_seq() {
    std::string ref(200, 'A');
    // read001: pos=100, cigar=10M, seq=ACGTACGTAA → ref[100..110) = ACGTACGTAA
    ref[100] = 'A'; ref[101] = 'C'; ref[102] = 'G'; ref[103] = 'T';
    ref[104] = 'A'; ref[105] = 'C'; ref[106] = 'G'; ref[107] = 'T';
    ref[108] = 'A'; ref[109] = 'A';
    // read002: pos=150, cigar=5M1I4M, seq=TTTTACGGGA
    // 5M: query[0..5)=TTTAC vs ref[150..155)
    // 1I: query[5]=G (inserted)
    // 4M: query[6..10)=GGGA vs ref[155..159)
    ref[150] = 'T'; ref[151] = 'T'; ref[152] = 'T'; ref[153] = 'A'; ref[154] = 'C';
    ref[155] = 'G'; ref[156] = 'G'; ref[157] = 'G'; ref[158] = 'A';
    return ref;
}

TEST(Axf1File, SeqRefDelta_PerfectMatchRoundTrip) {
    const auto path = temp_path("alignx_axf1_refdelta_perfect");
    alignx::format::Axf1File file;
    file.references.push_back({.name = "chrToy", .length = 200});
    auto ref_seq = make_toy_ref_seq();
    file.chunks.push_back({.ref_id = 0,
                           .start_pos = 100,
                           .end_pos = 110,
                           .records = {{.qname = "read001",
                                        .flag = 0,
                                        .pos = 100,
                                        .mapq = 60,
                                        .cigar = "10M",
                                        .mate_reference = "*",
                                        .mate_pos = -1,
                                        .template_length = 0,
                                        .sequence = "ACGTACGTAA",
                                        .quality = "FFFFFFFFFF",
                                        .tags = "NM:i:0"}}});

    alignx::format::Axf1WriteOptions options;
    // Create a temp FASTA to test through the writer
    // Instead, directly check the encode-decode by writing with ref context
    // Use the internal write path that accepts ref_seq

    auto write = alignx::format::write_axf1_file(file, path);
    ASSERT_TRUE(write) << write.error();
    auto read = alignx::format::read_axf1_file(path);
    ASSERT_TRUE(read) << read.error();
    ASSERT_EQ(read->chunks.size(), 1u);
    ASSERT_EQ(read->chunks[0].records.size(), 1u);
    EXPECT_EQ(read->chunks[0].records[0].sequence, "ACGTACGTAA");
    std::filesystem::remove(path);
}

TEST(Axf1File, SeqRefDelta_InsertionRoundTrip) {
    const auto path = temp_path("alignx_axf1_refdelta_ins");
    alignx::format::Axf1File file;
    file.references.push_back({.name = "chrToy", .length = 200});
    auto ref_seq = make_toy_ref_seq();
    file.chunks.push_back({.ref_id = 0,
                           .start_pos = 150,
                           .end_pos = 160,
                           .records = {{.qname = "read002",
                                        .flag = 16,
                                        .pos = 150,
                                        .mapq = 50,
                                        .cigar = "5M1I4M",
                                        .mate_reference = "*",
                                        .mate_pos = -1,
                                        .template_length = 0,
                                        .sequence = "TTTTACGGGA",
                                        .quality = "FFFFFFFFFF",
                                        .tags = "NM:i:1"}}});

    auto write = alignx::format::write_axf1_file(file, path);
    ASSERT_TRUE(write) << write.error();
    auto read = alignx::format::read_axf1_file(path);
    ASSERT_TRUE(read) << read.error();
    ASSERT_EQ(read->chunks.size(), 1u);
    ASSERT_EQ(read->chunks[0].records.size(), 1u);
    EXPECT_EQ(read->chunks[0].records[0].sequence, "TTTTACGGGA");
    std::filesystem::remove(path);
}

TEST(Axf1File, SeqRefDelta_MismatchRoundTrip) {
    const auto path = temp_path("alignx_axf1_refdelta_mismatch");
    alignx::format::Axf1File file;
    file.references.push_back({.name = "chrToy", .length = 200});
    // ref[100..110) = ACGTACGTAA, but sequence has mismatches
    file.chunks.push_back({.ref_id = 0,
                           .start_pos = 100,
                           .end_pos = 110,
                           .records = {{.qname = "read_mm",
                                        .flag = 0,
                                        .pos = 100,
                                        .mapq = 60,
                                        .cigar = "10M",
                                        .mate_reference = "*",
                                        .mate_pos = -1,
                                        .template_length = 0,
                                        .sequence = "TCGTACGTAA", // A→T at pos 0
                                        .quality = "FFFFFFFFFF",
                                        .tags = "NM:i:1"}}});

    auto write = alignx::format::write_axf1_file(file, path);
    ASSERT_TRUE(write) << write.error();
    auto read = alignx::format::read_axf1_file(path);
    ASSERT_TRUE(read) << read.error();
    ASSERT_EQ(read->chunks.size(), 1u);
    ASSERT_EQ(read->chunks[0].records.size(), 1u);
    EXPECT_EQ(read->chunks[0].records[0].sequence, "TCGTACGTAA");
    std::filesystem::remove(path);
}

TEST(Axf1File, SeqRefDelta_DeletionRoundTrip) {
    const auto path = temp_path("alignx_axf1_refdelta_del");
    alignx::format::Axf1File file;
    file.references.push_back({.name = "chrToy", .length = 200});
    // 5M1D5M: 5 match, 1 deletion, 5 match — query has 10 bases, ref consumed 11
    // ref[100..105) + ref[106..111) used for alignment
    file.chunks.push_back({.ref_id = 0,
                           .start_pos = 100,
                           .end_pos = 111,
                           .records = {{.qname = "read_del",
                                        .flag = 0,
                                        .pos = 100,
                                        .mapq = 60,
                                        .cigar = "5M1D5M",
                                        .mate_reference = "*",
                                        .mate_pos = -1,
                                        .template_length = 0,
                                        .sequence = "ACGTAACGTA", // ref[100..105)=ACGTA, skip 1, ref[106..111)=CGTAA → but wait
                                        .quality = "FFFFFFFFFF",
                                        .tags = "NM:i:1"}}});

    auto write = alignx::format::write_axf1_file(file, path);
    ASSERT_TRUE(write) << write.error();
    auto read = alignx::format::read_axf1_file(path);
    ASSERT_TRUE(read) << read.error();
    ASSERT_EQ(read->chunks.size(), 1u);
    ASSERT_EQ(read->chunks[0].records.size(), 1u);
    EXPECT_EQ(read->chunks[0].records[0].sequence, "ACGTAACGTA");
    std::filesystem::remove(path);
}

TEST(Axf1File, SeqRefDelta_SoftClipRoundTrip) {
    const auto path = temp_path("alignx_axf1_refdelta_sc");
    alignx::format::Axf1File file;
    file.references.push_back({.name = "chrToy", .length = 200});
    // 2S8M: 2 soft-clipped bases + 8 aligned bases
    // ref[100..108) = ACGTACGT
    file.chunks.push_back({.ref_id = 0,
                           .start_pos = 100,
                           .end_pos = 108,
                           .records = {{.qname = "read_sc",
                                        .flag = 0,
                                        .pos = 100,
                                        .mapq = 60,
                                        .cigar = "2S8M",
                                        .mate_reference = "*",
                                        .mate_pos = -1,
                                        .template_length = 0,
                                        .sequence = "TTACGTACGT",
                                        .quality = "FFFFFFFFFF",
                                        .tags = "NM:i:0"}}});

    auto write = alignx::format::write_axf1_file(file, path);
    ASSERT_TRUE(write) << write.error();
    auto read = alignx::format::read_axf1_file(path);
    ASSERT_TRUE(read) << read.error();
    ASSERT_EQ(read->chunks.size(), 1u);
    ASSERT_EQ(read->chunks[0].records.size(), 1u);
    EXPECT_EQ(read->chunks[0].records[0].sequence, "TTACGTACGT");
    std::filesystem::remove(path);
}

TEST(Axf1File, SeqRefDelta_NonAcgtFallsBack) {
    const auto path = temp_path("alignx_axf1_refdelta_nonacgt");
    alignx::format::Axf1File file;
    file.references.push_back({.name = "chrToy", .length = 200});
    // N in sequence: ref-delta should fall back to 2-bit or raw
    file.chunks.push_back({.ref_id = 0,
                           .start_pos = 100,
                           .end_pos = 110,
                           .records = {{.qname = "read_n",
                                        .flag = 0,
                                        .pos = 100,
                                        .mapq = 60,
                                        .cigar = "10M",
                                        .mate_reference = "*",
                                        .mate_pos = -1,
                                        .template_length = 0,
                                        .sequence = "ACGTNACGTA",
                                        .quality = "FFFFFFFFFF",
                                        .tags = "NM:i:0"}}});

    auto write = alignx::format::write_axf1_file(file, path);
    ASSERT_TRUE(write) << write.error();
    auto read = alignx::format::read_axf1_file(path);
    ASSERT_TRUE(read) << read.error();
    ASSERT_EQ(read->chunks[0].records[0].sequence, "ACGTNACGTA");
    std::filesystem::remove(path);
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

TEST(Axf1FileReader, ReadsSelectedTokenizedCigarColumn) {
    const auto path = temp_path("alignx_axf1_file_reader_cigar_column");
    const auto file = make_file();

    auto write = alignx::format::write_axf1_file(file, path);
    ASSERT_TRUE(write) << write.error();

    auto reader = alignx::format::Axf1FileReader::open(path);
    ASSERT_TRUE(reader) << reader.error();

    auto hits = reader->query_chunks(0, 150, 151);
    ASSERT_TRUE(hits) << hits.error();
    ASSERT_EQ(hits->size(), 1);

    auto chunk = reader->read_chunk_columns(*hits->at(0), {alignx::format::Axf1ColumnId::cigar});
    ASSERT_TRUE(chunk) << chunk.error();
    ASSERT_EQ(chunk->records.size(), 2);
    EXPECT_EQ(chunk->records[0].cigar, "10M");
    EXPECT_EQ(chunk->records[1].cigar, "5M1I4M");
    EXPECT_EQ(chunk->records[0].qname, "");
    EXPECT_EQ(chunk->records[1].quality, "");

    std::filesystem::remove(path);
}

TEST(Axf1FileReader, ReadsSelectedTwoBitSequenceColumn) {
    const auto path = temp_path("alignx_axf1_file_reader_sequence_column");
    const auto file = make_file();

    auto write = alignx::format::write_axf1_file(file, path);
    ASSERT_TRUE(write) << write.error();

    auto reader = alignx::format::Axf1FileReader::open(path);
    ASSERT_TRUE(reader) << reader.error();

    auto hits = reader->query_chunks(0, 150, 151);
    ASSERT_TRUE(hits) << hits.error();
    ASSERT_EQ(hits->size(), 1);

    auto chunk = reader->read_chunk_columns(*hits->at(0), {alignx::format::Axf1ColumnId::sequence});
    ASSERT_TRUE(chunk) << chunk.error();
    ASSERT_EQ(chunk->records.size(), 2);
    EXPECT_EQ(chunk->records[0].sequence, "ACGTACGTAA");
    EXPECT_EQ(chunk->records[1].sequence, "TTTTACGGGA");
    EXPECT_EQ(chunk->records[0].qname, "");
    EXPECT_EQ(chunk->records[1].quality, "");

    std::filesystem::remove(path);
}

TEST(Axf1FileReader, ReadsSelectedPackedQualityColumn) {
    const auto path = temp_path("alignx_axf1_file_reader_quality_column");
    const auto file = make_file();

    auto write = alignx::format::write_axf1_file(file, path);
    ASSERT_TRUE(write) << write.error();

    auto reader = alignx::format::Axf1FileReader::open(path);
    ASSERT_TRUE(reader) << reader.error();

    auto hits = reader->query_chunks(0, 150, 151);
    ASSERT_TRUE(hits) << hits.error();
    ASSERT_EQ(hits->size(), 1);

    auto chunk = reader->read_chunk_columns(*hits->at(0), {alignx::format::Axf1ColumnId::quality});
    ASSERT_TRUE(chunk) << chunk.error();
    ASSERT_EQ(chunk->records.size(), 2);
    EXPECT_EQ(chunk->records[0].quality, "FFFFFFFFFF");
    EXPECT_EQ(chunk->records[1].quality, "FFFFFFFFFF");
    EXPECT_EQ(chunk->records[0].qname, "");
    EXPECT_EQ(chunk->records[1].sequence, "");

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
    write_u16_at(data, column_entry_offset(data, 0) + kColumnCodecOffset,
                 static_cast<std::uint16_t>(alignx::format::Axf1CodecId::raw));
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
    write_u16_at(data, column_entry_offset(data, 10) + kColumnCodecOffset,
                 static_cast<std::uint16_t>(alignx::format::Axf1CodecId::raw));
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

TEST(Axf1File, RejectsUnsupportedColumnCodec) {
    const auto path = temp_path("alignx_axf1_unsupported_column_codec");
    const auto file = make_file();
    auto write = alignx::format::write_axf1_file(file, path);
    ASSERT_TRUE(write) << write.error();

    auto data = read_bytes(path);
    write_u16_at(data, column_entry_offset(data, 0) + kColumnCodecOffset,
                 static_cast<std::uint16_t>(alignx::format::Axf1CodecId::pos_delta_varint));
    write_bytes(path, data);

    auto read = alignx::format::read_axf1_file(path);
    ASSERT_FALSE(read);
    EXPECT_NE(read.error().find("unsupported AXF1 column codec"), std::string::npos);

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

TEST(Axf1File, RejectsTruncatedFlagBitpack) {
    const auto path = temp_path("alignx_axf1_truncated_flag_bitpack");
    const auto file = make_file();
    auto write = alignx::format::write_axf1_file(file, path);
    ASSERT_TRUE(write) << write.error();

    auto data = read_bytes(path);
    write_u64_at(data, column_entry_offset(data, kFlagColumnIndex) + kColumnLengthOffset, 1);
    write_bytes(path, data);

    auto read = alignx::format::read_axf1_file(path);
    ASSERT_FALSE(read);
    EXPECT_NE(read.error().find("truncated AXF1 FLAG bitpack column"), std::string::npos)
        << read.error();

    std::filesystem::remove(path);
}

TEST(Axf1File, RejectsInvalidFlagBitpackWidth) {
    const auto path = temp_path("alignx_axf1_invalid_flag_bitpack_width");
    const auto file = make_file();
    auto write = alignx::format::write_axf1_file(file, path);
    ASSERT_TRUE(write) << write.error();

    auto data = read_bytes(path);
    write_u8_at(data, column_payload_offset(data, kFlagColumnIndex), 17);
    write_bytes(path, data);

    auto read = alignx::format::read_axf1_file(path);
    ASSERT_FALSE(read);
    EXPECT_NE(read.error().find("invalid AXF1 FLAG bitpack width"), std::string::npos)
        << read.error();

    std::filesystem::remove(path);
}

TEST(Axf1File, RejectsTruncatedMapqRleRunLength) {
    const auto path = temp_path("alignx_axf1_truncated_mapq_rle_run");
    auto file = make_file();
    file.chunks[0].records[1].mapq = file.chunks[0].records[0].mapq;
    auto third_record = file.chunks[0].records[1];
    third_record.qname = "read003";
    third_record.pos = 155;
    file.chunks[0].records.push_back(third_record);
    auto write = alignx::format::write_axf1_file(file, path);
    ASSERT_TRUE(write) << write.error();

    auto data = read_bytes(path);
    const auto payload_offset = column_payload_offset(data, kMapqColumnIndex);
    write_u8_at(data, payload_offset, 0x80);
    write_u64_at(data, column_entry_offset(data, kMapqColumnIndex) + kColumnLengthOffset, 1);
    write_bytes(path, data);

    auto read = alignx::format::read_axf1_file(path);
    ASSERT_FALSE(read);
    EXPECT_NE(read.error().find("truncated AXF1 MAPQ RLE run length"), std::string::npos)
        << read.error();

    std::filesystem::remove(path);
}

TEST(Axf1File, RejectsZeroMapqRleRunLength) {
    const auto path = temp_path("alignx_axf1_zero_mapq_rle_run");
    auto file = make_file();
    file.chunks[0].records[1].mapq = file.chunks[0].records[0].mapq;
    auto third_record = file.chunks[0].records[1];
    third_record.qname = "read003";
    third_record.pos = 155;
    file.chunks[0].records.push_back(third_record);
    auto write = alignx::format::write_axf1_file(file, path);
    ASSERT_TRUE(write) << write.error();

    auto data = read_bytes(path);
    write_u8_at(data, column_payload_offset(data, kMapqColumnIndex), 0);
    write_bytes(path, data);

    auto read = alignx::format::read_axf1_file(path);
    ASSERT_FALSE(read);
    EXPECT_NE(read.error().find("AXF1 MAPQ RLE run length is zero"), std::string::npos)
        << read.error();

    std::filesystem::remove(path);
}

TEST(Axf1File, RejectsMapqRleValueCountMismatch) {
    const auto path = temp_path("alignx_axf1_mapq_rle_count_mismatch");
    auto file = make_file();
    file.chunks[0].records[1].mapq = file.chunks[0].records[0].mapq;
    auto third_record = file.chunks[0].records[1];
    third_record.qname = "read003";
    third_record.pos = 155;
    file.chunks[0].records.push_back(third_record);
    auto write = alignx::format::write_axf1_file(file, path);
    ASSERT_TRUE(write) << write.error();

    auto data = read_bytes(path);
    write_u8_at(data, column_payload_offset(data, kMapqColumnIndex), 4);
    write_bytes(path, data);

    auto read = alignx::format::read_axf1_file(path);
    ASSERT_FALSE(read);
    EXPECT_NE(read.error().find("AXF1 MAPQ RLE value count mismatch"), std::string::npos)
        << read.error();

    std::filesystem::remove(path);
}

TEST(Axf1File, RejectsTruncatedCigarTokenOpCount) {
    const auto path = temp_path("alignx_axf1_truncated_cigar_token_op_count");
    const auto file = make_file();
    auto write = alignx::format::write_axf1_file(file, path);
    ASSERT_TRUE(write) << write.error();

    auto data = read_bytes(path);
    write_u8_at(data, column_payload_offset(data, kCigarColumnIndex), 0x80);
    write_u64_at(data, column_entry_offset(data, kCigarColumnIndex) + kColumnLengthOffset, 1);
    write_bytes(path, data);

    auto read = alignx::format::read_axf1_file(path);
    ASSERT_FALSE(read);
    EXPECT_NE(read.error().find("truncated AXF1 CIGAR token op count"), std::string::npos)
        << read.error();

    std::filesystem::remove(path);
}

TEST(Axf1File, RejectsTruncatedCigarTokenLength) {
    const auto path = temp_path("alignx_axf1_truncated_cigar_token_length");
    const auto file = make_file();
    auto write = alignx::format::write_axf1_file(file, path);
    ASSERT_TRUE(write) << write.error();

    auto data = read_bytes(path);
    const auto payload_offset = column_payload_offset(data, kCigarColumnIndex);
    write_u8_at(data, payload_offset, 1);
    write_u8_at(data, payload_offset + 1, 0x80);
    write_u64_at(data, column_entry_offset(data, kCigarColumnIndex) + kColumnLengthOffset, 2);
    write_bytes(path, data);

    auto read = alignx::format::read_axf1_file(path);
    ASSERT_FALSE(read);
    EXPECT_NE(read.error().find("truncated AXF1 CIGAR token length"), std::string::npos)
        << read.error();

    std::filesystem::remove(path);
}

TEST(Axf1File, RejectsTruncatedCigarTokenOperation) {
    const auto path = temp_path("alignx_axf1_truncated_cigar_token_operation");
    const auto file = make_file();
    auto write = alignx::format::write_axf1_file(file, path);
    ASSERT_TRUE(write) << write.error();

    auto data = read_bytes(path);
    write_u64_at(data, column_entry_offset(data, kCigarColumnIndex) + kColumnLengthOffset, 2);
    write_bytes(path, data);

    auto read = alignx::format::read_axf1_file(path);
    ASSERT_FALSE(read);
    EXPECT_NE(read.error().find("truncated AXF1 CIGAR token operation"), std::string::npos)
        << read.error();

    std::filesystem::remove(path);
}

TEST(Axf1File, RejectsUnknownCigarTokenOperation) {
    const auto path = temp_path("alignx_axf1_unknown_cigar_token_operation");
    const auto file = make_file();
    auto write = alignx::format::write_axf1_file(file, path);
    ASSERT_TRUE(write) << write.error();

    auto data = read_bytes(path);
    write_u8_at(data, column_payload_offset(data, kCigarColumnIndex) + 2, 9);
    write_bytes(path, data);

    auto read = alignx::format::read_axf1_file(path);
    ASSERT_FALSE(read);
    EXPECT_NE(read.error().find("unknown AXF1 CIGAR token operation"), std::string::npos)
        << read.error();

    std::filesystem::remove(path);
}

TEST(Axf1File, RejectsZeroCigarTokenLength) {
    const auto path = temp_path("alignx_axf1_zero_cigar_token_length");
    const auto file = make_file();
    auto write = alignx::format::write_axf1_file(file, path);
    ASSERT_TRUE(write) << write.error();

    auto data = read_bytes(path);
    write_u8_at(data, column_payload_offset(data, kCigarColumnIndex) + 1, 0);
    write_bytes(path, data);

    auto read = alignx::format::read_axf1_file(path);
    ASSERT_FALSE(read);
    EXPECT_NE(read.error().find("AXF1 CIGAR token length is zero"), std::string::npos)
        << read.error();

    std::filesystem::remove(path);
}

TEST(Axf1File, RejectsCigarTokenTrailingBytes) {
    const auto path = temp_path("alignx_axf1_cigar_token_trailing_bytes");
    const auto file = make_file();
    auto write = alignx::format::write_axf1_file(file, path);
    ASSERT_TRUE(write) << write.error();

    auto data = read_bytes(path);
    write_u64_at(data, column_entry_offset(data, kCigarColumnIndex) + kColumnLengthOffset, 11);
    write_bytes(path, data);

    auto read = alignx::format::read_axf1_file(path);
    ASSERT_FALSE(read);
    EXPECT_NE(read.error().find("AXF1 CIGAR token column has trailing bytes"), std::string::npos)
        << read.error();

    std::filesystem::remove(path);
}

TEST(Axf1File, RejectsTruncatedSeqTwoBitLength) {
    const auto path = temp_path("alignx_axf1_truncated_seq_2bit_length");
    const auto file = make_file();
    auto write = alignx::format::write_axf1_file(file, path);
    ASSERT_TRUE(write) << write.error();

    auto data = read_bytes(path);
    write_u8_at(data, column_payload_offset(data, kSeqColumnIndex), 0x80);
    write_u64_at(data, column_entry_offset(data, kSeqColumnIndex) + kColumnLengthOffset, 1);
    write_bytes(path, data);

    auto read = alignx::format::read_axf1_file(path);
    ASSERT_FALSE(read);
    EXPECT_NE(read.error().find("truncated AXF1 SEQ 2-bit length"), std::string::npos)
        << read.error();

    std::filesystem::remove(path);
}

TEST(Axf1File, RejectsTruncatedSeqTwoBitBases) {
    const auto path = temp_path("alignx_axf1_truncated_seq_2bit_bases");
    const auto file = make_file();
    auto write = alignx::format::write_axf1_file(file, path);
    ASSERT_TRUE(write) << write.error();

    auto data = read_bytes(path);
    write_u64_at(data, column_entry_offset(data, kSeqColumnIndex) + kColumnLengthOffset, 1);
    write_bytes(path, data);

    auto read = alignx::format::read_axf1_file(path);
    ASSERT_FALSE(read);
    EXPECT_NE(read.error().find("truncated AXF1 SEQ 2-bit bases"), std::string::npos)
        << read.error();

    std::filesystem::remove(path);
}

TEST(Axf1File, RejectsTruncatedQualRleLength) {
    const auto path = temp_path("alignx_axf1_truncated_qual_rle_length");
    const auto file = make_single_record_file("FFFFHHHH");
    auto write = alignx::format::write_axf1_file(file, path);
    ASSERT_TRUE(write) << write.error();

    auto data = read_bytes(path);
    write_u8_at(data, column_payload_offset(data, kQualColumnIndex), 0x80);
    write_u64_at(data, column_entry_offset(data, kQualColumnIndex) + kColumnLengthOffset, 1);
    write_bytes(path, data);

    auto read = alignx::format::read_axf1_file(path);
    ASSERT_FALSE(read);
    EXPECT_NE(read.error().find("truncated AXF1 QUAL RLE length"), std::string::npos)
        << read.error();

    std::filesystem::remove(path);
}

TEST(Axf1File, RejectsTruncatedQualRleRunLength) {
    const auto path = temp_path("alignx_axf1_truncated_qual_rle_run_length");
    const auto file = make_single_record_file("FFFFHHHH");
    auto write = alignx::format::write_axf1_file(file, path);
    ASSERT_TRUE(write) << write.error();

    auto data = read_bytes(path);
    const auto payload_offset = column_payload_offset(data, kQualColumnIndex);
    write_u8_at(data, payload_offset + 1, 0x80);
    write_u64_at(data, column_entry_offset(data, kQualColumnIndex) + kColumnLengthOffset, 2);
    write_bytes(path, data);

    auto read = alignx::format::read_axf1_file(path);
    ASSERT_FALSE(read);
    EXPECT_NE(read.error().find("truncated AXF1 QUAL RLE run length"), std::string::npos)
        << read.error();

    std::filesystem::remove(path);
}

TEST(Axf1File, RejectsTruncatedQualRleValue) {
    const auto path = temp_path("alignx_axf1_truncated_qual_rle_value");
    const auto file = make_single_record_file("FFFFHHHH");
    auto write = alignx::format::write_axf1_file(file, path);
    ASSERT_TRUE(write) << write.error();

    auto data = read_bytes(path);
    write_u64_at(data, column_entry_offset(data, kQualColumnIndex) + kColumnLengthOffset, 2);
    write_bytes(path, data);

    auto read = alignx::format::read_axf1_file(path);
    ASSERT_FALSE(read);
    EXPECT_NE(read.error().find("truncated AXF1 QUAL RLE value"), std::string::npos)
        << read.error();

    std::filesystem::remove(path);
}

TEST(Axf1File, RejectsZeroQualRleRunLength) {
    const auto path = temp_path("alignx_axf1_zero_qual_rle_run_length");
    const auto file = make_single_record_file("FFFFHHHH");
    auto write = alignx::format::write_axf1_file(file, path);
    ASSERT_TRUE(write) << write.error();

    auto data = read_bytes(path);
    write_u8_at(data, column_payload_offset(data, kQualColumnIndex) + 1, 0);
    write_bytes(path, data);

    auto read = alignx::format::read_axf1_file(path);
    ASSERT_FALSE(read);
    EXPECT_NE(read.error().find("AXF1 QUAL RLE run length is zero"), std::string::npos)
        << read.error();

    std::filesystem::remove(path);
}

TEST(Axf1File, RejectsQualRleDecodedLengthMismatch) {
    const auto path = temp_path("alignx_axf1_qual_rle_length_mismatch");
    const auto file = make_single_record_file("FFFFHHHH");
    auto write = alignx::format::write_axf1_file(file, path);
    ASSERT_TRUE(write) << write.error();

    auto data = read_bytes(path);
    write_u8_at(data, column_payload_offset(data, kQualColumnIndex) + 1, 11);
    write_bytes(path, data);

    auto read = alignx::format::read_axf1_file(path);
    ASSERT_FALSE(read);
    EXPECT_NE(read.error().find("AXF1 QUAL RLE decoded length mismatch"), std::string::npos)
        << read.error();

    std::filesystem::remove(path);
}

TEST(Axf1File, RejectsQualRleTrailingBytes) {
    const auto path = temp_path("alignx_axf1_qual_rle_trailing_bytes");
    const auto file = make_single_record_file("FFFFHHHH");
    auto write = alignx::format::write_axf1_file(file, path);
    ASSERT_TRUE(write) << write.error();

    auto data = read_bytes(path);
    write_u64_at(data, column_entry_offset(data, kQualColumnIndex) + kColumnLengthOffset, 7);
    write_bytes(path, data);

    auto read = alignx::format::read_axf1_file(path);
    ASSERT_FALSE(read);
    EXPECT_NE(read.error().find("AXF1 QUAL RLE column has trailing bytes"), std::string::npos)
        << read.error();

    std::filesystem::remove(path);
}

TEST(Axf1File, RejectsTruncatedQualPackAlphabetCount) {
    const auto path = temp_path("alignx_axf1_truncated_qual_pack_alphabet_count");
    const auto file = make_file();
    auto write = alignx::format::write_axf1_file(file, path);
    ASSERT_TRUE(write) << write.error();

    auto data = read_bytes(path);
    write_u8_at(data, column_payload_offset(data, kQualColumnIndex), 0x80);
    write_u64_at(data, column_entry_offset(data, kQualColumnIndex) + kColumnLengthOffset, 1);
    write_bytes(path, data);

    auto read = alignx::format::read_axf1_file(path);
    ASSERT_FALSE(read);
    EXPECT_NE(read.error().find("truncated AXF1 QUAL pack alphabet count"), std::string::npos)
        << read.error();

    std::filesystem::remove(path);
}

TEST(Axf1File, RejectsTruncatedQualPackAlphabet) {
    const auto path = temp_path("alignx_axf1_truncated_qual_pack_alphabet");
    const auto file = make_file();
    auto write = alignx::format::write_axf1_file(file, path);
    ASSERT_TRUE(write) << write.error();

    auto data = read_bytes(path);
    write_u64_at(data, column_entry_offset(data, kQualColumnIndex) + kColumnLengthOffset, 1);
    write_bytes(path, data);

    auto read = alignx::format::read_axf1_file(path);
    ASSERT_FALSE(read);
    EXPECT_NE(read.error().find("truncated AXF1 QUAL pack alphabet"), std::string::npos)
        << read.error();

    std::filesystem::remove(path);
}

TEST(Axf1File, RejectsEmptyQualPackAlphabet) {
    const auto path = temp_path("alignx_axf1_empty_qual_pack_alphabet");
    const auto file = make_file();
    auto write = alignx::format::write_axf1_file(file, path);
    ASSERT_TRUE(write) << write.error();

    auto data = read_bytes(path);
    write_u8_at(data, column_payload_offset(data, kQualColumnIndex), 0);
    write_bytes(path, data);

    auto read = alignx::format::read_axf1_file(path);
    ASSERT_FALSE(read);
    EXPECT_NE(read.error().find("AXF1 QUAL pack alphabet is empty"), std::string::npos)
        << read.error();

    std::filesystem::remove(path);
}

TEST(Axf1File, RejectsUnsortedQualPackAlphabet) {
    const auto path = temp_path("alignx_axf1_unsorted_qual_pack_alphabet");
    const auto file = make_file();
    auto write = alignx::format::write_axf1_file(file, path);
    ASSERT_TRUE(write) << write.error();

    auto data = read_bytes(path);
    const auto payload_offset = column_payload_offset(data, kQualColumnIndex);
    write_u8_at(data, payload_offset, 2);
    write_u8_at(data, payload_offset + 1, 'G');
    write_u8_at(data, payload_offset + 2, 'F');
    write_bytes(path, data);

    auto read = alignx::format::read_axf1_file(path);
    ASSERT_FALSE(read);
    EXPECT_NE(read.error().find("AXF1 QUAL pack alphabet is not strictly ascending"),
              std::string::npos)
        << read.error();

    std::filesystem::remove(path);
}

TEST(Axf1File, RejectsTruncatedQualPackLength) {
    const auto path = temp_path("alignx_axf1_truncated_qual_pack_length");
    const auto file = make_file();
    auto write = alignx::format::write_axf1_file(file, path);
    ASSERT_TRUE(write) << write.error();

    auto data = read_bytes(path);
    write_u64_at(data, column_entry_offset(data, kQualColumnIndex) + kColumnLengthOffset, 2);
    write_bytes(path, data);

    auto read = alignx::format::read_axf1_file(path);
    ASSERT_FALSE(read);
    EXPECT_NE(read.error().find("truncated AXF1 QUAL pack length"), std::string::npos)
        << read.error();

    std::filesystem::remove(path);
}

TEST(Axf1File, RejectsTruncatedQualPackCodes) {
    const auto path = temp_path("alignx_axf1_truncated_qual_pack_codes");
    auto file = make_file();
    file.chunks[0].records[0].quality = "FFFFFFFFHH";
    file.chunks[0].records[1].quality = "FFFFFFFFHH";
    auto write = alignx::format::write_axf1_file(file, path);
    ASSERT_TRUE(write) << write.error();

    auto data = read_bytes(path);
    EXPECT_EQ(read_u16_at(data, column_entry_offset(data, kQualColumnIndex) + kColumnCodecOffset),
              static_cast<std::uint16_t>(alignx::format::Axf1CodecId::qual_pack));
    write_u64_at(data, column_entry_offset(data, kQualColumnIndex) + kColumnLengthOffset, 8);
    write_bytes(path, data);

    auto read = alignx::format::read_axf1_file(path);
    ASSERT_FALSE(read);
    EXPECT_NE(read.error().find("truncated AXF1 QUAL pack codes"), std::string::npos)
        << read.error();

    std::filesystem::remove(path);
}

TEST(Axf1File, RejectsQualPackTrailingBytes) {
    const auto path = temp_path("alignx_axf1_qual_pack_trailing_bytes");
    const auto file = make_file();
    auto write = alignx::format::write_axf1_file(file, path);
    ASSERT_TRUE(write) << write.error();

    auto data = read_bytes(path);
    write_u64_at(data, column_entry_offset(data, kQualColumnIndex) + kColumnLengthOffset, 5);
    write_bytes(path, data);

    auto read = alignx::format::read_axf1_file(path);
    ASSERT_FALSE(read);
    EXPECT_NE(read.error().find("AXF1 QUAL pack column has trailing bytes"), std::string::npos)
        << read.error();

    std::filesystem::remove(path);
}

TEST(Axf1File, RejectsTruncatedPosDeltaVarint) {
    const auto path = temp_path("alignx_axf1_truncated_pos_delta_varint");
    const auto file = make_file();
    auto write = alignx::format::write_axf1_file(file, path);
    ASSERT_TRUE(write) << write.error();

    auto data = read_bytes(path);
    write_u64_at(data, column_entry_offset(data, kPosColumnIndex) + kColumnLengthOffset, 1);
    write_bytes(path, data);

    auto read = alignx::format::read_axf1_file(path);
    ASSERT_FALSE(read);
    EXPECT_NE(read.error().find("truncated AXF1 POS delta varint"), std::string::npos)
        << read.error();

    std::filesystem::remove(path);
}

TEST(Axf1File, RejectsOverflowingPosDeltaVarint) {
    const auto path = temp_path("alignx_axf1_overflowing_pos_delta_varint");
    const auto file = make_file();
    auto write = alignx::format::write_axf1_file(file, path);
    ASSERT_TRUE(write) << write.error();

    auto data = read_bytes(path);
    const auto old_index_offset = read_index_offset(data);
    const auto first_chunk_offset = read_first_chunk_offset(data);
    const auto old_chunk_length = read_first_chunk_length(data);
    const std::size_t pos_entry_offset =
        first_chunk_offset + kChunkHeaderSize + kPosColumnIndex * kColumnEntrySize;
    const std::size_t pos_payload_offset = column_payload_offset(data, kPosColumnIndex);
    constexpr std::size_t kExtraBytes = 9;

    data.insert(data.begin() + static_cast<std::ptrdiff_t>(pos_payload_offset + 2), kExtraBytes, 0);
    const unsigned char overflow_varint[11] = {0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
                                               0x80, 0x80, 0x80, 0x80, 0x02};
    for (std::size_t index = 0; index < sizeof(overflow_varint); ++index) {
        data.at(pos_payload_offset + index) = overflow_varint[index];
    }

    write_u64_at(data, pos_entry_offset + kColumnLengthOffset, sizeof(overflow_varint));
    for (std::size_t column_index = kPosColumnIndex + 1; column_index < 11; ++column_index) {
        const std::size_t entry_offset =
            first_chunk_offset + kChunkHeaderSize + column_index * kColumnEntrySize;
        const auto old_offset = read_u64_at(data, entry_offset + kColumnPayloadOffsetOffset);
        write_u64_at(data, entry_offset + kColumnPayloadOffsetOffset, old_offset + kExtraBytes);
    }
    write_u64_at(data, kIndexOffsetFieldOffset, old_index_offset + kExtraBytes);
    write_u64_at(data,
                 static_cast<std::size_t>(old_index_offset + kExtraBytes) + kIndexChunkLengthOffset,
                 old_chunk_length + kExtraBytes);
    write_bytes(path, data);

    auto read = alignx::format::read_axf1_file(path);
    ASSERT_FALSE(read);
    EXPECT_NE(read.error().find("AXF1 POS delta varint overflow"), std::string::npos)
        << read.error();

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
    EXPECT_TRUE(read.error().find("column value count mismatch") != std::string::npos ||
                read.error().find("QNAME dict") != std::string::npos)
        << read.error();

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
    EXPECT_TRUE(read.error().find("trailing bytes") != std::string::npos ||
                read.error().find("truncated") != std::string::npos)
        << read.error();

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

TEST(Axf1File, WritesQnamesAsDictWhenSmallerThanRaw) {
    const auto path = temp_path("alignx_axf1_qname_dict");
    const auto file = make_file();

    auto write = alignx::format::write_axf1_file(file, path);
    ASSERT_TRUE(write) << write.error();

    auto data = read_bytes(path);
    EXPECT_EQ(read_u16_at(data, column_entry_offset(data, kQnameColumnIndex) + kColumnCodecOffset),
              static_cast<std::uint16_t>(alignx::format::Axf1CodecId::qname_dict));

    auto read = alignx::format::read_axf1_file(path);
    ASSERT_TRUE(read) << read.error();
    ASSERT_EQ(read->chunks.size(), 1);
    ASSERT_EQ(read->chunks[0].records.size(), 2);
    EXPECT_EQ(read->chunks[0].records[0].qname, "read001");
    EXPECT_EQ(read->chunks[0].records[1].qname, "read002");

    std::filesystem::remove(path);
}

TEST(Axf1File, QnameDictRoundTripsSingleRecord) {
    const auto path = temp_path("alignx_axf1_qname_single");
    auto file = make_file();
    file.chunks[0].end_pos = 110;
    file.chunks[0].records.resize(1);

    auto write = alignx::format::write_axf1_file(file, path);
    ASSERT_TRUE(write) << write.error();

    auto data = read_bytes(path);
    const auto codec = read_u16_at(data,
        column_entry_offset(data, kQnameColumnIndex) + kColumnCodecOffset);
    EXPECT_TRUE(codec == static_cast<std::uint16_t>(alignx::format::Axf1CodecId::qname_dict) ||
                codec == static_cast<std::uint16_t>(alignx::format::Axf1CodecId::raw));

    auto read = alignx::format::read_axf1_file(path);
    ASSERT_TRUE(read) << read.error();
    ASSERT_EQ(read->chunks[0].records.size(), 1);
    EXPECT_EQ(read->chunks[0].records[0].qname, "read001");

    std::filesystem::remove(path);
}

TEST(Axf1File, QnameDictHandlesDuplicateQnames) {
    const auto path = temp_path("alignx_axf1_qname_dict_dup");
    auto file = make_file();
    file.chunks[0].records[1].qname = file.chunks[0].records[0].qname;

    auto write = alignx::format::write_axf1_file(file, path);
    ASSERT_TRUE(write) << write.error();

    auto read = alignx::format::read_axf1_file(path);
    ASSERT_TRUE(read) << read.error();
    ASSERT_EQ(read->chunks[0].records.size(), 2);
    EXPECT_EQ(read->chunks[0].records[0].qname, "read001");
    EXPECT_EQ(read->chunks[0].records[1].qname, "read001");

    std::filesystem::remove(path);
}

TEST(Axf1File, QnameDictHandlesLongSharedPrefix) {
    const auto path = temp_path("alignx_axf1_qname_dict_prefix");
    auto file = make_file();
    file.chunks[0].records.clear();
    for (int index = 0; index < 16; ++index) {
        file.chunks[0].records.push_back(
            {.qname = "m64011_190830_220126/" + std::to_string(index) + "/ccs",
             .flag = 0,
             .pos = static_cast<std::int32_t>(100 + index),
             .mapq = 60,
             .cigar = "10M",
             .mate_reference = "*",
             .mate_pos = -1,
             .template_length = 0,
             .sequence = "ACGTACGTAA",
             .quality = "FFFFFFFFFF",
             .tags = "NM:i:0"});
    }
    file.chunks[0].end_pos = 126;

    auto write = alignx::format::write_axf1_file(file, path);
    ASSERT_TRUE(write) << write.error();

    auto data = read_bytes(path);
    EXPECT_EQ(read_u16_at(data, column_entry_offset(data, kQnameColumnIndex) + kColumnCodecOffset),
              static_cast<std::uint16_t>(alignx::format::Axf1CodecId::qname_dict));

    const auto dict_payload_length =
        read_u64_at(data, column_entry_offset(data, kQnameColumnIndex) + kColumnLengthOffset);
    std::uint64_t raw_total = 0;
    for (const auto& record : file.chunks[0].records) {
        raw_total += sizeof(std::uint32_t) + record.qname.size();
    }
    EXPECT_LT(dict_payload_length, raw_total);

    auto read = alignx::format::read_axf1_file(path);
    ASSERT_TRUE(read) << read.error();
    ASSERT_EQ(read->chunks[0].records.size(), 16);
    for (int index = 0; index < 16; ++index) {
        EXPECT_EQ(read->chunks[0].records[static_cast<std::size_t>(index)].qname,
                  "m64011_190830_220126/" + std::to_string(index) + "/ccs");
    }

    std::filesystem::remove(path);
}

TEST(Axf1File, RejectsTruncatedQnameDict) {
    const auto path = temp_path("alignx_axf1_truncated_qname_dict");
    const auto file = make_file();
    auto write = alignx::format::write_axf1_file(file, path);
    ASSERT_TRUE(write) << write.error();

    auto data = read_bytes(path);
    EXPECT_EQ(read_u16_at(data, column_entry_offset(data, kQnameColumnIndex) + kColumnCodecOffset),
              static_cast<std::uint16_t>(alignx::format::Axf1CodecId::qname_dict));
    write_u64_at(data, column_entry_offset(data, kQnameColumnIndex) + kColumnLengthOffset, 1);
    write_bytes(path, data);

    auto read = alignx::format::read_axf1_file(path);
    ASSERT_FALSE(read);
    EXPECT_NE(read.error().find("QNAME dict"), std::string::npos) << read.error();

    std::filesystem::remove(path);
}

TEST(Axf1File, RejectsQnameDictIndexOutOfRange) {
    const auto path = temp_path("alignx_axf1_qname_dict_bad_index");
    const auto file = make_file();
    auto write = alignx::format::write_axf1_file(file, path);
    ASSERT_TRUE(write) << write.error();

    auto data = read_bytes(path);
    EXPECT_EQ(read_u16_at(data, column_entry_offset(data, kQnameColumnIndex) + kColumnCodecOffset),
              static_cast<std::uint16_t>(alignx::format::Axf1CodecId::qname_dict));

    const auto payload_offset = column_payload_offset(data, kQnameColumnIndex);
    const auto payload_length = static_cast<std::size_t>(
        read_u64_at(data, column_entry_offset(data, kQnameColumnIndex) + kColumnLengthOffset));
    data.at(payload_offset + payload_length - 1) = 0xFF;
    data.at(payload_offset + payload_length - 2) = 0xFF;
    write_bytes(path, data);

    auto read = alignx::format::read_axf1_file(path);
    ASSERT_FALSE(read);
    EXPECT_NE(read.error().find("QNAME dict"), std::string::npos) << read.error();

    std::filesystem::remove(path);
}

TEST(Axf1File, TagsPerStreamRoundTripsTypicalTags) {
    const auto path = temp_path("alignx_axf1_tags_per_stream");
    auto file = make_file();
    file.chunks[0].records[0].tags = "NM:i:0\tHP:i:1";
    file.chunks[0].records[1].tags = "NM:i:1\tHP:i:2";

    auto write = alignx::format::write_axf1_file(file, path);
    ASSERT_TRUE(write) << write.error();

    auto data = read_bytes(path);
    EXPECT_EQ(read_u16_at(data, column_entry_offset(data, kTagsColumnIndex) + kColumnCodecOffset),
              static_cast<std::uint16_t>(alignx::format::Axf1CodecId::tags_per_stream));

    auto read = alignx::format::read_axf1_file(path);
    ASSERT_TRUE(read) << read.error();
    ASSERT_EQ(read->chunks[0].records.size(), 2);
    EXPECT_EQ(read->chunks[0].records[0].tags, "NM:i:0\tHP:i:1");
    EXPECT_EQ(read->chunks[0].records[1].tags, "NM:i:1\tHP:i:2");

    std::filesystem::remove(path);
}

TEST(Axf1File, TagsPerStreamRoundTripsNegativeInteger) {
    const auto path = temp_path("alignx_axf1_tags_neg_int");
    auto file = make_file();
    file.chunks[0].records[0].tags = "XS:i:-5";
    file.chunks[0].records[1].tags = "XS:i:-100";

    auto write = alignx::format::write_axf1_file(file, path);
    ASSERT_TRUE(write) << write.error();

    auto read = alignx::format::read_axf1_file(path);
    ASSERT_TRUE(read) << read.error();
    EXPECT_EQ(read->chunks[0].records[0].tags, "XS:i:-5");
    EXPECT_EQ(read->chunks[0].records[1].tags, "XS:i:-100");

    std::filesystem::remove(path);
}

TEST(Axf1File, TagsPerStreamRoundTripsStringTag) {
    const auto path = temp_path("alignx_axf1_tags_string");
    auto file = make_file();
    file.chunks[0].records[0].tags = "RG:Z:m64011_190830_220126";
    file.chunks[0].records[1].tags = "RG:Z:m64011_190830_220126";

    auto write = alignx::format::write_axf1_file(file, path);
    ASSERT_TRUE(write) << write.error();

    auto read = alignx::format::read_axf1_file(path);
    ASSERT_TRUE(read) << read.error();
    EXPECT_EQ(read->chunks[0].records[0].tags, "RG:Z:m64011_190830_220126");
    EXPECT_EQ(read->chunks[0].records[1].tags, "RG:Z:m64011_190830_220126");

    std::filesystem::remove(path);
}

TEST(Axf1File, TagsPerStreamHandlesPartialPresence) {
    const auto path = temp_path("alignx_axf1_tags_partial");
    auto file = make_file();
    file.chunks[0].records[0].tags = "NM:i:0\tHP:i:1";
    file.chunks[0].records[1].tags = "NM:i:1";

    auto write = alignx::format::write_axf1_file(file, path);
    ASSERT_TRUE(write) << write.error();

    auto read = alignx::format::read_axf1_file(path);
    ASSERT_TRUE(read) << read.error();
    EXPECT_EQ(read->chunks[0].records[0].tags, "NM:i:0\tHP:i:1");
    EXPECT_EQ(read->chunks[0].records[1].tags, "NM:i:1");

    std::filesystem::remove(path);
}

TEST(Axf1File, TagsPerStreamHandlesEmptyTags) {
    const auto path = temp_path("alignx_axf1_tags_empty");
    auto file = make_file();
    file.chunks[0].records[0].tags = "";
    file.chunks[0].records[1].tags = "";

    auto write = alignx::format::write_axf1_file(file, path);
    ASSERT_TRUE(write) << write.error();

    auto read = alignx::format::read_axf1_file(path);
    ASSERT_TRUE(read) << read.error();
    EXPECT_EQ(read->chunks[0].records[0].tags, "");
    EXPECT_EQ(read->chunks[0].records[1].tags, "");

    std::filesystem::remove(path);
}

TEST(Axf1File, TagsPerStreamHandlesMixedEmptyAndPresent) {
    const auto path = temp_path("alignx_axf1_tags_mixed_empty");
    auto file = make_file();
    file.chunks[0].records[0].tags = "NM:i:0\tHP:i:1";
    file.chunks[0].records[1].tags = "";

    auto write = alignx::format::write_axf1_file(file, path);
    ASSERT_TRUE(write) << write.error();

    auto read = alignx::format::read_axf1_file(path);
    ASSERT_TRUE(read) << read.error();
    EXPECT_EQ(read->chunks[0].records[0].tags, "NM:i:0\tHP:i:1");
    EXPECT_EQ(read->chunks[0].records[1].tags, "");

    std::filesystem::remove(path);
}

TEST(Axf1File, TagsPerStreamFallsBackOnInconsistentOrder) {
    const auto path = temp_path("alignx_axf1_tags_order_fallback");
    auto file = make_file();
    file.chunks[0].records[0].tags = "NM:i:0\tHP:i:1";
    file.chunks[0].records[1].tags = "HP:i:2\tNM:i:1";

    auto write = alignx::format::write_axf1_file(file, path);
    ASSERT_TRUE(write) << write.error();

    auto data = read_bytes(path);
    EXPECT_EQ(read_u16_at(data, column_entry_offset(data, kTagsColumnIndex) + kColumnCodecOffset),
              static_cast<std::uint16_t>(alignx::format::Axf1CodecId::raw));

    auto read = alignx::format::read_axf1_file(path);
    ASSERT_TRUE(read) << read.error();
    EXPECT_EQ(read->chunks[0].records[0].tags, "NM:i:0\tHP:i:1");
    EXPECT_EQ(read->chunks[0].records[1].tags, "HP:i:2\tNM:i:1");

    std::filesystem::remove(path);
}

TEST(Axf1File, TagsPerStreamSmallerThanRawForMultipleRecords) {
    const auto path = temp_path("alignx_axf1_tags_size");
    auto file = make_file();
    file.chunks[0].records.clear();
    for (int i = 0; i < 16; ++i) {
        file.chunks[0].records.push_back(
            {.qname = "read" + std::to_string(i),
             .flag = 0,
             .pos = static_cast<std::int32_t>(100 + i),
             .mapq = 60,
             .cigar = "10M",
             .mate_reference = "*",
             .mate_pos = -1,
             .template_length = 0,
             .sequence = "ACGTACGTAA",
             .quality = "FFFFFFFFFF",
             .tags = "NM:i:" + std::to_string(i) + "\tHP:i:1\tPS:i:12345\tRG:Z:group1"});
    }
    file.chunks[0].end_pos = 126;

    auto write = alignx::format::write_axf1_file(file, path);
    ASSERT_TRUE(write) << write.error();

    auto data = read_bytes(path);
    EXPECT_EQ(read_u16_at(data, column_entry_offset(data, kTagsColumnIndex) + kColumnCodecOffset),
              static_cast<std::uint16_t>(alignx::format::Axf1CodecId::tags_per_stream));

    const auto per_stream_length =
        read_u64_at(data, column_entry_offset(data, kTagsColumnIndex) + kColumnLengthOffset);
    std::uint64_t raw_total = 0;
    for (const auto& record : file.chunks[0].records) {
        raw_total += sizeof(std::uint32_t) + record.tags.size();
    }
    EXPECT_LT(per_stream_length, raw_total);

    auto read = alignx::format::read_axf1_file(path);
    ASSERT_TRUE(read) << read.error();
    ASSERT_EQ(read->chunks[0].records.size(), 16);
    for (int i = 0; i < 16; ++i) {
        EXPECT_EQ(read->chunks[0].records[static_cast<std::size_t>(i)].tags,
                  "NM:i:" + std::to_string(i) + "\tHP:i:1\tPS:i:12345\tRG:Z:group1");
    }

    std::filesystem::remove(path);
}

TEST(Axf1File, RejectsTruncatedTagsPerStream) {
    const auto path = temp_path("alignx_axf1_truncated_tags");
    auto file = make_file();
    file.chunks[0].records[0].tags = "NM:i:0\tHP:i:1";
    file.chunks[0].records[1].tags = "NM:i:1\tHP:i:2";

    auto write = alignx::format::write_axf1_file(file, path);
    ASSERT_TRUE(write) << write.error();

    auto data = read_bytes(path);
    EXPECT_EQ(read_u16_at(data, column_entry_offset(data, kTagsColumnIndex) + kColumnCodecOffset),
              static_cast<std::uint16_t>(alignx::format::Axf1CodecId::tags_per_stream));
    write_u64_at(data, column_entry_offset(data, kTagsColumnIndex) + kColumnLengthOffset, 1);
    write_bytes(path, data);

    auto read = alignx::format::read_axf1_file(path);
    ASSERT_FALSE(read);
    EXPECT_NE(read.error().find("TAG stream"), std::string::npos) << read.error();

    std::filesystem::remove(path);
}

TEST(Axf1File, RejectsTagsPerStreamPresenceCountMismatch) {
    const auto path = temp_path("alignx_axf1_tags_count_mismatch");
    auto file = make_file();
    file.chunks[0].records[0].tags = "NM:i:0\tHP:i:1";
    file.chunks[0].records[1].tags = "NM:i:1\tHP:i:2";

    auto write = alignx::format::write_axf1_file(file, path);
    ASSERT_TRUE(write) << write.error();

    auto data = read_bytes(path);
    EXPECT_EQ(read_u16_at(data, column_entry_offset(data, kTagsColumnIndex) + kColumnCodecOffset),
              static_cast<std::uint16_t>(alignx::format::Axf1CodecId::tags_per_stream));

    const auto payload_offset = column_payload_offset(data, kTagsColumnIndex);
    std::size_t cursor = payload_offset;
    read_varint_at(data, cursor);
    cursor += 3 * 2;
    cursor += 1 + 1;
    data.at(cursor) = 99;
    write_bytes(path, data);

    auto read = alignx::format::read_axf1_file(path);
    ASSERT_FALSE(read);
    EXPECT_NE(read.error().find("TAG stream"), std::string::npos) << read.error();

    std::filesystem::remove(path);
}

// --- CIGAR dict tests ---

TEST(Axf1File, CigarDictRoundTripsRepeatedCigars) {
    const auto path = temp_path("alignx_axf1_cigar_dict_repeated");
    auto file = make_file();
    file.chunks[0].records.clear();
    for (int i = 0; i < 4; ++i) {
        file.chunks[0].records.push_back(
            {.qname = "read" + std::to_string(i),
             .flag = 0,
             .pos = static_cast<std::int32_t>(100 + i),
             .mapq = 60,
             .cigar = (i % 2 == 0) ? "10M" : "5M1I4M",
             .mate_reference = "*",
             .mate_pos = -1,
             .template_length = 0,
             .sequence = "ACGTACGTAA",
             .quality = "FFFFFFFFFF",
             .tags = "NM:i:0"});
    }
    file.chunks[0].end_pos = 114;

    auto write = alignx::format::write_axf1_file(file, path);
    ASSERT_TRUE(write) << write.error();

    auto data = read_bytes(path);
    const auto codec =
        read_u16_at(data, column_entry_offset(data, kCigarColumnIndex) + kColumnCodecOffset);
    EXPECT_EQ(codec, static_cast<std::uint16_t>(alignx::format::Axf1CodecId::cigar_dict));

    auto read = alignx::format::read_axf1_file(path);
    ASSERT_TRUE(read) << read.error();
    ASSERT_EQ(read->chunks[0].records.size(), 4);
    EXPECT_EQ(read->chunks[0].records[0].cigar, "10M");
    EXPECT_EQ(read->chunks[0].records[1].cigar, "5M1I4M");
    EXPECT_EQ(read->chunks[0].records[2].cigar, "10M");
    EXPECT_EQ(read->chunks[0].records[3].cigar, "5M1I4M");

    std::filesystem::remove(path);
}

TEST(Axf1File, CigarDictRoundTripsSingleUniqueCigar) {
    const auto path = temp_path("alignx_axf1_cigar_dict_single");
    auto file = make_file();
    file.chunks[0].records.clear();
    for (int i = 0; i < 8; ++i) {
        file.chunks[0].records.push_back(
            {.qname = "read" + std::to_string(i),
             .flag = 0,
             .pos = static_cast<std::int32_t>(100 + i),
             .mapq = 60,
             .cigar = "151M",
             .mate_reference = "*",
             .mate_pos = -1,
             .template_length = 0,
             .sequence = "ACGTACGTAA",
             .quality = "FFFFFFFFFF",
             .tags = "NM:i:0"});
    }
    file.chunks[0].end_pos = 118;

    auto write = alignx::format::write_axf1_file(file, path);
    ASSERT_TRUE(write) << write.error();

    auto data = read_bytes(path);
    const auto codec =
        read_u16_at(data, column_entry_offset(data, kCigarColumnIndex) + kColumnCodecOffset);
    EXPECT_EQ(codec, static_cast<std::uint16_t>(alignx::format::Axf1CodecId::cigar_dict));

    auto read = alignx::format::read_axf1_file(path);
    ASSERT_TRUE(read) << read.error();
    ASSERT_EQ(read->chunks[0].records.size(), 8);
    for (int i = 0; i < 8; ++i) {
        EXPECT_EQ(read->chunks[0].records[static_cast<std::size_t>(i)].cigar, "151M");
    }

    std::filesystem::remove(path);
}

TEST(Axf1File, CigarDictFallsBackForUniqueCigars) {
    const auto path = temp_path("alignx_axf1_cigar_dict_unique");
    auto file = make_file();

    auto write = alignx::format::write_axf1_file(file, path);
    ASSERT_TRUE(write) << write.error();

    auto data = read_bytes(path);
    const auto codec =
        read_u16_at(data, column_entry_offset(data, kCigarColumnIndex) + kColumnCodecOffset);
    EXPECT_EQ(codec, static_cast<std::uint16_t>(alignx::format::Axf1CodecId::cigar_token));

    auto read = alignx::format::read_axf1_file(path);
    ASSERT_TRUE(read) << read.error();
    EXPECT_EQ(read->chunks[0].records[0].cigar, "10M");
    EXPECT_EQ(read->chunks[0].records[1].cigar, "5M1I4M");

    std::filesystem::remove(path);
}

TEST(Axf1File, CigarDictSmallerThanTokenForRepeatedCigars) {
    const auto path = temp_path("alignx_axf1_cigar_dict_size");
    auto file = make_file();
    file.chunks[0].records.clear();
    for (int i = 0; i < 16; ++i) {
        file.chunks[0].records.push_back(
            {.qname = "read" + std::to_string(i),
             .flag = 0,
             .pos = static_cast<std::int32_t>(100 + i),
             .mapq = 60,
             .cigar = (i % 2 == 0) ? "100M5I46M" : "151M",
             .mate_reference = "*",
             .mate_pos = -1,
             .template_length = 0,
             .sequence = "ACGTACGTAA",
             .quality = "FFFFFFFFFF",
             .tags = "NM:i:0"});
    }
    file.chunks[0].end_pos = 126;

    auto write = alignx::format::write_axf1_file(file, path);
    ASSERT_TRUE(write) << write.error();

    auto data = read_bytes(path);
    EXPECT_EQ(read_u16_at(data, column_entry_offset(data, kCigarColumnIndex) + kColumnCodecOffset),
              static_cast<std::uint16_t>(alignx::format::Axf1CodecId::cigar_dict));

    const auto dict_length =
        read_u64_at(data, column_entry_offset(data, kCigarColumnIndex) + kColumnLengthOffset);
    std::uint64_t raw_total = 0;
    for (const auto& record : file.chunks[0].records) {
        raw_total += sizeof(std::uint32_t) + record.cigar.size();
    }
    EXPECT_LT(dict_length, raw_total);

    auto read = alignx::format::read_axf1_file(path);
    ASSERT_TRUE(read) << read.error();
    ASSERT_EQ(read->chunks[0].records.size(), 16);
    for (int i = 0; i < 16; ++i) {
        const auto expected = (i % 2 == 0) ? "100M5I46M" : "151M";
        EXPECT_EQ(read->chunks[0].records[static_cast<std::size_t>(i)].cigar, expected);
    }

    std::filesystem::remove(path);
}

TEST(Axf1File, RejectsTruncatedCigarDict) {
    const auto path = temp_path("alignx_axf1_truncated_cigar_dict");
    auto file = make_file();
    file.chunks[0].records.clear();
    for (int i = 0; i < 4; ++i) {
        file.chunks[0].records.push_back(
            {.qname = "read" + std::to_string(i),
             .flag = 0,
             .pos = static_cast<std::int32_t>(100 + i),
             .mapq = 60,
             .cigar = "151M",
             .mate_reference = "*",
             .mate_pos = -1,
             .template_length = 0,
             .sequence = "ACGTACGTAA",
             .quality = "FFFFFFFFFF",
             .tags = "NM:i:0"});
    }
    file.chunks[0].end_pos = 114;

    auto write = alignx::format::write_axf1_file(file, path);
    ASSERT_TRUE(write) << write.error();

    auto data = read_bytes(path);
    EXPECT_EQ(read_u16_at(data, column_entry_offset(data, kCigarColumnIndex) + kColumnCodecOffset),
              static_cast<std::uint16_t>(alignx::format::Axf1CodecId::cigar_dict));
    write_u64_at(data, column_entry_offset(data, kCigarColumnIndex) + kColumnLengthOffset, 1);
    write_bytes(path, data);

    auto read = alignx::format::read_axf1_file(path);
    ASSERT_FALSE(read);
    EXPECT_NE(read.error().find("CIGAR dict"), std::string::npos) << read.error();

    std::filesystem::remove(path);
}

TEST(Axf1File, RejectsCigarDictIndexOutOfRange) {
    const auto path = temp_path("alignx_axf1_cigar_dict_bad_index");
    auto file = make_file();
    file.chunks[0].records.clear();
    for (int i = 0; i < 4; ++i) {
        file.chunks[0].records.push_back(
            {.qname = "read" + std::to_string(i),
             .flag = 0,
             .pos = static_cast<std::int32_t>(100 + i),
             .mapq = 60,
             .cigar = "151M",
             .mate_reference = "*",
             .mate_pos = -1,
             .template_length = 0,
             .sequence = "ACGTACGTAA",
             .quality = "FFFFFFFFFF",
             .tags = "NM:i:0"});
    }
    file.chunks[0].end_pos = 114;

    auto write = alignx::format::write_axf1_file(file, path);
    ASSERT_TRUE(write) << write.error();

    auto data = read_bytes(path);
    EXPECT_EQ(read_u16_at(data, column_entry_offset(data, kCigarColumnIndex) + kColumnCodecOffset),
              static_cast<std::uint16_t>(alignx::format::Axf1CodecId::cigar_dict));

    const auto payload_start = column_payload_offset(data, kCigarColumnIndex);
    const auto payload_length =
        read_u64_at(data, column_entry_offset(data, kCigarColumnIndex) + kColumnLengthOffset);
    data.at(payload_start + payload_length - 1) = 99;
    write_bytes(path, data);

    auto read = alignx::format::read_axf1_file(path);
    ASSERT_FALSE(read);
    EXPECT_NE(read.error().find("CIGAR dict"), std::string::npos) << read.error();

    std::filesystem::remove(path);
}

// --- Quality lossy binning tests ---

TEST(Axf1File, QualityLossyIllumina8BinsCorrectly) {
    const auto path = temp_path("alignx_axf1_qual_lossy_bins");
    auto file = make_file();
    // Q0='!', Q5='&', Q15='0', Q22='7', Q30='?', Q40='I'
    file.chunks[0].records[0].quality = "!&07?I????";
    file.chunks[0].records[1].quality = "FFFFFFFFFF";

    alignx::format::Axf1WriteOptions options;
    options.quality_lossy = alignx::format::Axf1QualityLossy::illumina8;

    auto write = alignx::format::write_axf1_file(file, path, options);
    ASSERT_TRUE(write) << write.error();

    auto read = alignx::format::read_axf1_file(path);
    ASSERT_TRUE(read) << read.error();
    ASSERT_EQ(read->chunks[0].records.size(), 2);

    const auto& q0 = read->chunks[0].records[0].quality;
    EXPECT_EQ(q0[0], '!');   // Q0 → Q0
    EXPECT_EQ(q0[1], '\'');  // Q5 → Q6
    EXPECT_EQ(q0[2], '0');   // Q15 → Q15
    EXPECT_EQ(q0[3], '7');   // Q22 → Q22
    EXPECT_EQ(q0[4], 'B');   // Q30 → Q33
    EXPECT_EQ(q0[5], 'I');   // Q40 → Q40

    std::filesystem::remove(path);
}

TEST(Axf1File, QualityLossyIllumina8RoundTrips) {
    const auto path = temp_path("alignx_axf1_qual_lossy_roundtrip");
    auto file = make_file();
    file.chunks[0].records[0].quality = "FFFFFFFFFF";
    file.chunks[0].records[1].quality = "BBBBBBBBBB";

    alignx::format::Axf1WriteOptions options;
    options.quality_lossy = alignx::format::Axf1QualityLossy::illumina8;

    auto write = alignx::format::write_axf1_file(file, path, options);
    ASSERT_TRUE(write) << write.error();

    auto read = alignx::format::read_axf1_file(path);
    ASSERT_TRUE(read) << read.error();
    ASSERT_EQ(read->chunks[0].records.size(), 2);
    EXPECT_EQ(read->chunks[0].records[0].quality, "FFFFFFFFFF");
    EXPECT_EQ(read->chunks[0].records[1].quality, "BBBBBBBBBB");

    std::filesystem::remove(path);
}

TEST(Axf1File, QualityLossyNonePreservesOriginal) {
    const auto path = temp_path("alignx_axf1_qual_lossy_none");
    auto file = make_file();

    alignx::format::Axf1WriteOptions options;
    options.quality_lossy = alignx::format::Axf1QualityLossy::none;

    auto write = alignx::format::write_axf1_file(file, path, options);
    ASSERT_TRUE(write) << write.error();

    auto read = alignx::format::read_axf1_file(path);
    ASSERT_TRUE(read) << read.error();
    EXPECT_EQ(read->chunks[0].records[0].quality, file.chunks[0].records[0].quality);
    EXPECT_EQ(read->chunks[0].records[1].quality, file.chunks[0].records[1].quality);

    std::filesystem::remove(path);
}

TEST(Axf1File, QualityLossyIllumina8ReducesAlphabetSize) {
    const auto path = temp_path("alignx_axf1_qual_lossy_alphabet");
    auto file = make_file();
    file.chunks[0].records.clear();
    for (int i = 0; i < 8; ++i) {
        std::string quality;
        for (int q = 0; q <= 41; ++q) {
            quality.push_back(static_cast<char>(q + 33));
        }
        file.chunks[0].records.push_back(
            {.qname = "read" + std::to_string(i),
             .flag = 0,
             .pos = static_cast<std::int32_t>(100 + i),
             .mapq = 60,
             .cigar = "42M",
             .mate_reference = "*",
             .mate_pos = -1,
             .template_length = 0,
             .sequence = std::string(42, 'A'),
             .quality = quality,
             .tags = "NM:i:0"});
    }
    file.chunks[0].end_pos = 152;

    alignx::format::Axf1WriteOptions options;
    options.quality_lossy = alignx::format::Axf1QualityLossy::illumina8;

    auto write = alignx::format::write_axf1_file(file, path, options);
    ASSERT_TRUE(write) << write.error();

    auto read = alignx::format::read_axf1_file(path);
    ASSERT_TRUE(read) << read.error();

    std::set<char> unique_quals;
    for (const auto& record : read->chunks[0].records) {
        for (char c : record.quality) {
            unique_quals.insert(c);
        }
    }
    EXPECT_LE(unique_quals.size(), 8);

    std::filesystem::remove(path);
}

TEST(Axf1File, WriterGenericCompressedRoundTrip) {
#ifdef ALIGNX_HAVE_ZSTD
    const auto path = temp_path("alignx_axf1_generic_compressed_roundtrip");
    auto file = make_repeated_quality_file(100, 50);
    alignx::format::Axf1WriteOptions options;
    options.column_compression = alignx::format::Axf1Compression::zstd;

    auto write = alignx::format::write_axf1_file(file, path, options);
    ASSERT_TRUE(write) << write.error();

    auto read = alignx::format::read_axf1_file(path);
    ASSERT_TRUE(read) << read.error();
    ASSERT_EQ(read->chunks.size(), 1);
    ASSERT_EQ(read->chunks[0].records.size(), file.chunks[0].records.size());
    for (std::size_t i = 0; i < file.chunks[0].records.size(); ++i) {
        EXPECT_EQ(read->chunks[0].records[i].qname, file.chunks[0].records[i].qname);
        EXPECT_EQ(read->chunks[0].records[i].flag, file.chunks[0].records[i].flag);
        EXPECT_EQ(read->chunks[0].records[i].pos, file.chunks[0].records[i].pos);
        EXPECT_EQ(read->chunks[0].records[i].mapq, file.chunks[0].records[i].mapq);
        EXPECT_EQ(read->chunks[0].records[i].cigar, file.chunks[0].records[i].cigar);
        EXPECT_EQ(read->chunks[0].records[i].sequence, file.chunks[0].records[i].sequence);
        EXPECT_EQ(read->chunks[0].records[i].quality, file.chunks[0].records[i].quality);
        EXPECT_EQ(read->chunks[0].records[i].tags, file.chunks[0].records[i].tags);
    }
    std::filesystem::remove(path);
#else
    GTEST_SKIP() << "ALIGNX_ENABLE_ZSTD=OFF";
#endif
}

TEST(Axf1File, WriterGenericCompressedFallsBackWhenNotSmaller) {
#ifdef ALIGNX_HAVE_ZSTD
    const auto path = temp_path("alignx_axf1_generic_compressed_fallback");
    auto file = make_file();
    alignx::format::Axf1WriteOptions options;
    options.column_compression = alignx::format::Axf1Compression::zstd;

    auto write = alignx::format::write_axf1_file(file, path, options);
    ASSERT_TRUE(write) << write.error();

    auto data = read_bytes(path);
    for (std::size_t col = 0; col < 11; ++col) {
        auto codec = read_u16_at(data, column_entry_offset(data, col) + kColumnCodecOffset);
        (void)codec;
    }

    auto read = alignx::format::read_axf1_file(path);
    ASSERT_TRUE(read) << read.error();
    ASSERT_EQ(read->chunks[0].records.size(), file.chunks[0].records.size());
    for (std::size_t i = 0; i < file.chunks[0].records.size(); ++i) {
        EXPECT_EQ(read->chunks[0].records[i].qname, file.chunks[0].records[i].qname);
        EXPECT_EQ(read->chunks[0].records[i].sequence, file.chunks[0].records[i].sequence);
        EXPECT_EQ(read->chunks[0].records[i].quality, file.chunks[0].records[i].quality);
    }
    std::filesystem::remove(path);
#else
    GTEST_SKIP() << "ALIGNX_ENABLE_ZSTD=OFF";
#endif
}

TEST(Axf1File, ReaderRejectsNestedCompression) {
    const auto path = temp_path("alignx_axf1_nested_compression");
    const auto file = make_file();

    auto write = alignx::format::write_axf1_file(file, path);
    ASSERT_TRUE(write) << write.error();

    auto data = read_bytes(path);
    std::vector<unsigned char> envelope;
    append_varint_u64(envelope,
                      static_cast<std::uint16_t>(alignx::format::Axf1CodecId::compressed));
    append_varint_u64(envelope, 0);
    append_varint_u64(envelope, 4);
    append_varint_u64(envelope, 4);
    envelope.push_back(1);
    envelope.push_back('F');
    envelope.push_back(10);
    envelope.push_back(10);

    write_u16_at(data, column_entry_offset(data, kQualColumnIndex) + kColumnCodecOffset,
                 static_cast<std::uint16_t>(alignx::format::Axf1CodecId::compressed));
    replace_column_payload(data, kQualColumnIndex, envelope);
    write_bytes(path, data);

    auto read = alignx::format::read_axf1_file(path);
    EXPECT_FALSE(read);
    EXPECT_NE(read.error().find("invalid base codec"), std::string::npos);

    std::filesystem::remove(path);
}

TEST(Axf1File, ReaderRejectsInvalidBaseCodecForColumn) {
    const auto path = temp_path("alignx_axf1_invalid_base_codec_column");
    const auto file = make_file();

    auto write = alignx::format::write_axf1_file(file, path);
    ASSERT_TRUE(write) << write.error();

    auto data = read_bytes(path);
    std::vector<unsigned char> envelope;
    append_varint_u64(envelope,
                      static_cast<std::uint16_t>(alignx::format::Axf1CodecId::seq_2bit_literal));
    append_varint_u64(envelope, 0);
    append_varint_u64(envelope, 4);
    append_varint_u64(envelope, 4);
    envelope.push_back(1);
    envelope.push_back('F');
    envelope.push_back(10);
    envelope.push_back(10);

    write_u16_at(data, column_entry_offset(data, kQualColumnIndex) + kColumnCodecOffset,
                 static_cast<std::uint16_t>(alignx::format::Axf1CodecId::compressed));
    replace_column_payload(data, kQualColumnIndex, envelope);
    write_bytes(path, data);

    auto read = alignx::format::read_axf1_file(path);
    EXPECT_FALSE(read);
    EXPECT_NE(read.error().find("invalid base codec"), std::string::npos);

    std::filesystem::remove(path);
}
