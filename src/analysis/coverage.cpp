#include "analysis/coverage.hpp"

#include <algorithm>
#include <cctype>

namespace alignx::analysis {

std::expected<void, std::string> add_coverage(CoverageResult& result, std::int32_t record_pos,
                                              std::string_view cigar) {
    if (cigar.empty() || cigar == "*") {
        return {};
    }

    std::int32_t ref_pos = record_pos;
    std::int64_t length = 0;
    for (char ch : cigar) {
        if (std::isdigit(static_cast<unsigned char>(ch))) {
            length = length * 10 + (ch - '0');
            continue;
        }
        if (length == 0) {
            return std::unexpected("invalid CIGAR string");
        }
        switch (ch) {
        case 'M':
        case '=':
        case 'X': {
            const std::int32_t cov_start = std::max(ref_pos, result.start);
            const std::int32_t cov_end = std::min(ref_pos + static_cast<std::int32_t>(length), result.end);
            for (std::int32_t p = cov_start; p < cov_end; ++p) {
                result.depth[static_cast<std::size_t>(p - result.start)] += 1;
            }
            ref_pos += static_cast<std::int32_t>(length);
            break;
        }
        case 'D':
        case 'N':
            ref_pos += static_cast<std::int32_t>(length);
            break;
        case 'I':
        case 'S':
        case 'H':
        case 'P':
            break;
        default:
            return std::unexpected("invalid CIGAR operation");
        }
        length = 0;
    }
    if (length != 0) {
        return std::unexpected("invalid CIGAR string");
    }
    return {};
}

} // namespace alignx::analysis
