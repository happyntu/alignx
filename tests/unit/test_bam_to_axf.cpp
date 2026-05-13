#include <gtest/gtest.h>

#include <filesystem>
#include <string>

#include "convert/bam_to_axf.hpp"
#include "format/axf_file.hpp"

namespace {

std::filesystem::path toy_bam_path() {
    return std::filesystem::path(TEST_DATA_DIR) / "toy_alignment.sorted.bam";
}

std::string block_payload(const alignx::format::AxfBlock& block) {
    return std::string(block.payload.begin(), block.payload.end());
}

#ifdef ALIGNX_HAVE_HTSLIB

TEST(BamToAxf, ConvertsToyBamToAxfMvp) {
    const auto path = std::filesystem::temp_directory_path() / "alignx_toy_convert.axf";

    auto convert = alignx::convert::convert_bam_to_axf_mvp(toy_bam_path(), path);
    ASSERT_TRUE(convert) << convert.error();

    auto axf = alignx::format::read_axf_file(path);
    ASSERT_TRUE(axf) << axf.error();

    ASSERT_EQ(axf->references.size(), 1);
    EXPECT_EQ(axf->references[0].name, "chrToy");
    EXPECT_EQ(axf->references[0].length, 1000);

    ASSERT_EQ(axf->blocks.size(), 1);
    EXPECT_EQ(axf->blocks[0].ref_id, 0);
    EXPECT_EQ(axf->blocks[0].start_pos, 100);
    EXPECT_EQ(axf->blocks[0].end_pos, 159);
    EXPECT_EQ(axf->blocks[0].record_count, 2);

    EXPECT_EQ(block_payload(axf->blocks[0]),
              "read001\t0\tchrToy\t101\t60\t10M\t*\t0\t0\tACGTACGTAA\tFFFFFFFFFF\tNM:i:0\n"
              "read002\t16\tchrToy\t151\t50\t5M1I4M\t*\t0\t0\tTTTTACGGGA\tFFFFFFFFFF\tNM:i:1\n");

    std::filesystem::remove(path);
}

TEST(BamToAxf, ConvertsOnlyRequestedRegion) {
    const auto path = std::filesystem::temp_directory_path() / "alignx_toy_convert_region.axf";

    auto convert = alignx::convert::convert_bam_to_axf_mvp(toy_bam_path(), path, "chrToy:1-120");
    ASSERT_TRUE(convert) << convert.error();

    auto axf = alignx::format::read_axf_file(path);
    ASSERT_TRUE(axf) << axf.error();

    ASSERT_EQ(axf->references.size(), 1);
    ASSERT_EQ(axf->blocks.size(), 1);
    EXPECT_EQ(axf->blocks[0].start_pos, 100);
    EXPECT_EQ(axf->blocks[0].end_pos, 110);
    EXPECT_EQ(axf->blocks[0].record_count, 1);

    EXPECT_EQ(block_payload(axf->blocks[0]),
              "read001\t0\tchrToy\t101\t60\t10M\t*\t0\t0\tACGTACGTAA\tFFFFFFFFFF\tNM:i:0\n");

    std::filesystem::remove(path);
}

TEST(BamToAxf, ConvertsEmptyRegionToAxfWithReferencesAndNoBlocks) {
    const auto path =
        std::filesystem::temp_directory_path() / "alignx_toy_convert_empty_region.axf";

    auto convert = alignx::convert::convert_bam_to_axf_mvp(toy_bam_path(), path, "chrToy:251-260");
    ASSERT_TRUE(convert) << convert.error();

    auto axf = alignx::format::read_axf_file(path);
    ASSERT_TRUE(axf) << axf.error();

    ASSERT_EQ(axf->references.size(), 1);
    EXPECT_EQ(axf->references[0].name, "chrToy");
    EXPECT_TRUE(axf->blocks.empty());

    std::filesystem::remove(path);
}

TEST(BamToAxf, RegionFilterExcludesAdjacentToyRecords) {
    const auto path =
        std::filesystem::temp_directory_path() / "alignx_toy_convert_adjacent_empty.axf";

    auto convert = alignx::convert::convert_bam_to_axf_mvp(toy_bam_path(), path, "chrToy:111-150");
    ASSERT_TRUE(convert) << convert.error();

    auto axf = alignx::format::read_axf_file(path);
    ASSERT_TRUE(axf) << axf.error();
    ASSERT_EQ(axf->references.size(), 1);
    EXPECT_TRUE(axf->blocks.empty());

    std::filesystem::remove(path);
}

TEST(BamToAxf, RegionFilterUsesSamClosedCoordinates) {
    const auto read001_path =
        std::filesystem::temp_directory_path() / "alignx_toy_convert_region_read001_edge.axf";
    auto read001_convert =
        alignx::convert::convert_bam_to_axf_mvp(toy_bam_path(), read001_path, "chrToy:110-150");
    ASSERT_TRUE(read001_convert) << read001_convert.error();

    auto read001_axf = alignx::format::read_axf_file(read001_path);
    ASSERT_TRUE(read001_axf) << read001_axf.error();
    ASSERT_EQ(read001_axf->blocks.size(), 1);
    EXPECT_EQ(read001_axf->blocks[0].record_count, 1);
    EXPECT_EQ(block_payload(read001_axf->blocks[0]),
              "read001\t0\tchrToy\t101\t60\t10M\t*\t0\t0\tACGTACGTAA\tFFFFFFFFFF\tNM:i:0\n");

    const auto read002_path =
        std::filesystem::temp_directory_path() / "alignx_toy_convert_region_read002_edge.axf";
    auto read002_convert =
        alignx::convert::convert_bam_to_axf_mvp(toy_bam_path(), read002_path, "chrToy:111-151");
    ASSERT_TRUE(read002_convert) << read002_convert.error();

    auto read002_axf = alignx::format::read_axf_file(read002_path);
    ASSERT_TRUE(read002_axf) << read002_axf.error();
    ASSERT_EQ(read002_axf->blocks.size(), 1);
    EXPECT_EQ(read002_axf->blocks[0].record_count, 1);
    EXPECT_EQ(block_payload(read002_axf->blocks[0]),
              "read002\t16\tchrToy\t151\t50\t5M1I4M\t*\t0\t0\tTTTTACGGGA\tFFFFFFFFFF\tNM:i:1\n");

    std::filesystem::remove(read001_path);
    std::filesystem::remove(read002_path);
}

TEST(BamToAxf, RejectsMalformedRegionBeforeWritingAxf) {
    const auto path = std::filesystem::temp_directory_path() / "alignx_toy_convert_bad_region.axf";
    std::filesystem::remove(path);

    auto convert = alignx::convert::convert_bam_to_axf_mvp(toy_bam_path(), path, "chrToy");

    ASSERT_FALSE(convert);
    EXPECT_NE(convert.error().find("region must use ref:start-end"), std::string::npos);
    EXPECT_FALSE(std::filesystem::exists(path));
}

#endif

} // namespace
