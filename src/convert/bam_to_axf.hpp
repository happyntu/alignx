#pragma once

#include <expected>
#include <filesystem>
#include <optional>
#include <string>

#include "convert/axf1_chunk_policy.hpp"
#include "format/axf1_file.hpp"

namespace alignx::convert {

[[nodiscard]] std::expected<void, std::string>
convert_bam_to_axf_mvp(const std::filesystem::path& input_bam,
                       const std::filesystem::path& output_axf,
                       const std::optional<std::string>& region = std::nullopt);

[[nodiscard]] std::expected<void, std::string>
convert_bam_to_axf1_mvp(const std::filesystem::path& input_bam,
                        const std::filesystem::path& output_axf,
                        const std::optional<std::string>& region = std::nullopt);

[[nodiscard]] std::expected<void, std::string> convert_bam_to_axf1_mvp(
    const std::filesystem::path& input_bam, const std::filesystem::path& output_axf,
    const std::optional<std::string>& region, const format::Axf1WriteOptions& options);

[[nodiscard]] std::expected<void, std::string> convert_bam_to_axf1_mvp(
    const std::filesystem::path& input_bam, const std::filesystem::path& output_axf,
    const std::optional<std::string>& region, const format::Axf1WriteOptions& options,
    const Axf1ChunkPolicy& chunk_policy);

} // namespace alignx::convert
