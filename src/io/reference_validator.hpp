#pragma once

#include <array>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

#include "format/axf1_file.hpp"
#include "io/fasta_reader.hpp"

namespace alignx::io {

class ReferenceValidator {
public:
    [[nodiscard]] static std::expected<ReferenceValidator, std::string>
    create(const std::filesystem::path& fasta_path,
           const std::vector<format::MetadataEntry>& extensions,
           const std::vector<format::Axf1Reference>& references);

    [[nodiscard]] std::expected<void, std::string>
    validate_contig(std::uint32_t ref_id) const;

    [[nodiscard]] bool has_checksums() const noexcept;

private:
    struct ContigCheck {
        std::string name;
        std::array<unsigned char, 32> expected_sha256{};
        mutable bool validated = false;
        mutable bool valid = false;
    };

    FastaReader fasta_;
    std::unordered_map<std::uint32_t, ContigCheck> checks_;

    ReferenceValidator(FastaReader fasta,
                       std::unordered_map<std::uint32_t, ContigCheck> checks);
};

[[nodiscard]] std::expected<void, std::string>
require_reference_for_extensions(const std::vector<format::MetadataEntry>& extensions,
                                 const std::optional<std::filesystem::path>& reference);

} // namespace alignx::io
