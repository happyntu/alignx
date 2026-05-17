#include <gtest/gtest.h>

#include <filesystem>
#include <sstream>
#include <string>

#include "convert/bam_to_axf.hpp"
#include "format/axf1_file.hpp"
#include "format/axf_file.hpp"
#include "io/fasta_reader.hpp"
#include "query/axf1_view.hpp"

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

TEST(BamToAxf, ConvertsToyBamToAxf1Mvp) {
    const auto path = std::filesystem::temp_directory_path() / "alignx_toy_convert.axf1";

    auto convert = alignx::convert::convert_bam_to_axf1_mvp(toy_bam_path(), path);
    ASSERT_TRUE(convert) << convert.error();

    auto axf = alignx::format::read_axf1_file(path);
    ASSERT_TRUE(axf) << axf.error();

    ASSERT_EQ(axf->references.size(), 1);
    EXPECT_EQ(axf->metadata.source_path, toy_bam_path().string());
    EXPECT_EQ(axf->metadata.conversion_region, "");
    EXPECT_FALSE(axf->metadata.is_subset);
    EXPECT_EQ(axf->references[0].name, "chrToy");
    EXPECT_EQ(axf->references[0].length, 1000);

    ASSERT_EQ(axf->chunks.size(), 1);
    EXPECT_EQ(axf->chunks[0].ref_id, 0);
    EXPECT_EQ(axf->chunks[0].start_pos, 100);
    EXPECT_EQ(axf->chunks[0].end_pos, 159);

    ASSERT_EQ(axf->chunks[0].records.size(), 2);
    EXPECT_EQ(axf->chunks[0].records[0].qname, "read001");
    EXPECT_EQ(axf->chunks[0].records[0].flag, 0);
    EXPECT_EQ(axf->chunks[0].records[0].pos, 100);
    EXPECT_EQ(axf->chunks[0].records[0].mapq, 60);
    EXPECT_EQ(axf->chunks[0].records[0].cigar, "10M");
    EXPECT_EQ(axf->chunks[0].records[0].mate_reference, "*");
    EXPECT_EQ(axf->chunks[0].records[0].mate_pos, -1);
    EXPECT_EQ(axf->chunks[0].records[0].template_length, 0);
    EXPECT_EQ(axf->chunks[0].records[0].sequence, "ACGTACGTAA");
    EXPECT_EQ(axf->chunks[0].records[0].quality, "FFFFFFFFFF");
    EXPECT_EQ(axf->chunks[0].records[0].tags, "NM:i:0");

    EXPECT_EQ(axf->chunks[0].records[1].qname, "read002");
    EXPECT_EQ(axf->chunks[0].records[1].flag, 16);
    EXPECT_EQ(axf->chunks[0].records[1].pos, 150);
    EXPECT_EQ(axf->chunks[0].records[1].mapq, 50);
    EXPECT_EQ(axf->chunks[0].records[1].cigar, "5M1I4M");
    EXPECT_EQ(axf->chunks[0].records[1].mate_reference, "*");
    EXPECT_EQ(axf->chunks[0].records[1].mate_pos, -1);
    EXPECT_EQ(axf->chunks[0].records[1].template_length, 0);
    EXPECT_EQ(axf->chunks[0].records[1].sequence, "TTTTACGGGA");
    EXPECT_EQ(axf->chunks[0].records[1].quality, "FFFFFFFFFF");
    EXPECT_EQ(axf->chunks[0].records[1].tags, "NM:i:1");

    std::filesystem::remove(path);
}

TEST(BamToAxf, Axf1ViewMatchesToyBamViewOutput) {
    const auto path = std::filesystem::temp_directory_path() / "alignx_toy_convert_view.axf1";

    auto convert = alignx::convert::convert_bam_to_axf1_mvp(toy_bam_path(), path);
    ASSERT_TRUE(convert) << convert.error();

    std::ostringstream out;
    auto view = alignx::query::write_axf1_region_sam(path, "chrToy:1-250", out);

    EXPECT_TRUE(view) << view.error();
    EXPECT_EQ(out.str(),
              "read001\t0\tchrToy\t101\t60\t10M\t*\t0\t0\tACGTACGTAA\tFFFFFFFFFF\tNM:i:0\n"
              "read002\t16\tchrToy\t151\t50\t5M1I4M\t*\t0\t0\tTTTTACGGGA\tFFFFFFFFFF\tNM:i:1\n");

    std::filesystem::remove(path);
}

