#pragma once

#include <expected>
#include <filesystem>
#include <optional>
#include <string>

namespace alignx::convert {

[[nodiscard]] std::expected<void, std::string>
convert_bam_to_axf_mvp(const std::filesystem::path& input_bam,
                       const std::filesystem::path& output_axf,
                       const std::optional<std::string>& region = std::nullopt);

[[nodiscard]] std::expected<void, std::string>
convert_bam_to_axf1_mvp(const std::filesystem::path& input_bam,
                        const std::filesystem::path& output_axf,
                        const std::optional<std::string>& region = std::nullopt);

} // namespace alignx::convert
