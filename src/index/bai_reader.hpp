#pragma once

#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace alignx::index {

struct BaiChunk {
    std::uint64_t begin = 0;
    std::uint64_t end = 0;
};

struct BaiBin {
    std::uint32_t id = 0;
    std::vector<BaiChunk> chunks;
};

struct BaiReference {
    std::vector<BaiBin> bins;
    std::vector<std::uint64_t> linear_offsets;
};

struct BaiIndex {
    std::vector<BaiReference> references;
    std::optional<std::uint64_t> unplaced_unmapped_count;

    [[nodiscard]] std::size_t reference_count() const noexcept;
};

[[nodiscard]] std::expected<BaiIndex, std::string>
read_bai_index(const std::filesystem::path& path);

} // namespace alignx::index