TEST(BamToAxf, Axf1ViewCanQuerySecondConvertedRecord) {
    const auto path =
        std::filesystem::temp_directory_path() / "alignx_toy_convert_view_second_chunk.axf1";

    auto convert = alignx::convert::convert_bam_to_axf1_mvp(toy_bam_path(), path);
    ASSERT_TRUE(convert) << convert.error();

    std::ostringstream out;
    auto view = alignx::query::write_axf1_region_sam(path, "chrToy:151-159", out);

    ASSERT_TRUE(view) << view.error();
    EXPECT_EQ(out.str(),
              "read002\t16\tchrToy\t151\t50\t5M1I4M\t*\t0\t0\tTTTTACGGGA\tFFFFFFFFFF\tNM:i:1\n");

    std::filesystem::remove(path);
}

TEST(BamToAxf, Axf1MvpSkipsToyUnmappedRecord) {
    const auto path =
        std::filesystem::temp_directory_path() / "alignx_toy_convert_mapped_only.axf1";

    auto convert = alignx::convert::convert_bam_to_axf1_mvp(toy_bam_path(), path);
    ASSERT_TRUE(convert) << convert.error();

    auto axf = alignx::format::read_axf1_file(path);
    ASSERT_TRUE(axf) << axf.error();

    std::size_t record_count = 0;
    for (const alignx::format::Axf1Chunk& chunk : axf->chunks) {
        for (const alignx::format::Axf1Record& record : chunk.records) {
            ++record_count;
            EXPECT_NE(record.qname, "read003");
            EXPECT_NE(record.flag, 4);
        }
    }
    EXPECT_EQ(record_count, 2);

    std::filesystem::remove(path);
}

TEST(BamToAxf, ConvertsOnlyRequestedRegionToAxf1) {
    const auto path = std::filesystem::temp_directory_path() / "alignx_toy_convert_region.axf1";

    auto convert = alignx::convert::convert_bam_to_axf1_mvp(toy_bam_path(), path, "chrToy:151-160");
    ASSERT_TRUE(convert) << convert.error();

    auto axf = alignx::format::read_axf1_file(path);
    ASSERT_TRUE(axf) << axf.error();
    ASSERT_EQ(axf->references.size(), 1);
    EXPECT_EQ(axf->metadata.source_path, toy_bam_path().string());
    EXPECT_EQ(axf->metadata.conversion_region, "chrToy:151-160");
    EXPECT_TRUE(axf->metadata.is_subset);
    ASSERT_EQ(axf->chunks.size(), 1);
    EXPECT_EQ(axf->chunks[0].start_pos, 150);
    EXPECT_EQ(axf->chunks[0].end_pos, 159);
    ASSERT_EQ(axf->chunks[0].records.size(), 1);
    EXPECT_EQ(axf->chunks[0].records[0].qname, "read002");

    std::ostringstream out;
    auto view = alignx::query::write_axf1_region_sam(path, "chrToy:1-250", out);
    ASSERT_TRUE(view) << view.error();
    EXPECT_EQ(out.str(),
              "read002\t16\tchrToy\t151\t50\t5M1I4M\t*\t0\t0\tTTTTACGGGA\tFFFFFFFFFF\tNM:i:1\n");

    std::filesystem::remove(path);
}

TEST(BamToAxf, ConvertsEmptyRegionToAxf1WithReferencesAndNoChunks) {
    const auto path =
        std::filesystem::temp_directory_path() / "alignx_toy_convert_empty_region.axf1";

    auto convert = alignx::convert::convert_bam_to_axf1_mvp(toy_bam_path(), path, "chrToy:251-260");
    ASSERT_TRUE(convert) << convert.error();

    auto axf = alignx::format::read_axf1_file(path);
    ASSERT_TRUE(axf) << axf.error();

    ASSERT_EQ(axf->references.size(), 1);
    EXPECT_EQ(axf->references[0].name, "chrToy");
    EXPECT_TRUE(axf->chunks.empty());

    std::filesystem::remove(path);
}

