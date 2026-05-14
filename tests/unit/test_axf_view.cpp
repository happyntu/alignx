#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "format/axf_file.hpp"
#include "query/axf_view.hpp"

namespace {

std::filesystem::path temp_path(std::string_view label) {
    const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
           (std::string(label) + "_" + std::to_string(suffix) + ".axf");
}

std::vector<unsigned char> bytes(std::string_view text) {
    return std::vector<unsigned char>(text.begin(), text.end());
}

alignx::format::AxfFile make_axf(std::string_view payload, std::int32_t block_start = 100,
                                 std::int32_t block_end = 200) {
    return alignx::format::AxfFile{.references = {{.name = "chrToy", .length = 1000}},
                                   .blocks = {{.ref_id = 0,
                                               .start_pos = block_start,
                                               .end_pos = block_end,
                                               .record_count = 1,
                                               .payload = bytes(payload)}}};
}

void write_axf_or_fail(const alignx::format::AxfFile& axf, const std::filesystem::path& path) {
    auto write = alignx::format::write_axf_file(axf, path);
    ASSERT_TRUE(write) << write.error();
}

} // namespace

TEST(AxfView, FiltersRecordsInsideOverlappingBlock) {
    const auto path = temp_path("alignx_axf_view_filter");
    const std::string payload =
        "read001\t0\tchrToy\t101\t60\t10M\t*\t0\t0\tACGTACGTAA\tFFFFFFFFFF\n"
        "read002\t0\tchrToy\t301\t60\t10M\t*\t0\t0\tACGTACGTAA\tFFFFFFFFFF\n";
    write_axf_or_fail(make_axf(payload, 100, 310), path);

    std::ostringstream out;
    auto result = alignx::query::write_axf_region_sam(path, "chrToy:101-110", out);

    EXPECT_TRUE(result) << result.error();
    EXPECT_EQ(out.str(), "read001\t0\tchrToy\t101\t60\t10M\t*\t0\t0\tACGTACGTAA\tFFFFFFFFFF\n");

    std::filesystem::remove(path);
}

TEST(AxfView, UsesCigarReferenceConsumingOperations) {
    const auto path = temp_path("alignx_axf_view_cigar");
    const std::string payload =
        "readCigar\t0\tchrToy\t101\t60\t2S5M1I3D4N2=1X1H1P\t*\t0\t0\tACGTACGTAA\tFFFFFFFFFF\n";
    write_axf_or_fail(make_axf(payload), path);

    std::ostringstream out;
    auto result = alignx::query::write_axf_region_sam(path, "chrToy:115-115", out);

    EXPECT_TRUE(result) << result.error();
    EXPECT_EQ(out.str(), payload);

    std::filesystem::remove(path);
}

TEST(AxfView, ReturnsNoRecordsWhenBlockOverlapsButRecordDoesNot) {
    const auto path = temp_path("alignx_axf_view_no_record_hit");
    const std::string payload =
        "readFar\t0\tchrToy\t301\t60\t10M\t*\t0\t0\tACGTACGTAA\tFFFFFFFFFF\n";
    write_axf_or_fail(make_axf(payload, 100, 200), path);

    std::ostringstream out;
    auto result = alignx::query::write_axf_region_sam(path, "chrToy:151-160", out);

    EXPECT_TRUE(result) << result.error();
    EXPECT_EQ(out.str(), "");

    std::filesystem::remove(path);
}

