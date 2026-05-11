#pragma once

#include <cstdint>
#include <expected>
#include <filesystem>
#include <string>
#include <vector>

namespace alignx::index {

struct AxfInterval {
    std::uint32_t start = 0;
    std::uint32_t end = 0;
    std::uint64_t chunk_offset = 0;
    std::uint64_t column_index_offset = 0;

    [[nodiscard]] bool overlaps(std::uint32_t query_start, std::uint32_t query_end) const noexcept;
};

class AxfIndex {
public:
    explicit AxfIndex(std::uint32_t reference_count = 0);

    [[nodiscard]] std::uint32_t reference_count() const noexcept;
    [[nodiscard]] const std::vector<AxfInterval>& intervals(std::uint32_t ref_id) const;

    void add_interval(std::uint32_t ref_id, AxfInterval interval);

    [[nodiscard]] std::expected<void, std::string> sort_and_validate();
    [[nodiscard]] std::expected<std::vector<AxfInterval>, std::string>
    query(std::uint32_t ref_id, std::uint32_t start, std::uint32_t end) const;

private:
    std::vector<std::vector<AxfInterval>> references_;
};

[[nodiscard]] std::expected<void, std::string> write_axf_index(const AxfIndex& index,
                                                               const std::filesystem::path& path);

[[nodiscard]] std::expected<AxfIndex, std::string>
read_axf_index(const std::filesystem::path& path);

} // namespace alignx::index
