#include <gtest/gtest.h>

#include <filesystem>
#include <string>

#include "convert/bam_to_axf.hpp"
#include "format/axf_file.hpp"

namespace {

std::filesystem::path toy_bam_path() {
    return std::filesystem::path(TEST_DATA_DIR) / "toy_alignment.sorted.bam";
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

    const std::string payload(axf->blocks[0].payload.begin(), axf->blocks[0].payload.end());
    EXPECT_EQ(payload,
              "read001\t0\tchrToy\t101\t60\t10M\t*\t0\t0\tACGTACGTAA\tFFFFFFFFFF\tNM:i:0\n"
              "read002\t16\tchrToy\t151\t50\t5M1I4M\t*\t0\t0\tTTTTACGGGA\tFFFFFFFFFF\tNM:i:1\n");

    std::filesystem::remove(path);
}

#endif

} // namespace
