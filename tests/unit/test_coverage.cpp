#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "analysis/coverage.hpp"
#include "format/axf1_file.hpp"
#include "query/axf1_coverage.hpp"

namespace {

std::filesystem::path temp_path(std::string_view label) {
    const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
           (std::string(label) + "_" + std::to_string(suffix) + ".axf1");
}

alignx::format::Axf1Record make_record(std::string qname, std::int32_t pos,
                                       std::string cigar = "10M", std::uint16_t flag = 0,
                                       std::uint8_t mapq = 60) {
    return alignx::format::Axf1Record{.qname = std::move(qname),
                                      .flag = flag,
                                      .pos = pos,
                                      .mapq = mapq,
                                      .cigar = std::move(cigar),
                                      .mate_reference = "*",
                                      .mate_pos = -1,
                                      .template_length = 0,
                                      .sequence = "ACGTACGTAA",
                                      .quality = "FFFFFFFFFF",
                                      .tags = ""};
}

void write_axf1_or_fail(const alignx::format::Axf1File& file, const std::filesystem::path& path) {
    auto write = alignx::format::write_axf1_file(file, path);
    ASSERT_TRUE(write) << write.error();
}

} // namespace

TEST(Coverage, AddCoverageSimpleMatch) {
    alignx::analysis::CoverageResult result;
    result.start = 100;
    result.end = 120;
    result.depth.resize(20, 0);

    auto add = alignx::analysis::add_coverage(result, 100, "10M");
    ASSERT_TRUE(add) << add.error();

    for (int i = 0; i < 10; ++i) {
        EXPECT_EQ(result.depth[static_cast<std::size_t>(i)], 1) << "position " << (100 + i);
    }
    for (int i = 10; i < 20; ++i) {
        EXPECT_EQ(result.depth[static_cast<std::size_t>(i)], 0) << "position " << (100 + i);
    }
}

TEST(Coverage, AddCoveragePartialOverlap) {
    alignx::analysis::CoverageResult result;
    result.start = 105;
    result.end = 115;
    result.depth.resize(10, 0);

    auto add = alignx::analysis::add_coverage(result, 100, "20M");
    ASSERT_TRUE(add) << add.error();

    for (std::size_t i = 0; i < 10; ++i) {
        EXPECT_EQ(result.depth[i], 1);
    }
}

TEST(Coverage, AddCoverageDeletionSkipsPositions) {
    alignx::analysis::CoverageResult result;
    result.start = 100;
    result.end = 120;
    result.depth.resize(20, 0);

    // 5M3D5M: 5 bases match, 3 bases deleted (no coverage), 5 bases match
    auto add = alignx::analysis::add_coverage(result, 100, "5M3D5M");
    ASSERT_TRUE(add) << add.error();

    for (int i = 0; i < 5; ++i) {
        EXPECT_EQ(result.depth[static_cast<std::size_t>(i)], 1) << "pos " << (100 + i);
    }
    for (int i = 5; i < 8; ++i) {
        EXPECT_EQ(result.depth[static_cast<std::size_t>(i)], 0) << "pos " << (100 + i) << " (deletion)";
    }
    for (int i = 8; i < 13; ++i) {
        EXPECT_EQ(result.depth[static_cast<std::size_t>(i)], 1) << "pos " << (100 + i);
    }
}

TEST(Coverage, AddCoverageInsertionDoesNotConsumeReference) {
    alignx::analysis::CoverageResult result;
    result.start = 100;
    result.end = 115;
    result.depth.resize(15, 0);

    // 5M2I5M: ref span = 10 (5+5), insertion doesn't consume ref
    auto add = alignx::analysis::add_coverage(result, 100, "5M2I5M");
    ASSERT_TRUE(add) << add.error();

    for (int i = 0; i < 10; ++i) {
        EXPECT_EQ(result.depth[static_cast<std::size_t>(i)], 1) << "pos " << (100 + i);
    }
    for (int i = 10; i < 15; ++i) {
        EXPECT_EQ(result.depth[static_cast<std::size_t>(i)], 0) << "pos " << (100 + i);
    }
}

TEST(Coverage, AddCoverageStarCigar) {
    alignx::analysis::CoverageResult result;
    result.start = 100;
    result.end = 110;
    result.depth.resize(10, 0);

    auto add = alignx::analysis::add_coverage(result, 100, "*");
    ASSERT_TRUE(add) << add.error();

    for (std::size_t i = 0; i < 10; ++i) {
        EXPECT_EQ(result.depth[i], 0);
    }
}

