#pragma once

#include <cstdint>
#include <expected>
#include <filesystem>
#include <string>
#include <vector>

namespace alignx::format {

struct AxfReference {
    std::string name;
    std::uint32_t length = 0;
};

struct AxfBlock {
    std::uint32_t ref_id = 0;
    std::int32_t start_pos = -1;
    std::int32_t end_pos = -1;
    std::uint32_t record_count = 0;
    std::vector<unsigned char> payload;

    [[nodiscard]] bool overlaps(std::int32_t query_start, std::int32_t query_end) const noexcept;
};

struct AxfFile {
    std::vector<AxfReference> references;
    std::vector<AxfBlock> blocks;

    [[nodiscard]] std::expected<std::vector<const AxfBlock*>, std::string>
    query_blocks(std::uint32_t ref_id, std::int32_t start, std::int32_t end) const;
};

[[nodiscard]] std::expected<void, std::string> write_axf_file(const AxfFile& file,
                                                              const std::filesystem::path& path);

[[nodiscard]] std::expected<AxfFile, std::string> read_axf_file(const std::filesystem::path& path);

} // namespace alignx::format
