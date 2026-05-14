#pragma once

#include <cstddef>
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

struct AxfBlockIndexEntry {
    std::uint32_t ref_id = 0;
    std::int32_t start_pos = -1;
    std::int32_t end_pos = -1;
    std::uint32_t record_count = 0;
    std::uint64_t payload_offset = 0;
    std::uint64_t payload_length = 0;

    [[nodiscard]] bool overlaps(std::int32_t query_start, std::int32_t query_end) const noexcept;
};

struct AxfBlockRange {
    std::size_t begin = 0;
    std::size_t end = 0;
};

struct AxfFileIndex {
    std::vector<AxfReference> references;
    std::vector<AxfBlockIndexEntry> blocks;
    std::vector<AxfBlockRange> reference_block_ranges;
    std::vector<std::size_t> end_sorted_block_indices;
    std::vector<AxfBlockRange> reference_end_sorted_block_ranges;

    [[nodiscard]] std::expected<std::vector<const AxfBlockIndexEntry*>, std::string>
    query_blocks(std::uint32_t ref_id, std::int32_t start, std::int32_t end) const;
};

class AxfFileReader {
public:
    [[nodiscard]] static std::expected<AxfFileReader, std::string> open(std::filesystem::path path);

    [[nodiscard]] const AxfFileIndex& index() const noexcept;

    [[nodiscard]] std::expected<std::vector<const AxfBlockIndexEntry*>, std::string>
    query_blocks(std::uint32_t ref_id, std::int32_t start, std::int32_t end) const;

    [[nodiscard]] std::expected<std::vector<unsigned char>, std::string>
    read_payload(const AxfBlockIndexEntry& block) const;

private:
    AxfFileReader(std::filesystem::path path, AxfFileIndex index);

    std::filesystem::path path_;
    AxfFileIndex index_;
};

[[nodiscard]] std::expected<void, std::string> write_axf_file(const AxfFile& file,
                                                              const std::filesystem::path& path);

[[nodiscard]] std::expected<AxfFile, std::string> read_axf_file(const std::filesystem::path& path);

[[nodiscard]] std::expected<AxfFileIndex, std::string>
read_axf_index_metadata(const std::filesystem::path& path);

[[nodiscard]] std::expected<std::vector<unsigned char>, std::string>
read_axf_block_payload(const std::filesystem::path& path, const AxfBlockIndexEntry& block);

} // namespace alignx::format
