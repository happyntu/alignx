#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include "convert/axf_to_bam.hpp"
#include "format/axf1_file.hpp"

#ifdef ALIGNX_HAVE_HTSLIB
#include "convert/bam_to_axf.hpp"
#include "io/bam_reader.hpp"
#endif

namespace {

std::filesystem::path temp_dir(std::string_view label) {
    const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    auto dir = std::filesystem::temp_directory_path() /
               (std::string(label) + "_" + std::to_string(suffix));
    std::filesystem::create_directories(dir);
    return dir;
}

std::filesystem::path toy_bam_path() {
    return std::filesystem::path(TEST_DATA_DIR) / "toy_alignment.sorted.bam";
}

std::filesystem::path toy_cram_path() {
    return std::filesystem::path(TEST_DATA_DIR) / "toy_alignment.sorted.cram";
}

alignx::format::Axf1Record make_record(std::string qname, std::int32_t pos,
                                        std::string cigar = "10M", std::uint16_t flag = 0,
                                        std::uint8_t mapq = 60, std::string seq = "ACGTACGTAA",
                                        std::string qual = "FFFFFFFFFF",
                                        std::string tags = "NM:i:0") {
    return alignx::format::Axf1Record{.qname = std::move(qname),
                                       .flag = flag,
                                       .pos = pos,
                                       .mapq = mapq,
                                       .cigar = std::move(cigar),
                                       .mate_reference = "*",
                                       .mate_pos = -1,
                                       .template_length = 0,
                                       .sequence = std::move(seq),
                                       .quality = std::move(qual),
                                       .tags = std::move(tags)};
}

#ifdef ALIGNX_HAVE_HTSLIB

TEST(ExportAxf1ToBam, ExportToyAxf1ToBam) {
    const auto dir = temp_dir("alignx_export_toy");
    const auto axf1_path = dir / "toy.axf1";
    const auto bam_path = dir / "exported.bam";

    alignx::format::Axf1File file{
        .references = {{.name = "chrToy", .length = 1000}},
        .chunks = {{.ref_id = 0,
                    .start_pos = 100,
                    .end_pos = 110,
                    .records = {make_record("read001", 100, "10M", 0, 60)}}}};

    auto write_result = alignx::format::write_axf1_file(file, axf1_path);
    ASSERT_TRUE(write_result) << write_result.error();

    auto export_result = alignx::convert::convert_axf1_to_bam(axf1_path, bam_path);
    ASSERT_TRUE(export_result) << export_result.error();
    ASSERT_TRUE(std::filesystem::is_regular_file(bam_path));

    auto reader = alignx::io::BamReader::open(bam_path);
    ASSERT_TRUE(reader) << reader.error();

    auto refs = reader->references();
    ASSERT_TRUE(refs) << refs.error();
    ASSERT_EQ(refs->size(), 1);
    EXPECT_EQ(refs->at(0).name, "chrToy");
    EXPECT_EQ(refs->at(0).length, 1000);

    auto rec = reader->next_record();
    ASSERT_TRUE(rec) << rec.error();
    ASSERT_TRUE(rec->has_value());
    EXPECT_EQ(rec->value().qname, "read001");
    EXPECT_EQ(rec->value().position, 100);
    EXPECT_EQ(rec->value().flag, 0);
    EXPECT_EQ(rec->value().mapq, 60);

    auto rec2 = reader->next_record();
    ASSERT_TRUE(rec2) << rec2.error();
    EXPECT_FALSE(rec2->has_value());

    std::filesystem::remove_all(dir);
}

TEST(ExportAxf1ToBam, ExportEmptyAxf1ProducesValidBam) {
    const auto dir = temp_dir("alignx_export_empty");
    const auto axf1_path = dir / "empty.axf1";
    const auto bam_path = dir / "empty.bam";

    alignx::format::Axf1File file{
        .references = {{.name = "chrToy", .length = 1000}},
        .chunks = {}};

    auto write_result = alignx::format::write_axf1_file(file, axf1_path);
    ASSERT_TRUE(write_result) << write_result.error();

    auto export_result = alignx::convert::convert_axf1_to_bam(axf1_path, bam_path);
    ASSERT_TRUE(export_result) << export_result.error();
    ASSERT_TRUE(std::filesystem::is_regular_file(bam_path));

    auto reader = alignx::io::BamReader::open(bam_path);
    ASSERT_TRUE(reader) << reader.error();

    auto refs = reader->references();
    ASSERT_TRUE(refs) << refs.error();
    ASSERT_EQ(refs->size(), 1);
    EXPECT_EQ(refs->at(0).name, "chrToy");

    std::filesystem::remove_all(dir);
}

TEST(ExportAxf1ToBam, ExportToyBamRoundtripSamDiff) {
    const auto dir = temp_dir("alignx_export_roundtrip");
    const auto axf1_path = dir / "toy.axf1";
    const auto exported_bam_path = dir / "exported.bam";

    auto convert_result = alignx::convert::convert_bam_to_axf1_mvp(toy_bam_path(), axf1_path);
    ASSERT_TRUE(convert_result) << convert_result.error();

    auto export_result = alignx::convert::convert_axf1_to_bam(axf1_path, exported_bam_path);
    ASSERT_TRUE(export_result) << export_result.error();

    // Collect SAM lines from original BAM (sequential, mapped records only)
    auto orig_reader = alignx::io::BamReader::open(toy_bam_path());
    ASSERT_TRUE(orig_reader) << orig_reader.error();

    std::vector<std::string> original_lines;
    for (;;) {
        auto line = orig_reader->next_sam_line();
        ASSERT_TRUE(line) << line.error();
        if (!line->has_value()) break;
        original_lines.push_back(line->value());
    }
    // AXF1 only stores mapped records, so filter out unmapped from original
    std::erase_if(original_lines, [](const std::string& line) {
        auto tab1 = line.find('\t');
        if (tab1 == std::string::npos) return false;
        auto tab2 = line.find('\t', tab1 + 1);
        if (tab2 == std::string::npos) return false;
        auto flag_str = line.substr(tab1 + 1, tab2 - tab1 - 1);
        auto flag = static_cast<std::uint16_t>(std::stoi(flag_str));
        return (flag & 0x4U) != 0;
    });

    // Collect SAM lines from exported BAM (sequential)
    auto exported_reader = alignx::io::BamReader::open(exported_bam_path);
    ASSERT_TRUE(exported_reader) << exported_reader.error();

    std::vector<std::string> exported_lines;
    for (;;) {
        auto line = exported_reader->next_sam_line();
        ASSERT_TRUE(line) << line.error();
        if (!line->has_value()) break;
        exported_lines.push_back(line->value());
    }

    ASSERT_EQ(original_lines.size(), exported_lines.size())
        << "Record count mismatch: original " << original_lines.size() << " vs exported "
        << exported_lines.size();

    for (std::size_t i = 0; i < original_lines.size(); ++i) {
        EXPECT_EQ(original_lines[i], exported_lines[i]) << "SAM line " << i << " differs";
    }

    std::filesystem::remove_all(dir);
}

TEST(ExportAxf1ToBam, ExportCramRoundtripSamDiff) {
    const auto dir = temp_dir("alignx_export_cram_roundtrip");
    const auto axf1_path = dir / "cram_origin.axf1";
    const auto exported_bam_path = dir / "exported.bam";

    auto convert_result = alignx::convert::convert_bam_to_axf1_mvp(toy_cram_path(), axf1_path);
    ASSERT_TRUE(convert_result) << convert_result.error();

    auto export_result = alignx::convert::convert_axf1_to_bam(axf1_path, exported_bam_path);
    ASSERT_TRUE(export_result) << export_result.error();

    // Collect SAM lines from CRAM (mapped records only)
    auto cram_reader = alignx::io::BamReader::open(toy_cram_path());
    ASSERT_TRUE(cram_reader) << cram_reader.error();

    std::vector<std::string> cram_lines;
    for (;;) {
        auto line = cram_reader->next_sam_line();
        ASSERT_TRUE(line) << line.error();
        if (!line->has_value()) break;
        cram_lines.push_back(line->value());
    }
    std::erase_if(cram_lines, [](const std::string& line) {
        auto tab1 = line.find('\t');
        if (tab1 == std::string::npos) return false;
        auto tab2 = line.find('\t', tab1 + 1);
        if (tab2 == std::string::npos) return false;
        auto flag_str = line.substr(tab1 + 1, tab2 - tab1 - 1);
        auto flag = static_cast<std::uint16_t>(std::stoi(flag_str));
        return (flag & 0x4U) != 0;
    });

    // Collect SAM lines from exported BAM
    auto exported_reader = alignx::io::BamReader::open(exported_bam_path);
    ASSERT_TRUE(exported_reader) << exported_reader.error();

    std::vector<std::string> exported_lines;
    for (;;) {
        auto line = exported_reader->next_sam_line();
        ASSERT_TRUE(line) << line.error();
        if (!line->has_value()) break;
        exported_lines.push_back(line->value());
    }

    ASSERT_EQ(cram_lines.size(), exported_lines.size())
        << "Record count mismatch: CRAM " << cram_lines.size() << " vs exported "
        << exported_lines.size();

    for (std::size_t i = 0; i < cram_lines.size(); ++i) {
        EXPECT_EQ(cram_lines[i], exported_lines[i]) << "SAM line " << i << " differs";
    }

    std::filesystem::remove_all(dir);
}

#endif

} // namespace