TEST(BamToAxf, RejectsMalformedRegionBeforeWritingAxf1) {
    const auto path = std::filesystem::temp_directory_path() / "alignx_toy_convert_bad_region.axf1";
    std::filesystem::remove(path);

    auto convert = alignx::convert::convert_bam_to_axf1_mvp(toy_bam_path(), path, "chrToy");

    ASSERT_FALSE(convert);
    EXPECT_NE(convert.error().find("region must use ref:start-end"), std::string::npos);
    EXPECT_FALSE(std::filesystem::exists(path));
}

TEST(BamToAxf, ConvertWithReference_StoresChecksums) {
    const auto path =
        std::filesystem::temp_directory_path() / "alignx_toy_convert_ref_checksums.axf1";
    const auto ref_path = std::filesystem::path(TEST_DATA_DIR) / "toy_ref.fa";
    alignx::format::Axf1WriteOptions options;
    options.reference_fasta = ref_path;

    auto convert = alignx::convert::convert_bam_to_axf1_mvp(toy_bam_path(), path,
                                                              std::nullopt, options);
    ASSERT_TRUE(convert) << convert.error();

    auto read = alignx::format::read_axf1_file(path);
    ASSERT_TRUE(read) << read.error();

    bool found_sha256 = false;
    bool found_ref_path = false;
    for (const auto& ext : read->metadata.extensions) {
        if (ext.key_id == alignx::format::extension_key::kRefContigSha256) {
            found_sha256 = true;
            auto parsed = alignx::format::parse_ref_contig_sha256_entry(ext);
            ASSERT_GE(parsed.size(), 1u);
            EXPECT_EQ(parsed[0].first, 0u);
            bool all_zero = true;
            for (auto b : parsed[0].second) {
                if (b != 0) { all_zero = false; break; }
            }
            EXPECT_FALSE(all_zero);
        }
        if (ext.key_id == alignx::format::extension_key::kEncodeReferencePath) {
            found_ref_path = true;
        }
    }
    EXPECT_TRUE(found_sha256);
    EXPECT_TRUE(found_ref_path);

    std::filesystem::remove(path);
}

TEST(BamToAxf, ConvertWithReference_SamParity) {
    const auto axf1_path =
        std::filesystem::temp_directory_path() / "alignx_toy_convert_ref_parity.axf1";
    const auto ref_path = std::filesystem::path(TEST_DATA_DIR) / "toy_ref.fa";
    alignx::format::Axf1WriteOptions options;
    options.reference_fasta = ref_path;

    auto convert = alignx::convert::convert_bam_to_axf1_mvp(toy_bam_path(), axf1_path,
                                                              std::nullopt, options);
    ASSERT_TRUE(convert) << convert.error();

    auto read = alignx::format::read_axf1_file(axf1_path);
    ASSERT_TRUE(read) << read.error();

    // View the AXF1 file and compare with expected SAM output
    std::ostringstream sam_out;
    auto view_result = alignx::query::write_axf1_region_sam(axf1_path, "chrToy:1-1000", sam_out);
    ASSERT_TRUE(view_result) << view_result.error();

    const std::string expected_sam =
        "read001\t0\tchrToy\t101\t60\t10M\t*\t0\t0\tACGTACGTAA\tFFFFFFFFFF\tNM:i:0\n"
        "read002\t16\tchrToy\t151\t50\t5M1I4M\t*\t0\t0\tTTTTACGGGA\tFFFFFFFFFF\tNM:i:1\n";
    EXPECT_EQ(sam_out.str(), expected_sam);

    std::filesystem::remove(axf1_path);
}

