#pragma once

#include <expected>
#include <filesystem>
#include <string>

namespace alignx::convert {

[[nodiscard]] std::expected<void, std::string>
convert_bam_to_axf_mvp(const std::filesystem::path& input_bam,
                       const std::filesystem::path& output_axf);

} // namespace alignx::convert
