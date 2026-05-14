#pragma once

#include <cstdint>
#include <expected>
#include <filesystem>
#include <string>
#include <vector>

namespace alignx::format {

enum class Axf1ColumnId : std::uint16_t {
    qname = 1,
    flag = 2,
    pos = 3,
    mapq = 4,
    cigar = 5,
    mate_reference = 6,
    mate_pos = 7,
    template_length = 8,
    sequence = 9,
    quality = 10,
    tags = 11,
};

enum class Axf1CodecId : std::uint16_t {
    raw = 0,
    pos_delta_varint = 1,
    flag_bitpack = 2,
    mapq_rle = 3,
    seq_2bit_literal = 4,
};

struct Axf1Reference {
    std::string name;
    std::uint32_t length = 0;
};

struct Axf1Record {
    std::string qname;
    std::uint16_t flag = 0;
    std::int32_t pos = -1; // 0-based
    std::uint8_t mapq = 0;
    std::string cigar;
    std::string mate_reference;
    std::int32_t mate_pos = 0;
    std::int32_t template_length = 0;
    std::string sequence;
    std::string quality;
    std::string tags;
};

struct Axf1Chunk {
    std::uint32_t ref_id = 0;
    std::int32_t start_pos = -1;
    std::int32_t end_pos = -1;
    std::vector<Axf1Record> records;
};

struct Axf1FileMetadata {
    std::string source_path;
    std::string conversion_region;
    bool is_subset = false;
};

struct Axf1File {
    Axf1FileMetadata metadata;
    std::vector<Axf1Reference> references;
    std::vector<Axf1Chunk> chunks;
};

struct Axf1FileIndexMetadata {
    std::string source_path;
    std::string conversion_region;
    bool is_subset = false;
};

struct Axf1ChunkIndexEntry {
    std::uint32_t ref_id = 0;
    std::int32_t start_pos = -1;
    std::int32_t end_pos = -1;
    std::uint32_t record_count = 0;
    std::uint64_t chunk_offset = 0;
    std::uint64_t chunk_length = 0;

    [[nodiscard]] bool overlaps(std::int32_t query_start, std::int32_t query_end) const noexcept;
};

struct Axf1FileIndex {
    Axf1FileIndexMetadata metadata;
    std::vector<Axf1Reference> references;
    std::vector<Axf1ChunkIndexEntry> chunks;

    [[nodiscard]] std::expected<std::vector<const Axf1ChunkIndexEntry*>, std::string>
    query_chunks(std::uint32_t ref_id, std::int32_t start, std::int32_t end) const;
};

class Axf1FileReader {
public:
    [[nodiscard]] static std::expected<Axf1FileReader, std::string>
    open(std::filesystem::path path);

    [[nodiscard]] const Axf1FileIndex& index() const noexcept;

    [[nodiscard]] std::expected<std::vector<const Axf1ChunkIndexEntry*>, std::string>
    query_chunks(std::uint32_t ref_id, std::int32_t start, std::int32_t end) const;

    [[nodiscard]] std::expected<Axf1Chunk, std::string>
    read_chunk(const Axf1ChunkIndexEntry& chunk) const;

    [[nodiscard]] std::expected<Axf1Chunk, std::string>
    read_chunk_columns(const Axf1ChunkIndexEntry& chunk,
                       const std::vector<Axf1ColumnId>& columns) const;

private:
    Axf1FileReader(std::filesystem::path path, Axf1FileIndex index);

    std::filesystem::path path_;
    Axf1FileIndex index_;
};

[[nodiscard]] std::expected<void, std::string> write_axf1_file(const Axf1File& file,
                                                               const std::filesystem::path& path);

[[nodiscard]] std::expected<Axf1File, std::string>
read_axf1_file(const std::filesystem::path& path);

[[nodiscard]] std::expected<Axf1FileIndex, std::string>
read_axf1_index_metadata(const std::filesystem::path& path);

[[nodiscard]] std::expected<Axf1Chunk, std::string>
read_axf1_chunk(const std::filesystem::path& path, const Axf1ChunkIndexEntry& chunk);

[[nodiscard]] std::expected<Axf1Chunk, std::string>
read_axf1_chunk_columns(const std::filesystem::path& path, const Axf1ChunkIndexEntry& chunk,
                        const std::vector<Axf1ColumnId>& columns);

} // namespace alignx::format
