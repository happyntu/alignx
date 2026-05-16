#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include "io/bam_reader.hpp"

namespace {

std::filesystem::path toy_bam_path() {
    return std::filesystem::path(TEST_DATA_DIR) / "toy_alignment.sorted.bam";
}

std::filesystem::path toy_cram_path() {
    return std::filesystem::path(TEST_DATA_DIR) / "toy_alignment.sorted.cram";
}

void set_env_var(const char* name, const char* value) {
#ifdef _WIN32
    _putenv_s(name, value);
#else
    setenv(name, value, 1);
#endif
}

void unset_env_var(const char* name) {
#ifdef _WIN32
    _putenv_s(name, "");
#else
    unsetenv(name);
#endif
}

#ifdef ALIGNX_HAVE_HTSLIB

TEST(BamReader, OpensToyBamAndLoadsIndex) {
    auto reader = alignx::io::BamReader::open(toy_bam_path());
    ASSERT_TRUE(reader) << reader.error();

    EXPECT_EQ(reader->reference_count(), 1);
    EXPECT_TRUE(reader->has_index());
}

TEST(BamReader, OpensToyBamWithConfiguredHtsThreads) {
    set_env_var("ALIGNX_HTS_THREADS", "1");

    auto reader = alignx::io::BamReader::open(toy_bam_path());

    unset_env_var("ALIGNX_HTS_THREADS");

    ASSERT_TRUE(reader) << reader.error();
    EXPECT_TRUE(reader->has_index());
}

TEST(BamReader, RejectsInvalidConfiguredHtsThreads) {
    set_env_var("ALIGNX_HTS_THREADS", "not-an-integer");

    auto reader = alignx::io::BamReader::open(toy_bam_path());

    unset_env_var("ALIGNX_HTS_THREADS");

    ASSERT_FALSE(reader);
    EXPECT_NE(reader.error().find("ALIGNX_HTS_THREADS"), std::string::npos);
}

TEST(BamReader, StreamsAllToyRecords) {
    auto reader = alignx::io::BamReader::open(toy_bam_path());
    ASSERT_TRUE(reader) << reader.error();

    std::vector<std::string> qnames;
    for (;;) {
        auto record = reader->next_record();
        ASSERT_TRUE(record) << record.error();
        if (!record->has_value()) {
            break;
        }
        qnames.push_back((*record)->qname);
    }

    ASSERT_EQ(qnames.size(), 3);
    EXPECT_EQ(qnames[0], "read001");
    EXPECT_EQ(qnames[1], "read002");
    EXPECT_EQ(qnames[2], "read003");
}

TEST(BamReader, FetchesRegionFromToyBam) {
    auto reader = alignx::io::BamReader::open(toy_bam_path());
    ASSERT_TRUE(reader) << reader.error();

    auto fetch = reader->fetch("chrToy:1-250");
    ASSERT_TRUE(fetch) << fetch.error();

    std::vector<alignx::io::BamRecord> records;
    for (;;) {
        auto record = reader->next_record();
        ASSERT_TRUE(record) << record.error();
        if (!record->has_value()) {
            break;
        }
        records.push_back(**record);
    }

    ASSERT_EQ(records.size(), 2);
    EXPECT_EQ(records[0].qname, "read001");
    EXPECT_EQ(records[0].reference_name, "chrToy");
    EXPECT_EQ(records[0].position, 100);
    EXPECT_EQ(records[0].end_position, 110);
    EXPECT_EQ(records[0].mapq, 60);
    EXPECT_FALSE(records[0].is_unmapped());

    EXPECT_EQ(records[1].qname, "read002");
    EXPECT_EQ(records[1].position, 150);
    EXPECT_EQ(records[1].end_position, 159);
    EXPECT_EQ(records[1].mapq, 50);
}

TEST(BamReader, FormatsSamLineViewsForToyRegion) {
    auto reader = alignx::io::BamReader::open(toy_bam_path());
    ASSERT_TRUE(reader) << reader.error();

    auto fetch = reader->fetch("chrToy:1-250");
    ASSERT_TRUE(fetch) << fetch.error();

    std::vector<std::string> lines;
    for (;;) {
        auto line = reader->next_sam_line_view();
        ASSERT_TRUE(line) << line.error();
        if (!line->has_value()) {
            break;
        }
        lines.emplace_back(line->value());
    }

    ASSERT_EQ(lines.size(), 2);
    EXPECT_EQ(lines[0],
              "read001\t0\tchrToy\t101\t60\t10M\t*\t0\t0\tACGTACGTAA\tFFFFFFFFFF\tNM:i:0");
    EXPECT_EQ(lines[1],
              "read002\t16\tchrToy\t151\t50\t5M1I4M\t*\t0\t0\tTTTTACGGGA\tFFFFFFFFFF\tNM:i:1");
}

TEST(CramReader, OpensToyAndLoadsIndex) {
    auto reader = alignx::io::BamReader::open(toy_cram_path());
    ASSERT_TRUE(reader) << reader.error();

    EXPECT_EQ(reader->reference_count(), 1);
    EXPECT_TRUE(reader->has_index());
}

TEST(CramReader, StreamsAllToyRecords) {
    auto reader = alignx::io::BamReader::open(toy_cram_path());
    ASSERT_TRUE(reader) << reader.error();

    std::vector<std::string> qnames;
    for (;;) {
        auto record = reader->next_record();
        ASSERT_TRUE(record) << record.error();
        if (!record->has_value()) {
            break;
        }
        qnames.push_back((*record)->qname);
    }

    ASSERT_EQ(qnames.size(), 3);
    EXPECT_EQ(qnames[0], "read001");
    EXPECT_EQ(qnames[1], "read002");
    EXPECT_EQ(qnames[2], "read003");
}

TEST(CramReader, FetchesRegionFromToyCram) {
    auto reader = alignx::io::BamReader::open(toy_cram_path());
    ASSERT_TRUE(reader) << reader.error();

    auto fetch = reader->fetch("chrToy:1-250");
    ASSERT_TRUE(fetch) << fetch.error();

    std::vector<alignx::io::BamRecord> records;
    for (;;) {
        auto record = reader->next_record();
        ASSERT_TRUE(record) << record.error();
        if (!record->has_value()) {
            break;
        }
        records.push_back(**record);
    }

    ASSERT_EQ(records.size(), 2);
    EXPECT_EQ(records[0].qname, "read001");
    EXPECT_EQ(records[0].reference_name, "chrToy");
    EXPECT_EQ(records[0].position, 100);
    EXPECT_EQ(records[0].end_position, 110);
    EXPECT_EQ(records[0].mapq, 60);
    EXPECT_FALSE(records[0].is_unmapped());

    EXPECT_EQ(records[1].qname, "read002");
    EXPECT_EQ(records[1].position, 150);
    EXPECT_EQ(records[1].end_position, 159);
    EXPECT_EQ(records[1].mapq, 50);
}

TEST(CramReader, FormatsSamLineForToyRegion) {
    auto reader = alignx::io::BamReader::open(toy_cram_path());
    ASSERT_TRUE(reader) << reader.error();

    auto fetch = reader->fetch("chrToy:1-250");
    ASSERT_TRUE(fetch) << fetch.error();

    std::vector<std::string> lines;
    for (;;) {
        auto line = reader->next_sam_line_view();
        ASSERT_TRUE(line) << line.error();
        if (!line->has_value()) {
            break;
        }
        lines.emplace_back(line->value());
    }

    ASSERT_EQ(lines.size(), 2);
    // CRAM adds MD:Z tag computed from reference; core fields identical to BAM
    EXPECT_EQ(
        lines[0],
        "read001\t0\tchrToy\t101\t60\t10M\t*\t0\t0\tACGTACGTAA\tFFFFFFFFFF\tMD:Z:10\tNM:i:0");
    EXPECT_EQ(
        lines[1],
        "read002\t16\tchrToy\t151\t50\t5M1I4M\t*\t0\t0\tTTTTACGGGA\tFFFFFFFFFF\tMD:Z:9\tNM:i:1");
}

#else

TEST(BamReader, ReportsMissingHtslib) {
    auto reader = alignx::io::BamReader::open(toy_bam_path());
    ASSERT_FALSE(reader);
    EXPECT_NE(reader.error().find("without HTSlib"), std::string::npos);
}

#endif

} // namespace
