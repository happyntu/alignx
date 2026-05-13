#pragma once

#include <expected>
#include <filesystem>
#include <iosfwd>
#include <string>

namespace alignx::query {

[[nodiscard]] std::expected<void, std::string>
write_axf_region_sam(const std::filesystem::path& input, const std::string& region,
                     std::ostream& out);

} // namespace alignx::query
