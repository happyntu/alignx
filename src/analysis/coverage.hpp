#pragma once

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

namespace alignx::analysis {

struct CoverageResult {
    std::string reference;
    std::int32_t start = 0; // 0-based inclusive
    std::int32_t end = 0;   // 0-based exclusive
    std::vector<std::uint32_t> depth;
    std::uint64_t records_counted = 0;
};

[[nodiscard]] std::expected<void, std::string>
add_coverage(CoverageResult& result, std::int32_t record_pos, std::string_view cigar);

} // namespace alignx::analysis
