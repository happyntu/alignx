#include <gtest/gtest.h>

#include <filesystem>
#include <string>
#include <vector>

#include "io/bam_reader.hpp"

namespace {

std::filesystem::path toy_bam_path() {
    return std::filesystem::path(TEST_DATA_DIR) / "toy_alignment.sorted.bam";
}

#ifdef ALIGNX_HAVE_HTSLIB

TEST(BamReader, OpensToyBamAndLoadsIndex) {
    auto reader = alignx::io::BamReader::open(toy_bam_path());
    ASSERT_TRUE(reader) << reader.error();

    EXPECT_EQ(reader->reference_count(), 1);
    EXPECT_TRUE(reader->has_index());
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

#else

TEST(BamReader, ReportsMissingHtslib) {
    auto reader = alignx::io::BamReader::open(toy_bam_path());
    ASSERT_FALSE(reader);
    EXPECT_NE(reader.error().find("without HTSlib"), std::string::npos);
}

#endif

} // namespace
