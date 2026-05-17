#include "io/reference_validator.hpp"

#include <utility>

namespace alignx::io {

ReferenceValidator::ReferenceValidator(FastaReader fasta,
                                       std::unordered_map<std::uint32_t, ContigCheck> checks)
    : fasta_(std::move(fasta)), checks_(std::move(checks)) {}

std::expected<ReferenceValidator, std::string>
ReferenceValidator::create(const std::filesystem::path& fasta_path,
                           const std::vector<format::MetadataEntry>& extensions,
                           const std::vector<format::Axf1Reference>& references) {
    auto fasta = FastaReader::open(fasta_path);
    if (!fasta) {
        return std::unexpected("failed to open reference FASTA: " + fasta.error());
    }

    std::unordered_map<std::uint32_t, ContigCheck> checks;
    for (const auto& ext : extensions) {
        if (ext.key_id == format::extension_key::kRefContigSha256) {
            auto parsed = format::parse_ref_contig_sha256_entry(ext);
            for (const auto& [ref_id, sha] : parsed) {
                if (ref_id < references.size()) {
                    checks[ref_id] = ContigCheck{
                        .name = references[ref_id].name,
                        .expected_sha256 = sha};
                }
            }
        }
    }

    return ReferenceValidator(std::move(*fasta), std::move(checks));
}

std::expected<void, std::string>
ReferenceValidator::validate_contig(std::uint32_t ref_id) const {
    auto it = checks_.find(ref_id);
    if (it == checks_.end()) {
        return {};
    }
    auto& check = it->second;
    if (check.validated) {
        if (check.valid) return {};
        return std::unexpected("reference contig " + check.name +
                               " SHA-256 mismatch (cached)");
    }
    check.validated = true;

    auto sha = fasta_.compute_contig_sha256(check.name);
    if (!sha) {
        check.valid = false;
        return std::unexpected("failed to compute SHA-256 for contig " + check.name +
                               ": " + sha.error());
    }
    if (*sha != check.expected_sha256) {
        check.valid = false;
        return std::unexpected("reference contig " + check.name +
                               " SHA-256 mismatch; provide the correct reference FASTA");
    }
    check.valid = true;
    return {};
}

bool ReferenceValidator::has_checksums() const noexcept {
    return !checks_.empty();
}

std::expected<void, std::string>
require_reference_for_extensions(const std::vector<format::MetadataEntry>& extensions,
                                 const std::optional<std::filesystem::path>& reference) {
    for (const auto& ext : extensions) {
        if (ext.key_id == format::extension_key::kRefContigSha256 &&
            (ext.flags & format::kExtFlagRequired) != 0) {
            if (!reference.has_value()) {
                return std::unexpected(
                    "file requires reference FASTA for decode; "
                    "use --reference <path> or set ALIGNX_REFERENCE");
            }
            return {};
        }
    }
    return {};
}

} // namespace alignx::io
