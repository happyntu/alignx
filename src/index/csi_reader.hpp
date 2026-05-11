#pragma once

#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace alignx::index {

struct CsiChunk {
    std::uint64_t begin = 0;
    std::uint64_t end = 0;
};

struct CsiBin {
    std::uint32_t id = 0;
    std::uint64_t loffset = 0;
    std::vector<CsiChunk> chunks;
};

struct CsiReference {
    std::vector<CsiBin> bins;
};

struct CsiIndex {
    std::int32_t min_shift = 0;
    std::int32_t depth = 0;
    std::vector<unsigned char> aux;
    std::vector<CsiReference> references;
    std::optional<std::uint64_t> unplaced_unmapped_count;

    [[nodiscard]] std::size_t reference_count() const noexcept;
};

[[nodiscard]] std::expected<CsiIndex, std::string>
read_csi_index(const std::filesystem::path& path);

} // namespace alignx::index
