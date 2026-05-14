#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string_view>
#include <utility>
#include <vector>

#include "format/axf1_file.hpp"
#include "query/axf1_view.hpp"

namespace {

constexpr std::size_t kIndexOffsetFieldOffset = 20;
constexpr std::size_t kIndexEntrySize = 32;
constexpr std::size_t kIndexChunkOffsetOffset = 16;
constexpr std::size_t kChunkHeaderSize = 18;
constexpr std::size_t kColumnEntrySize = 20;
constexpr std::size_t kColumnLengthOffset = 12;

std::filesystem::path temp_path(std::string_view label) {
    const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
           (std::string(label) + "_" + std::to_string(suffix) + ".axf1");
}

alignx::format::Axf1Record make_record(std::string qname, std::int32_t pos,
                                       std::string cigar = "10M", std::string tags = "NM:i:0") {
    return alignx::format::Axf1Record{.qname = std::move(qname),
                                      .flag = 0,
                                      .pos = pos,
                                      .mapq = 60,
                                      .cigar = std::move(cigar),
                                      .mate_reference = "*",
                                      .mate_pos = 0,
                                      .template_length = 0,
                                      .sequence = "ACGTACGTAA",
                                      .quality = "FFFFFFFFFF",
                                      .tags = std::move(tags)};
}

alignx::format::Axf1File make_file() {
    return alignx::format::Axf1File{
        .references = {{.name = "chrToy", .length = 1000}},
        .chunks = {{.ref_id = 0,
                    .start_pos = 100,
                    .end_pos = 310,
                    .records = {make_record("read001", 100, "10M", "NM:i:0"),
                                make_record("read002", 300, "10M", "NM:i:1")}}}};
}

void write_axf1_or_fail(const alignx::format::Axf1File& file, const std::filesystem::path& path) {
    auto write = alignx::format::write_axf1_file(file, path);
    ASSERT_TRUE(write) << write.error();
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

void write_u64_at(std::vector<unsigned char>& data, std::size_t offset, std::uint64_t value) {
    for (std::size_t byte = 0; byte < sizeof(value); ++byte) {
        data.at(offset + byte) = static_cast<unsigned char>((value >> (byte * 8U)) & 0xFFU);
    }
}

std::uint64_t read_index_offset(const std::vector<unsigned char>& data) {
    return read_u64_at(data, kIndexOffsetFieldOffset);
}

std::uint64_t read_chunk_offset(const std::vector<unsigned char>& data, std::size_t chunk_index) {
    return read_u64_at(data, static_cast<std::size_t>(read_index_offset(data)) +
                                 chunk_index * kIndexEntrySize + kIndexChunkOffsetOffset);
}

std::size_t column_entry_offset(const std::vector<unsigned char>& data, std::size_t chunk_index,
                                std::size_t column_index) {
    return static_cast<std::size_t>(read_chunk_offset(data, chunk_index)) + kChunkHeaderSize +
           column_index * kColumnEntrySize;
}

void corrupt_first_column_length(const std::filesystem::path& path, std::size_t chunk_index) {
    auto data = read_bytes(path);
    const std::size_t qname_length_offset =
        column_entry_offset(data, chunk_index, 0) + kColumnLengthOffset;
    write_u64_at(data, qname_length_offset, read_u64_at(data, qname_length_offset) + 1);
    write_bytes(path, data);
}

} // namespace

TEST(Axf1View, FiltersRecordsInsideOverlappingChunk) {
    const auto path = temp_path("alignx_axf1_view_filter");
    write_axf1_or_fail(make_file(), path);

    std::ostringstream out;
    auto result = alignx::query::write_axf1_region_sam(path, "chrToy:101-110", out);

    EXPECT_TRUE(result) << result.error();
    EXPECT_EQ(out.str(),
              "read001\t0\tchrToy\t101\t60\t10M\t*\t0\t0\tACGTACGTAA\tFFFFFFFFFF\tNM:i:0\n");

    std::filesystem::remove(path);
}

TEST(Axf1View, UsesCigarReferenceConsumingOperations) {
    const auto path = temp_path("alignx_axf1_view_cigar");
    alignx::format::Axf1File file{
        .references = {{.name = "chrToy", .length = 1000}},
        .chunks = {{.ref_id = 0,
                    .start_pos = 100,
                    .end_pos = 116,
                    .records = {make_record("readCigar", 100, "2S5M1I3D4N2=1X1H1P", "")}}}};
    write_axf1_or_fail(file, path);

    std::ostringstream out;
    auto result = alignx::query::write_axf1_region_sam(path, "chrToy:115-115", out);

    EXPECT_TRUE(result) << result.error();
    EXPECT_EQ(out.str(), "readCigar\t0\tchrToy\t101\t60\t2S5M1I3D4N2=1X1H1P\t*\t0\t0\tACGTACGTAA\t"
                         "FFFFFFFFFF\n");

    std::filesystem::remove(path);
}

TEST(Axf1View, ReturnsNoRecordsForNoHitRegion) {
    const auto path = temp_path("alignx_axf1_view_no_hit");
    write_axf1_or_fail(make_file(), path);

    std::ostringstream out;
    auto result = alignx::query::write_axf1_region_sam(path, "chrToy:501-510", out);

    EXPECT_TRUE(result) << result.error();
    EXPECT_EQ(out.str(), "");

    std::filesystem::remove(path);
}

TEST(Axf1View, PreservesChunkThenRecordOrderWithinQueriedReference) {
    const auto path = temp_path("alignx_axf1_view_order");
    alignx::format::Axf1File file{
        .references = {{.name = "chrA", .length = 1000}, {.name = "chrB", .length = 1000}},
        .chunks = {{.ref_id = 0,
                    .start_pos = 100,
                    .end_pos = 230,
                    .records = {make_record("chrA_read001", 100, "10M", "NM:i:0"),
                                make_record("chrA_read002", 220, "10M", "NM:i:1")}},
                   {.ref_id = 1,
                    .start_pos = 100,
                    .end_pos = 110,
                    .records = {make_record("chrB_read001", 100, "10M", "NM:i:0")}},
                   {.ref_id = 0,
                    .start_pos = 300,
                    .end_pos = 310,
                    .records = {make_record("chrA_read003", 300, "10M", "NM:i:2")}}}};
    write_axf1_or_fail(file, path);

    std::ostringstream out;
    auto result = alignx::query::write_axf1_region_sam(path, "chrA:1-400", out);

    EXPECT_TRUE(result) << result.error();
    EXPECT_EQ(out.str(),
              "chrA_read001\t0\tchrA\t101\t60\t10M\t*\t0\t0\tACGTACGTAA\tFFFFFFFFFF\tNM:i:0\n"
              "chrA_read002\t0\tchrA\t221\t60\t10M\t*\t0\t0\tACGTACGTAA\tFFFFFFFFFF\tNM:i:1\n"
              "chrA_read003\t0\tchrA\t301\t60\t10M\t*\t0\t0\tACGTACGTAA\tFFFFFFFFFF\tNM:i:2\n");

    std::filesystem::remove(path);
}

TEST(Axf1View, ReportsMissingReference) {
    const auto path = temp_path("alignx_axf1_view_missing_ref");
    write_axf1_or_fail(make_file(), path);

    std::ostringstream out;
    auto result = alignx::query::write_axf1_region_sam(path, "chrMissing:1-10", out);

    ASSERT_FALSE(result);
    EXPECT_NE(result.error().find("reference not found in AXF1"), std::string::npos);
    EXPECT_EQ(out.str(), "");

    std::filesystem::remove(path);
}

TEST(Axf1View, ReportsMalformedRegion) {
    const auto path = temp_path("alignx_axf1_view_bad_region");
    write_axf1_or_fail(make_file(), path);

    std::ostringstream out;
    auto result = alignx::query::write_axf1_region_sam(path, "chrToy", out);

    ASSERT_FALSE(result);
    EXPECT_NE(result.error().find("region must use ref:start-end"), std::string::npos);
    EXPECT_EQ(out.str(), "");

    std::filesystem::remove(path);
}

TEST(Axf1View, WritesNewlineTerminatedOutputWithoutTags) {
    const auto path = temp_path("alignx_axf1_view_newline");
    alignx::format::Axf1File file{
        .references = {{.name = "chrToy", .length = 1000}},
        .chunks = {{.ref_id = 0,
                    .start_pos = 100,
                    .end_pos = 110,
                    .records = {make_record("read001", 100, "10M", "")}}}};
    write_axf1_or_fail(file, path);

    std::ostringstream out;
    auto result = alignx::query::write_axf1_region_sam(path, "chrToy:101-110", out);

    EXPECT_TRUE(result) << result.error();
    EXPECT_EQ(out.str(), "read001\t0\tchrToy\t101\t60\t10M\t*\t0\t0\tACGTACGTAA\tFFFFFFFFFF\n");

    std::filesystem::remove(path);
}

TEST(Axf1View, DoesNotWritePartialOutputWhenLaterRecordIsInvalid) {
    const auto path = temp_path("alignx_axf1_view_atomic_error");
    alignx::format::Axf1File file{
        .references = {{.name = "chrToy", .length = 1000}},
        .chunks = {{.ref_id = 0,
                    .start_pos = 100,
                    .end_pos = 140,
                    .records = {make_record("read001", 100, "10M", "NM:i:0"),
                                make_record("badCigar", 120, "10Z", "NM:i:1")}}}};
    write_axf1_or_fail(file, path);

    std::ostringstream out;
    auto result = alignx::query::write_axf1_region_sam(path, "chrToy:101-130", out);

    ASSERT_FALSE(result);
    EXPECT_NE(result.error().find("invalid CIGAR operation"), std::string::npos);
    EXPECT_EQ(out.str(), "");

    std::filesystem::remove(path);
}

TEST(Axf1View, DoesNotDecodeNonOverlappingMalformedChunk) {
    const auto path = temp_path("alignx_axf1_view_lazy_non_overlap");
    alignx::format::Axf1File file{
        .references = {{.name = "chrToy", .length = 1000}},
        .chunks = {{.ref_id = 0,
                    .start_pos = 100,
                    .end_pos = 110,
                    .records = {make_record("read001", 100, "10M", "NM:i:0")}},
                   {.ref_id = 0,
                    .start_pos = 500,
                    .end_pos = 510,
                    .records = {make_record("badChunk", 500, "10M", "NM:i:1")}}}};
    write_axf1_or_fail(file, path);
    corrupt_first_column_length(path, 1);

    std::ostringstream out;
    auto result = alignx::query::write_axf1_region_sam(path, "chrToy:101-110", out);

    EXPECT_TRUE(result) << result.error();
    EXPECT_EQ(out.str(),
              "read001\t0\tchrToy\t101\t60\t10M\t*\t0\t0\tACGTACGTAA\tFFFFFFFFFF\tNM:i:0\n");

    std::filesystem::remove(path);
}

TEST(Axf1View, DoesNotDecodeOutputColumnsWhenOverlappingChunkHasNoMatchingRecords) {
    const auto path = temp_path("alignx_axf1_view_selective_no_record_hit");
    write_axf1_or_fail(make_file(), path);
    corrupt_first_column_length(path, 0);

    std::ostringstream out;
    auto result = alignx::query::write_axf1_region_sam(path, "chrToy:151-299", out);

    EXPECT_TRUE(result) << result.error();
    EXPECT_EQ(out.str(), "");

    std::filesystem::remove(path);
}

TEST(Axf1View, ReportsOverlappingMalformedChunkAtomically) {
    const auto path = temp_path("alignx_axf1_view_lazy_overlap_error");
    alignx::format::Axf1File file{
        .references = {{.name = "chrToy", .length = 1000}},
        .chunks = {{.ref_id = 0,
                    .start_pos = 100,
                    .end_pos = 110,
                    .records = {make_record("read001", 100, "10M", "NM:i:0")}},
                   {.ref_id = 0,
                    .start_pos = 500,
                    .end_pos = 510,
                    .records = {make_record("badChunk", 500, "10M", "NM:i:1")}}}};
    write_axf1_or_fail(file, path);
    corrupt_first_column_length(path, 1);

    std::ostringstream out;
    auto result = alignx::query::write_axf1_region_sam(path, "chrToy:101-510", out);

    ASSERT_FALSE(result);
    EXPECT_NE(result.error().find("string column has trailing bytes"), std::string::npos);
    EXPECT_EQ(out.str(), "");

    std::filesystem::remove(path);
}
