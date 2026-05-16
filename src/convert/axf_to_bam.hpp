#pragma once

#include <expected>
#include <filesystem>
#include <optional>
#include <string>

namespace alignx::convert {

[[nodiscard]] std::expected<void, std::string>
convert_axf1_to_bam(const std::filesystem::path& input_axf,
                    const std::filesystem::path& output_bam,
                    std::optional<int> hts_threads = std::nullopt);

} // namespace alignx::convert
