#pragma once

#include <chrono>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <string>

#include "analysis/coverage.hpp"
#include "query/record_filter.hpp"

namespace alignx::query {

struct Axf1CoverageProfile {
    std::uint64_t chunks_selected = 0;
    std::uint64_t chunks_with_matches = 0;
    std::uint64_t records_scanned = 0;
    std::uint64_t records_matched = 0;
    std::uint64_t records_filtered = 0;
    std::uint64_t selective_bytes_read = 0;
    std::uint64_t selective_payload_bytes = 0;
    std::chrono::steady_clock::duration open_time{};
    std::chrono::steady_clock::duration reference_lookup_time{};
    std::chrono::steady_clock::duration chunk_query_time{};
    std::chrono::steady_clock::duration selective_decode_time{};
    std::chrono::steady_clock::duration filter_time{};
    std::chrono::steady_clock::duration coverage_time{};
};

[[nodiscard]] std::expected<analysis::CoverageResult, std::string>
compute_axf1_coverage(const std::filesystem::path& input, const std::string& region,
                      const RecordFilter& filter = {});

[[nodiscard]] std::expected<analysis::CoverageResult, std::string>
compute_axf1_coverage_profiled(const std::filesystem::path& input, const std::string& region,
                               Axf1CoverageProfile& profile, const RecordFilter& filter = {});

} // namespace alignx::query
