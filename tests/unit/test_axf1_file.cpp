#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
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
constexpr std::size_t kFlagColumnIndex = 1;
constexpr std::size_t kPosColumnIndex = 2;
constexpr std::size_t kMapqColumnIndex = 3;
constexpr std::size_t kCigarColumnIndex = 4;
constexpr std::size_t kSeqColumnIndex = 8;
constexpr std::size_t kQualColumnIndex = 9;

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

alignx::format::Axf1File make_single_record_file(std::string quality) {
    auto file = make_file();
    file.chunks[0].end_pos = 110;
    file.chunks[0].records.resize(1);
    file.chunks[0].records[0].quality = std::move(quality);
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
        EXPECT_EQ(
            read_u16_at(data, column_entry_offset(data, kCigarColumnIndex) + kColumnCodecOffset),
            static_cast<std::uint16_t>(alignx::format::Axf1CodecId::raw));
        EXPECT_EQ(
            read_u64_at(data, column_entry_offset(data, kCigarColumnIndex) + kColumnLengthOffset),
            4 + file.chunks[0].records[0].cigar.size() + 4 + fallback_cigar.size());

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
    const auto file = make_file();
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
