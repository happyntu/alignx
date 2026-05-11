#include <gtest/gtest.h>

#include <filesystem>
#include <utility>

#include "index/bam_index_projection.hpp"

namespace {

std::filesystem::path toy_bai_path() {
    return std::filesystem::path(TEST_DATA_DIR) / "toy_alignment.sorted.bam.bai";
}

std::filesystem::path toy_csi_path() {
    return std::filesystem::path(TEST_DATA_DIR) / "toy_alignment.sorted.bam.csi";
}

TEST(BamIndexProjection, ComputesBinGenomicIntervals) {
    auto leaf = alignx::index::bin_to_genomic_interval(4681, 14, 5);
    ASSERT_TRUE(leaf) << leaf.error();
    EXPECT_EQ(leaf->start, 0);
    EXPECT_EQ(leaf->end, 16'384);

    auto next_leaf = alignx::index::bin_to_genomic_interval(4682, 14, 5);
    ASSERT_TRUE(next_leaf) << next_leaf.error();
    EXPECT_EQ(next_leaf->start, 16'384);
    EXPECT_EQ(next_leaf->end, 32'768);

    auto root = alignx::index::bin_to_genomic_interval(0, 14, 5);
    ASSERT_TRUE(root) << root.error();
    EXPECT_EQ(root->start, 0);
    EXPECT_EQ(root->end, 536'870'912);
}

TEST(BamIndexProjection, ProjectsToyBaiIntoAxfIndex) {
    auto bai = alignx::index::read_bai_index(toy_bai_path());
    ASSERT_TRUE(bai) << bai.error();

    auto axf = alignx::index::project_bai_to_axf_index(*bai);
    ASSERT_TRUE(axf) << axf.error();

    ASSERT_EQ(axf->reference_count(), 1);
    ASSERT_EQ(axf->intervals(0).size(), 1);
    const auto& interval = axf->intervals(0).front();
    EXPECT_EQ(interval.start, 0);
    EXPECT_EQ(interval.end, 16'384);
    EXPECT_EQ(interval.chunk_offset, bai->references.front().bins.front().chunks.front().begin);
    EXPECT_EQ(interval.column_index_offset,
              bai->references.front().bins.front().chunks.front().end);

    auto hits = axf->query(0, 100, 110);
    ASSERT_TRUE(hits) << hits.error();
    ASSERT_EQ(hits->size(), 1);
    EXPECT_EQ(hits->front().chunk_offset, interval.chunk_offset);

    auto misses = axf->query(0, 16'384, 32'768);
    ASSERT_TRUE(misses) << misses.error();
    EXPECT_TRUE(misses->empty());
}

TEST(BamIndexProjection, ProjectsToyCsiIntoAxfIndex) {
    auto csi = alignx::index::read_csi_index(toy_csi_path());
    ASSERT_TRUE(csi) << csi.error();

    auto axf = alignx::index::project_csi_to_axf_index(*csi);
    ASSERT_TRUE(axf) << axf.error();

    ASSERT_EQ(axf->reference_count(), 1);
    ASSERT_EQ(axf->intervals(0).size(), 1);
    const auto& interval = axf->intervals(0).front();
    EXPECT_EQ(interval.start, 0);
    EXPECT_EQ(interval.end, 16'384);
    EXPECT_EQ(interval.chunk_offset, csi->references.front().bins.front().chunks.front().begin);
    EXPECT_EQ(interval.column_index_offset,
              csi->references.front().bins.front().chunks.front().end);
}

TEST(BamIndexProjection, SkipsBaiMetadataBin) {
    alignx::index::BaiIndex bai;
    alignx::index::BaiReference reference;
    reference.bins.push_back(
        alignx::index::BaiBin{.id = 37'450, .chunks = {{.begin = 1, .end = 2}}});
    bai.references.push_back(std::move(reference));

    auto axf = alignx::index::project_bai_to_axf_index(bai);
    ASSERT_TRUE(axf) << axf.error();
    ASSERT_EQ(axf->reference_count(), 1);
    EXPECT_TRUE(axf->intervals(0).empty());
}

TEST(BamIndexProjection, RejectsInvalidChunkRange) {
    alignx::index::BaiIndex bai;
    alignx::index::BaiReference reference;
    reference.bins.push_back(
        alignx::index::BaiBin{.id = 4681, .chunks = {{.begin = 10, .end = 10}}});
    bai.references.push_back(std::move(reference));

    auto axf = alignx::index::project_bai_to_axf_index(bai);

    EXPECT_FALSE(axf);
    EXPECT_NE(axf.error().find("begin < end"), std::string::npos);
}

TEST(BamIndexProjection, RejectsUnsupportedCsiCoordinateRange) {
    alignx::index::CsiIndex csi;
    csi.min_shift = 31;
    csi.depth = 1;
    alignx::index::CsiReference reference;
    reference.bins.push_back(alignx::index::CsiBin{.id = 0, .chunks = {{.begin = 1, .end = 2}}});
    csi.references.push_back(std::move(reference));

    auto axf = alignx::index::project_csi_to_axf_index(csi);

    EXPECT_FALSE(axf);
    EXPECT_NE(axf.error().find("coordinate range"), std::string::npos);
}

} // namespace
