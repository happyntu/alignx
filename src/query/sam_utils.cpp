#include "query/sam_utils.hpp"

#include <algorithm>
#include <cctype>
#include <limits>

namespace alignx::query {

std::expected<std::int32_t, std::string> cigar_reference_span(std::string_view cigar) {
    if (cigar.empty() || cigar == "*") {
        return 1;
    }

    std::int64_t span = 0;
    std::int64_t length = 0;
    bool saw_op = false;
    for (char ch : cigar) {
        if (std::isdigit(static_cast<unsigned char>(ch))) {
            length = length * 10 + (ch - '0');
            if (length > std::numeric_limits<std::int32_t>::max()) {
                return std::unexpected("CIGAR operation length is too large");
            }
            continue;
        }

        if (length == 0) {
            return std::unexpected("invalid CIGAR string");
        }
        switch (ch) {
        case 'M':
        case 'D':
        case 'N':
        case '=':
        case 'X':
            span += length;
            if (span > std::numeric_limits<std::int32_t>::max()) {
                return std::unexpected("CIGAR reference span is too large");
            }
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
        saw_op = true;
    }
    if (!saw_op || length != 0) {
        return std::unexpected("invalid CIGAR string");
    }
    return static_cast<std::int32_t>(std::max<std::int64_t>(span, 1));
}

bool half_open_intervals_overlap(std::int32_t lhs_start, std::int32_t lhs_end,
                                 std::int32_t rhs_start, std::int32_t rhs_end) noexcept {
    return lhs_start < rhs_end && rhs_start < lhs_end;
}

} // namespace alignx::query