TEST(BamToAxf, ConvertWithReference_UsesRefDelta) {
    // Build a file with many high-identity records to guarantee ref-delta wins
    const auto axf1_path =
        std::filesystem::temp_directory_path() / "alignx_toy_convert_ref_codec.axf1";
    const auto ref_path = std::filesystem::path(TEST_DATA_DIR) / "toy_ref.fa";

    // Write an AXF1 file with synthetic high-identity records matching toy_ref.fa
    // toy_ref.fa chrToy is 1000bp; we need to know the exact bases.
    // Read the reference to construct matching reads
    auto fasta = alignx::io::FastaReader::open(ref_path);
    ASSERT_TRUE(fasta) << fasta.error();
    auto ref_seq_result = fasta->fetch_contig("chrToy");
    ASSERT_TRUE(ref_seq_result) << ref_seq_result.error();
    auto ref_seq = *ref_seq_result;
    for (char& c : ref_seq) {
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 32);
    }

    alignx::format::Axf1File file;
    file.references.push_back({.name = "chrToy", .length = 1000});
    alignx::format::Axf1Chunk chunk;
    chunk.ref_id = 0;
    chunk.start_pos = 100;
    chunk.end_pos = 500;
    // Create 50 perfect-match reads of 100bp each
    for (int i = 0; i < 50; ++i) {
        int pos = 100 + i * 8;
        std::string seq = ref_seq.substr(static_cast<std::size_t>(pos), 100);
        chunk.records.push_back({.qname = "read" + std::to_string(i),
                                 .flag = 0,
                                 .pos = static_cast<std::int32_t>(pos),
                                 .mapq = 60,
                                 .cigar = "100M",
                                 .mate_reference = "*",
                                 .mate_pos = -1,
                                 .template_length = 0,
                                 .sequence = seq,
                                 .quality = std::string(100, 'F'),
                                 .tags = "NM:i:0"});
    }
    file.chunks.push_back(std::move(chunk));

    alignx::format::Axf1WriteOptions options;
    options.reference_fasta = ref_path;
    auto write = alignx::format::write_axf1_file(file, axf1_path, options);
    ASSERT_TRUE(write) << write.error();

    auto reader = alignx::format::Axf1FileReader::open(axf1_path);
    ASSERT_TRUE(reader) << reader.error();
    ASSERT_GE(reader->index().chunks.size(), 1u);

    auto raw = reader->read_chunk_raw(reader->index().chunks[0]);
    ASSERT_TRUE(raw) << raw.error();

    ASSERT_GE(raw->size(), 18u);
    const auto* ptr = raw->data();
    const std::uint16_t column_count =
        static_cast<std::uint16_t>(ptr[16]) | (static_cast<std::uint16_t>(ptr[17]) << 8);
    ASSERT_GE(raw->size(), 18u + column_count * 20u);

    bool found_seq_ref_delta = false;
    for (std::uint16_t c = 0; c < column_count; ++c) {
        const auto* entry_ptr = ptr + 18 + c * 20;
        auto col_id = static_cast<std::uint16_t>(entry_ptr[0]) |
                      (static_cast<std::uint16_t>(entry_ptr[1]) << 8);
        auto codec_id = static_cast<std::uint16_t>(entry_ptr[2]) |
                        (static_cast<std::uint16_t>(entry_ptr[3]) << 8);
        if (col_id == static_cast<std::uint16_t>(alignx::format::Axf1ColumnId::sequence) &&
            codec_id == static_cast<std::uint16_t>(alignx::format::Axf1CodecId::seq_ref_delta)) {
            found_seq_ref_delta = true;
        }
    }
    EXPECT_TRUE(found_seq_ref_delta) << "Expected seq_ref_delta codec for high-identity reads";

    std::filesystem::remove(axf1_path);
}

TEST(BamToAxf, ConvertWithReference_StoresReferencePath) {
    const auto path =
        std::filesystem::temp_directory_path() / "alignx_toy_convert_ref_path.axf1";
    const auto ref_path = std::filesystem::path(TEST_DATA_DIR) / "toy_ref.fa";
    alignx::format::Axf1WriteOptions options;
    options.reference_fasta = ref_path;

    auto convert = alignx::convert::convert_bam_to_axf1_mvp(toy_bam_path(), path,
                                                              std::nullopt, options);
    ASSERT_TRUE(convert) << convert.error();

    auto read = alignx::format::read_axf1_file(path);
    ASSERT_TRUE(read) << read.error();

    bool found = false;
    for (const auto& ext : read->metadata.extensions) {
        if (ext.key_id == alignx::format::extension_key::kEncodeReferencePath) {
            found = true;
            std::string stored(ext.value.begin(), ext.value.end());
            EXPECT_EQ(stored, ref_path.string());
        }
    }
    EXPECT_TRUE(found);

    std::filesystem::remove(path);
}

#endif

} // namespace