TEST(AxfView, DoesNotParseNonOverlappingBlockPayload) {
    const auto path = temp_path("alignx_axf_view_skips_non_overlapping_payload");
    const std::string hit_payload =
        "read001\t0\tchrToy\t101\t60\t10M\t*\t0\t0\tACGTACGTAA\tFFFFFFFFFF\n";
    const std::string skipped_payload = "bad\tline\n";
    alignx::format::AxfFile file{.references = {{.name = "chrToy", .length = 1000}},
                                 .blocks = {{.ref_id = 0,
                                             .start_pos = 100,
                                             .end_pos = 110,
                                             .record_count = 1,
                                             .payload = bytes(hit_payload)},
                                            {.ref_id = 0,
                                             .start_pos = 300,
                                             .end_pos = 310,
                                             .record_count = 1,
                                             .payload = bytes(skipped_payload)}}};
    write_axf_or_fail(file, path);

    std::ostringstream out;
    auto result = alignx::query::write_axf_region_sam(path, "chrToy:101-110", out);

    EXPECT_TRUE(result) << result.error();
    EXPECT_EQ(out.str(), hit_payload);

    std::filesystem::remove(path);
}

TEST(AxfView, ReportsMissingReference) {
    const auto path = temp_path("alignx_axf_view_missing_ref");
    write_axf_or_fail(
        make_axf("read001\t0\tchrToy\t101\t60\t10M\t*\t0\t0\tACGTACGTAA\tFFFFFFFFFF\n"), path);

    std::ostringstream out;
    auto result = alignx::query::write_axf_region_sam(path, "chrMissing:1-10", out);

    ASSERT_FALSE(result);
    EXPECT_NE(result.error().find("reference not found"), std::string::npos);
    EXPECT_EQ(out.str(), "");

    std::filesystem::remove(path);
}

TEST(AxfView, ReportsMalformedRegion) {
    const auto path = temp_path("alignx_axf_view_bad_region");
    write_axf_or_fail(
        make_axf("read001\t0\tchrToy\t101\t60\t10M\t*\t0\t0\tACGTACGTAA\tFFFFFFFFFF\n"), path);

    std::ostringstream out;
    auto result = alignx::query::write_axf_region_sam(path, "chrToy", out);

    ASSERT_FALSE(result);
    EXPECT_NE(result.error().find("region must use ref:start-end"), std::string::npos);
    EXPECT_EQ(out.str(), "");

    std::filesystem::remove(path);
}

TEST(AxfView, ReportsMalformedSamPayload) {
    const auto path = temp_path("alignx_axf_view_bad_payload");
    write_axf_or_fail(make_axf("bad\tline\n"), path);

    std::ostringstream out;
    auto result = alignx::query::write_axf_region_sam(path, "chrToy:101-110", out);

    ASSERT_FALSE(result);
    EXPECT_NE(result.error().find("malformed SAM line"), std::string::npos);
    EXPECT_EQ(out.str(), "");

    std::filesystem::remove(path);
}

TEST(AxfView, DoesNotWritePartialOutputWhenLaterPayloadIsMalformed) {
    const auto path = temp_path("alignx_axf_view_atomic_error");
    const std::string payload =
        "read001\t0\tchrToy\t101\t60\t10M\t*\t0\t0\tACGTACGTAA\tFFFFFFFFFF\n"
        "bad\tline\n";
    write_axf_or_fail(make_axf(payload), path);

    std::ostringstream out;
    auto result = alignx::query::write_axf_region_sam(path, "chrToy:101-110", out);

    ASSERT_FALSE(result);
    EXPECT_NE(result.error().find("malformed SAM line"), std::string::npos);
    EXPECT_EQ(out.str(), "");

    std::filesystem::remove(path);
}

TEST(AxfView, NormalizesFinalPayloadLineToNewlineTerminatedOutput) {
    const auto path = temp_path("alignx_axf_view_final_newline");
    const std::string payload = "read001\t0\tchrToy\t101\t60\t10M\t*\t0\t0\tACGTACGTAA\tFFFFFFFFFF";
    write_axf_or_fail(make_axf(payload), path);

    std::ostringstream out;
    auto result = alignx::query::write_axf_region_sam(path, "chrToy:101-110", out);

    EXPECT_TRUE(result) << result.error();
    EXPECT_EQ(out.str(), payload + "\n");

    std::filesystem::remove(path);
}
