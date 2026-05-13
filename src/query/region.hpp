#pragma once

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>

namespace alignx::query {

struct SamRegion {
    std::string reference;
    std::int32_t start = 0;
    std::int32_t end = 0;
};

[[nodiscard]] std::expected<SamRegion, std::string> parse_sam_region(std::string_view region);

} // namespace alignx::query
