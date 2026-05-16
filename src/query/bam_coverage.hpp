#pragma once

#include <chrono>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>

#include "analysis/coverage.hpp"

namespace alignx::query {

struct BamCoverageProfile {
    std::uint64_t records_scanned = 0;
    std::uint64_t records_matched = 0;
    std::chrono::steady_clock::duration open_time{};
    std::chrono::steady_clock::duration fetch_time{};
    std::chrono::steady_clock::duration read_time{};
    std::chrono::steady_clock::duration coverage_time{};
};

[[nodiscard]] std::expected<analysis::CoverageResult, std::string>
compute_bam_coverage(const std::filesystem::path& input, const std::string& region,
                     std::optional<int> hts_threads = std::nullopt);

[[nodiscard]] std::expected<analysis::CoverageResult, std::string>
compute_bam_coverage_profiled(const std::filesystem::path& input, const std::string& region,
                              BamCoverageProfile& profile,
                              std::optional<int> hts_threads = std::nullopt);

} // namespace alignx::query
