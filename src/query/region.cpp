#include "query/region.hpp"

#include <cctype>
#include <limits>

namespace alignx::query {
namespace {

std::expected<std::int32_t, std::string> parse_positive_i32(std::string_view text,
                                                            std::string_view label) {
    if (text.empty()) {
        return std::unexpected("missing " + std::string(label));
    }

    std::int64_t value = 0;
    for (char ch : text) {
        if (!std::isdigit(static_cast<unsigned char>(ch))) {
            return std::unexpected("invalid " + std::string(label));
        }
        value = value * 10 + (ch - '0');
        if (value > std::numeric_limits<std::int32_t>::max()) {
            return std::unexpected(std::string(label) + " is too large");
        }
    }
    if (value <= 0) {
        return std::unexpected(std::string(label) + " must be positive");
    }
    return static_cast<std::int32_t>(value);
}

} // namespace

std::expected<SamRegion, std::string> parse_sam_region(std::string_view region) {
    const std::size_t colon = region.find(':');
    const std::size_t dash = region.find('-', colon == std::string_view::npos ? 0 : colon + 1);
    if (colon == std::string_view::npos || dash == std::string_view::npos || colon == 0 ||
        dash <= colon + 1 || dash + 1 >= region.size()) {
        return std::unexpected("region must use ref:start-end");
    }

    auto one_based_start = parse_positive_i32(region.substr(colon + 1, dash - colon - 1), "start");
    auto one_based_end = parse_positive_i32(region.substr(dash + 1), "end");
    if (!one_based_start) {
        return std::unexpected(one_based_start.error());
    }
    if (!one_based_end) {
        return std::unexpected(one_based_end.error());
    }
    if (*one_based_start > *one_based_end) {
        return std::unexpected("region start must be <= end");
    }

    return SamRegion{.reference = std::string(region.substr(0, colon)),
                     .start = *one_based_start - 1,
                     .end = *one_based_end};
}

} // namespace alignx::query
