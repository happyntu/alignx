#include <gtest/gtest.h>

#include <filesystem>
#include <string>

#include "io/fasta_reader.hpp"

namespace {

std::filesystem::path toy_ref_path() {
    return std::filesystem::path(TEST_DATA_DIR) / "toy_ref.fa";
}

} // namespace

TEST(FastaReader, OpensToyRef) {
    auto reader = alignx::io::FastaReader::open(toy_ref_path());
    ASSERT_TRUE(reader) << reader.error();
}

TEST(FastaReader, MissingFastaReturnsError) {
    auto reader = alignx::io::FastaReader::open("/nonexistent/path.fa");
    ASSERT_FALSE(reader);
    EXPECT_NE(reader.error().find("failed to open FASTA index"), std::string::npos);
}

TEST(FastaReader, ListsContigs) {
    auto reader = alignx::io::FastaReader::open(toy_ref_path());
    ASSERT_TRUE(reader) << reader.error();

    auto contigs = reader->contigs();
    ASSERT_TRUE(contigs) << contigs.error();
    ASSERT_EQ(contigs->size(), 1u);
    EXPECT_EQ((*contigs)[0].name, "chrToy");
    EXPECT_EQ((*contigs)[0].length, 1000u);
}

TEST(FastaReader, FetchFullContig) {
    auto reader = alignx::io::FastaReader::open(toy_ref_path());
    ASSERT_TRUE(reader) << reader.error();

    auto seq = reader->fetch_contig("chrToy");
    ASSERT_TRUE(seq) << seq.error();
    EXPECT_EQ(seq->size(), 1000u);
}

TEST(FastaReader, FetchSubsequence) {
    auto reader = alignx::io::FastaReader::open(toy_ref_path());
    ASSERT_TRUE(reader) << reader.error();

    // 0-based half-open [0, 10)
    auto seq = reader->fetch_sequence("chrToy", 0, 10);
    ASSERT_TRUE(seq) << seq.error();
    EXPECT_EQ(seq->size(), 10u);
    EXPECT_EQ(*seq, "AAAAAAAAAA");
}

TEST(FastaReader, FetchSubsequenceAtKnownRegion) {
    auto reader = alignx::io::FastaReader::open(toy_ref_path());
    ASSERT_TRUE(reader) << reader.error();

    auto seq = reader->fetch_sequence("chrToy", 100, 110);
    ASSERT_TRUE(seq) << seq.error();
    EXPECT_EQ(seq->size(), 10u);
    EXPECT_EQ(*seq, "ACGTACGTAA");
}

TEST(FastaReader, MissingContigReturnsError) {
    auto reader = alignx::io::FastaReader::open(toy_ref_path());
    ASSERT_TRUE(reader) << reader.error();

    auto seq = reader->fetch_contig("chrX");
    ASSERT_FALSE(seq);
    EXPECT_NE(seq.error().find("contig not found"), std::string::npos);
}

TEST(FastaReader, MoveConstruction) {
    auto reader = alignx::io::FastaReader::open(toy_ref_path());
    ASSERT_TRUE(reader) << reader.error();

    alignx::io::FastaReader moved = std::move(*reader);
    auto contigs = moved.contigs();
    ASSERT_TRUE(contigs) << contigs.error();
    EXPECT_EQ(contigs->size(), 1u);
}