TEST(Coverage, MultipleReadsStackCoverage) {
    alignx::analysis::CoverageResult result;
    result.start = 100;
    result.end = 115;
    result.depth.resize(15, 0);

    auto add1 = alignx::analysis::add_coverage(result, 100, "10M");
    ASSERT_TRUE(add1) << add1.error();
    auto add2 = alignx::analysis::add_coverage(result, 105, "10M");
    ASSERT_TRUE(add2) << add2.error();

    for (int i = 0; i < 5; ++i) {
        EXPECT_EQ(result.depth[static_cast<std::size_t>(i)], 1) << "pos " << (100 + i);
    }
    for (int i = 5; i < 10; ++i) {
        EXPECT_EQ(result.depth[static_cast<std::size_t>(i)], 2) << "pos " << (100 + i);
    }
    for (int i = 10; i < 15; ++i) {
        EXPECT_EQ(result.depth[static_cast<std::size_t>(i)], 1) << "pos " << (100 + i);
    }
}

TEST(Axf1Coverage, ComputesCoverageFromAxf1File) {
    const auto path = temp_path("alignx_axf1_coverage");
    alignx::format::Axf1File file{
        .references = {{.name = "chrToy", .length = 1000}},
        .chunks = {{.ref_id = 0,
                    .start_pos = 100,
                    .end_pos = 210,
                    .records = {make_record("read001", 100, "10M"),
                                make_record("read002", 105, "10M"),
                                make_record("read003", 200, "10M")}}}};
    write_axf1_or_fail(file, path);

    auto result = alignx::query::compute_axf1_coverage(path, "chrToy:101-116");

    ASSERT_TRUE(result) << result.error();
    EXPECT_EQ(result->records_counted, 2);
    EXPECT_EQ(result->depth.size(), 16);
    EXPECT_EQ(result->depth[0], 1);   // pos 100: read001 only
    EXPECT_EQ(result->depth[5], 2);   // pos 105: read001 + read002
    EXPECT_EQ(result->depth[10], 1);  // pos 110: read002 only
    EXPECT_EQ(result->depth[14], 1);  // pos 114: read002 last base
    EXPECT_EQ(result->depth[15], 0);  // pos 115: no reads

    std::filesystem::remove(path);
}

TEST(Axf1Coverage, ProfileShowsNoOutputDecode) {
    const auto path = temp_path("alignx_axf1_coverage_profile");
    alignx::format::Axf1File file{
        .references = {{.name = "chrToy", .length = 1000}},
        .chunks = {{.ref_id = 0,
                    .start_pos = 100,
                    .end_pos = 110,
                    .records = {make_record("read001", 100, "10M")}}}};
    write_axf1_or_fail(file, path);

    alignx::query::Axf1CoverageProfile profile;
    auto result = alignx::query::compute_axf1_coverage_profiled(path, "chrToy:101-110", profile);

    ASSERT_TRUE(result) << result.error();
    EXPECT_EQ(profile.chunks_selected, 1);
    EXPECT_EQ(profile.chunks_with_matches, 1);
    EXPECT_EQ(profile.records_scanned, 1);
    EXPECT_EQ(profile.records_matched, 1);
    EXPECT_GT(profile.selective_bytes_read, 0);
    EXPECT_GT(profile.selective_payload_bytes, 0);

    std::filesystem::remove(path);
}

TEST(Axf1Coverage, ReportsMissingReference) {
    const auto path = temp_path("alignx_axf1_coverage_missing_ref");
    alignx::format::Axf1File file{
        .references = {{.name = "chrToy", .length = 1000}},
        .chunks = {{.ref_id = 0,
                    .start_pos = 100,
                    .end_pos = 110,
                    .records = {make_record("read001", 100, "10M")}}}};
    write_axf1_or_fail(file, path);

    auto result = alignx::query::compute_axf1_coverage(path, "chrMissing:1-10");

    ASSERT_FALSE(result);
    EXPECT_NE(result.error().find("reference not found in AXF1"), std::string::npos);

    std::filesystem::remove(path);
}

TEST(Axf1Coverage, ReturnsZeroCoverageForNoHitRegion) {
    const auto path = temp_path("alignx_axf1_coverage_no_hit");
    alignx::format::Axf1File file{
        .references = {{.name = "chrToy", .length = 1000}},
        .chunks = {{.ref_id = 0,
                    .start_pos = 100,
                    .end_pos = 110,
                    .records = {make_record("read001", 100, "10M")}}}};
    write_axf1_or_fail(file, path);

    auto result = alignx::query::compute_axf1_coverage(path, "chrToy:501-510");

    ASSERT_TRUE(result) << result.error();
    EXPECT_EQ(result->records_counted, 0);
    for (auto depth : result->depth) {
        EXPECT_EQ(depth, 0);
    }

    std::filesystem::remove(path);
}

