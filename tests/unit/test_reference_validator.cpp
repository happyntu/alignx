#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

#include "format/axf1_file.hpp"
#include "io/fasta_reader.hpp"
#include "io/reference_validator.hpp"

namespace {

std::filesystem::path toy_ref_path() {
    return std::filesystem::path(TEST_DATA_DIR) / "toy_ref.fa";
}

#ifdef ALIGNX_HAVE_HTSLIB

TEST(ReferenceValidator, MatchingRefSucceeds) {
    auto fasta = alignx::io::FastaReader::open(toy_ref_path());
    ASSERT_TRUE(fasta) << fasta.error();
    auto sha = fasta->compute_contig_sha256("chrToy");
    ASSERT_TRUE(sha) << sha.error();

    std::vector<alignx::format::Axf1Reference> refs = {{"chrToy", 1000}};
    std::vector<std::pair<std::uint32_t, std::array<unsigned char, 32>>> checksums;
    checksums.push_back({0, *sha});
    auto ext = alignx::format::make_ref_contig_sha256_entry(checksums,
                                                             alignx::format::kExtFlagRequired);

    std::vector<alignx::format::MetadataEntry> extensions = {ext};
    auto validator = alignx::io::ReferenceValidator::create(toy_ref_path(), extensions, refs);
    ASSERT_TRUE(validator) << validator.error();

    auto result = validator->validate_contig(0);
    EXPECT_TRUE(result) << result.error();
}

TEST(ReferenceValidator, MismatchedRefFails) {
    std::vector<alignx::format::Axf1Reference> refs = {{"chrToy", 1000}};
    std::array<unsigned char, 32> wrong_sha{};
    wrong_sha[0] = 0xFF;
    std::vector<std::pair<std::uint32_t, std::array<unsigned char, 32>>> checksums;
    checksums.push_back({0, wrong_sha});
    auto ext = alignx::format::make_ref_contig_sha256_entry(checksums,
                                                             alignx::format::kExtFlagRequired);

    std::vector<alignx::format::MetadataEntry> extensions = {ext};
    auto validator = alignx::io::ReferenceValidator::create(toy_ref_path(), extensions, refs);
    ASSERT_TRUE(validator) << validator.error();

    auto result = validator->validate_contig(0);
    EXPECT_FALSE(result);
    EXPECT_NE(result.error().find("SHA-256 mismatch"), std::string::npos);
}

TEST(ReferenceValidator, CachesResult) {
    auto fasta = alignx::io::FastaReader::open(toy_ref_path());
    ASSERT_TRUE(fasta) << fasta.error();
    auto sha = fasta->compute_contig_sha256("chrToy");
    ASSERT_TRUE(sha) << sha.error();

    std::vector<alignx::format::Axf1Reference> refs = {{"chrToy", 1000}};
    std::vector<std::pair<std::uint32_t, std::array<unsigned char, 32>>> checksums;
    checksums.push_back({0, *sha});
    auto ext = alignx::format::make_ref_contig_sha256_entry(checksums);

    std::vector<alignx::format::MetadataEntry> extensions = {ext};
    auto validator = alignx::io::ReferenceValidator::create(toy_ref_path(), extensions, refs);
    ASSERT_TRUE(validator) << validator.error();

    auto r1 = validator->validate_contig(0);
    EXPECT_TRUE(r1) << r1.error();
    auto r2 = validator->validate_contig(0);
    EXPECT_TRUE(r2) << r2.error();
}

TEST(ReferenceValidator, MissingContigInFastaStillCreates) {
    std::vector<alignx::format::Axf1Reference> refs = {{"chrMissing", 500}};
    std::array<unsigned char, 32> sha{};
    std::vector<std::pair<std::uint32_t, std::array<unsigned char, 32>>> checksums;
    checksums.push_back({0, sha});
    auto ext = alignx::format::make_ref_contig_sha256_entry(checksums);

    std::vector<alignx::format::MetadataEntry> extensions = {ext};
    auto validator = alignx::io::ReferenceValidator::create(toy_ref_path(), extensions, refs);
    ASSERT_TRUE(validator) << validator.error();

    auto result = validator->validate_contig(0);
    EXPECT_FALSE(result);
}

TEST(ReferenceValidator, RequireReferenceForExtensions_NoRefWithRequired) {
    std::array<unsigned char, 32> sha{};
    std::vector<std::pair<std::uint32_t, std::array<unsigned char, 32>>> checksums;
    checksums.push_back({0, sha});
    auto ext = alignx::format::make_ref_contig_sha256_entry(checksums,
                                                             alignx::format::kExtFlagRequired);

    std::vector<alignx::format::MetadataEntry> extensions = {ext};
    auto result = alignx::io::require_reference_for_extensions(extensions, std::nullopt);
    EXPECT_FALSE(result);
    EXPECT_NE(result.error().find("requires reference FASTA"), std::string::npos);
}

TEST(ReferenceValidator, RequireReferenceForExtensions_WithRefSucceeds) {
    std::array<unsigned char, 32> sha{};
    std::vector<std::pair<std::uint32_t, std::array<unsigned char, 32>>> checksums;
    checksums.push_back({0, sha});
    auto ext = alignx::format::make_ref_contig_sha256_entry(checksums,
                                                             alignx::format::kExtFlagRequired);

    std::vector<alignx::format::MetadataEntry> extensions = {ext};
    std::optional<std::filesystem::path> ref = toy_ref_path();
    auto result = alignx::io::require_reference_for_extensions(extensions, ref);
    EXPECT_TRUE(result) << result.error();
}

TEST(ReferenceValidator, RequireReferenceForExtensions_OptionalNoRef) {
    std::array<unsigned char, 32> sha{};
    std::vector<std::pair<std::uint32_t, std::array<unsigned char, 32>>> checksums;
    checksums.push_back({0, sha});
    auto ext = alignx::format::make_ref_contig_sha256_entry(checksums, 0x00);

    std::vector<alignx::format::MetadataEntry> extensions = {ext};
    auto result = alignx::io::require_reference_for_extensions(extensions, std::nullopt);
    EXPECT_TRUE(result) << result.error();
}

#endif

} // namespace
