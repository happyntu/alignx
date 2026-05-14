#pragma once

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>

namespace alignx::query {

[[nodiscard]] std::expected<std::int32_t, std::string> cigar_reference_span(std::string_view cigar);

[[nodiscard]] bool half_open_intervals_overlap(std::int32_t lhs_start, std::int32_t lhs_end,
                                               std::int32_t rhs_start,
                                               std::int32_t rhs_end) noexcept;

} // namespace alignx::query