TEST(Axf1Coverage, MultiChunkCoverage) {
    const auto path = temp_path("alignx_axf1_coverage_multi_chunk");
    alignx::format::Axf1File file{
        .references = {{.name = "chrToy", .length = 1000}},
        .chunks = {{.ref_id = 0,
                    .start_pos = 100,
                    .end_pos = 110,
                    .records = {make_record("read001", 100, "10M")}},
                   {.ref_id = 0,
                    .start_pos = 105,
                    .end_pos = 115,
                    .records = {make_record("read002", 105, "10M")}}}};
    write_axf1_or_fail(file, path);

    auto result = alignx::query::compute_axf1_coverage(path, "chrToy:101-116");

    ASSERT_TRUE(result) << result.error();
    EXPECT_EQ(result->records_counted, 2);
    EXPECT_EQ(result->depth[0], 1);
    EXPECT_EQ(result->depth[5], 2);
    EXPECT_EQ(result->depth[10], 1);
    EXPECT_EQ(result->depth[14], 1);
    EXPECT_EQ(result->depth[15], 0);

    std::filesystem::remove(path);
}

TEST(Axf1Coverage, FlagExcludeFiltersRecords) {
    const auto path = temp_path("alignx_axf1_coverage_flag_filter");
    alignx::format::Axf1File file{
        .references = {{.name = "chrToy", .length = 1000}},
        .chunks = {{.ref_id = 0,
                    .start_pos = 100,
                    .end_pos = 210,
                    .records = {make_record("read001", 100, "10M", 0, 60),
                                make_record("read002", 105, "10M", 16, 50)}}}};
    write_axf1_or_fail(file, path);

    alignx::query::RecordFilter filter{.flag_exclude = 16};
    auto result = alignx::query::compute_axf1_coverage(path, "chrToy:101-116", filter);

    ASSERT_TRUE(result) << result.error();
    EXPECT_EQ(result->records_counted, 1);
    EXPECT_EQ(result->depth[0], 1);
    EXPECT_EQ(result->depth[5], 1);
    EXPECT_EQ(result->depth[10], 0);

    std::filesystem::remove(path);
}

TEST(Axf1Coverage, MinMapqFiltersRecords) {
    const auto path = temp_path("alignx_axf1_coverage_mapq_filter");
    alignx::format::Axf1File file{
        .references = {{.name = "chrToy", .length = 1000}},
        .chunks = {{.ref_id = 0,
                    .start_pos = 100,
                    .end_pos = 210,
                    .records = {make_record("read001", 100, "10M", 0, 60),
                                make_record("read002", 105, "10M", 0, 20)}}}};
    write_axf1_or_fail(file, path);

    alignx::query::RecordFilter filter{.min_mapq = 30};
    auto result = alignx::query::compute_axf1_coverage(path, "chrToy:101-116", filter);

    ASSERT_TRUE(result) << result.error();
    EXPECT_EQ(result->records_counted, 1);
    EXPECT_EQ(result->depth[0], 1);
    EXPECT_EQ(result->depth[5], 1);
    EXPECT_EQ(result->depth[10], 0);

    std::filesystem::remove(path);
}

TEST(Axf1Coverage, FilterProfileShowsFilteredCount) {
    const auto path = temp_path("alignx_axf1_coverage_filter_profile");
    alignx::format::Axf1File file{
        .references = {{.name = "chrToy", .length = 1000}},
        .chunks = {{.ref_id = 0,
                    .start_pos = 100,
                    .end_pos = 210,
                    .records = {make_record("read001", 100, "10M", 0, 60),
                                make_record("read002", 105, "10M", 16, 50)}}}};
    write_axf1_or_fail(file, path);

    alignx::query::Axf1CoverageProfile profile;
    alignx::query::RecordFilter filter{.flag_exclude = 16};
    auto result =
        alignx::query::compute_axf1_coverage_profiled(path, "chrToy:101-116", profile, filter);

    ASSERT_TRUE(result) << result.error();
    EXPECT_EQ(profile.records_scanned, 2);
    EXPECT_EQ(profile.records_matched, 1);
    EXPECT_EQ(profile.records_filtered, 1);

    std::filesystem::remove(path);
}
