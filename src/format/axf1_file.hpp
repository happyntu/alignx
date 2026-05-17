#pragma once

#include <array>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <utility>
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
    cigar_token = 5,
    qual_rle = 6,
    qual_pack = 7,
    qual_pack_compressed = 8,
    qname_dict = 9,
    tags_per_stream = 10,
    cigar_dict = 11,
    compressed = 12,
    seq_ref_delta = 13,
};

enum class Axf1Compression {
    none,
    zstd,
};

enum class Axf1QualityLossy {
    none,
    illumina8,
};

struct Axf1WriteOptions {
    Axf1Compression quality_compression = Axf1Compression::none;
    Axf1Compression column_compression = Axf1Compression::none;
    Axf1QualityLossy quality_lossy = Axf1QualityLossy::none;
    std::optional<std::filesystem::path> reference_fasta;
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

struct MetadataEntry {
    std::uint16_t key_id = 0;
    std::uint8_t flags = 0;
    std::vector<unsigned char> value;
};

namespace extension_key {
constexpr std::uint16_t kRefAssemblyName = 1;
constexpr std::uint16_t kRefContigTable = 2;
constexpr std::uint16_t kRefContigSha256 = 3;
constexpr std::uint16_t kRefFastaUri = 4;
constexpr std::uint16_t kBamHeaderSha256 = 5;
constexpr std::uint16_t kEncodeReferencePath = 6;
} // namespace extension_key

constexpr std::uint8_t kExtFlagRequired = 0x01;

struct Axf1FileMetadata {
    std::string source_path;
    std::string conversion_region;
    bool is_subset = false;
    std::vector<MetadataEntry> extensions;
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
    std::vector<MetadataEntry> extensions;
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

struct Axf1ChunkReadProfile {
    std::uint64_t bytes_read = 0;
    std::uint64_t total_payload_bytes = 0;
    std::uint64_t selected_payload_bytes = 0;
    std::uint16_t total_columns = 0;
    std::uint16_t selected_columns = 0;
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
    Axf1FileReader(Axf1FileReader&&) noexcept;
    Axf1FileReader& operator=(Axf1FileReader&&) noexcept;
    ~Axf1FileReader();

    [[nodiscard]] static std::expected<Axf1FileReader, std::string>
    open(std::filesystem::path path);

    [[nodiscard]] const Axf1FileIndex& index() const noexcept;

    [[nodiscard]] std::expected<std::vector<const Axf1ChunkIndexEntry*>, std::string>
    query_chunks(std::uint32_t ref_id, std::int32_t start, std::int32_t end) const;

    [[nodiscard]] std::expected<Axf1Chunk, std::string>
    read_chunk(const Axf1ChunkIndexEntry& chunk);

    [[nodiscard]] std::expected<Axf1Chunk, std::string>
    read_chunk_profiled(const Axf1ChunkIndexEntry& chunk, Axf1ChunkReadProfile& profile);

    [[nodiscard]] std::expected<Axf1Chunk, std::string>
    read_chunk_columns(const Axf1ChunkIndexEntry& chunk,
                       const std::vector<Axf1ColumnId>& columns);

    [[nodiscard]] std::expected<Axf1Chunk, std::string>
    read_chunk_columns_profiled(const Axf1ChunkIndexEntry& chunk,
                                const std::vector<Axf1ColumnId>& columns,
                                Axf1ChunkReadProfile& profile);

    [[nodiscard]] std::expected<Axf1Chunk, std::string>
    read_chunk_columns_selective(const Axf1ChunkIndexEntry& chunk,
                                const std::vector<Axf1ColumnId>& columns,
                                Axf1ChunkReadProfile& profile,
                                const std::string* ref_seq = nullptr);

    [[nodiscard]] std::expected<std::vector<unsigned char>, std::string>
    read_chunk_raw(const Axf1ChunkIndexEntry& chunk);

    [[nodiscard]] static std::expected<Axf1Chunk, std::string>
    decode_chunk_raw(const std::vector<unsigned char>& chunk_bytes,
                     const Axf1ChunkIndexEntry& chunk,
                     const std::vector<Axf1ColumnId>& columns,
                     const std::string* ref_seq = nullptr);

    [[nodiscard]] static std::expected<Axf1Chunk, std::string>
    decode_chunk_mapped(const unsigned char* data, std::uint64_t length,
                        const Axf1ChunkIndexEntry& chunk,
                        const std::vector<Axf1ColumnId>& columns,
                        const std::string* ref_seq = nullptr);

    struct FusedDecodeResult {
        std::string sam_output;
        std::size_t records_formatted = 0;
    };

    [[nodiscard]] static std::expected<FusedDecodeResult, std::string>
    decode_chunk_to_sam_mapped(const unsigned char* data, std::uint64_t length,
                              const Axf1ChunkIndexEntry& chunk,
                              const std::string& ref_name,
                              bool is_interior_chunk,
                              std::int32_t region_start, std::int32_t region_end,
                              const std::string* ref_seq = nullptr);

    [[nodiscard]] static std::expected<std::size_t, std::string>
    decode_chunk_to_sam_append(const unsigned char* data, std::uint64_t length,
                              const Axf1ChunkIndexEntry& chunk,
                              const std::string& ref_name,
                              bool is_interior_chunk,
                              std::int32_t region_start, std::int32_t region_end,
                              std::string& output,
                              const std::string* ref_seq = nullptr);

    struct FilteredAppendResult {
        std::size_t records_scanned = 0;
        std::size_t records_matched = 0;
        std::size_t records_filtered = 0;
    };

    [[nodiscard]] static std::expected<FilteredAppendResult, std::string>
    decode_chunk_to_sam_append_filtered(const unsigned char* data, std::uint64_t length,
                                       const Axf1ChunkIndexEntry& chunk,
                                       const std::string& ref_name,
                                       bool is_interior_chunk,
                                       std::int32_t region_start, std::int32_t region_end,
                                       std::uint16_t flag_exclude, std::uint8_t min_mapq,
                                       std::string& output,
                                       const std::string* ref_seq = nullptr);

    [[nodiscard]] const unsigned char* mapped_data() const noexcept;
    [[nodiscard]] std::uint64_t file_size() const noexcept;

private:
    Axf1FileReader(std::filesystem::path path, Axf1FileIndex index,
                   std::unique_ptr<std::ifstream> stream, std::uint64_t file_size,
                   const unsigned char* mmap_ptr);

    [[nodiscard]] std::expected<std::vector<unsigned char>, std::string>
    read_range(std::uint64_t offset, std::uint64_t length);

    std::filesystem::path path_;
    Axf1FileIndex index_;
    std::unique_ptr<std::ifstream> stream_;
    std::uint64_t file_size_ = 0;
    const unsigned char* mmap_ptr_ = nullptr;
    int mmap_fd_ = -1;
};

[[nodiscard]] std::expected<void, std::string> write_axf1_file(const Axf1File& file,
                                                               const std::filesystem::path& path);

[[nodiscard]] std::expected<void, std::string> write_axf1_file(const Axf1File& file,
                                                               const std::filesystem::path& path,
                                                               const Axf1WriteOptions& options);

[[nodiscard]] std::expected<Axf1File, std::string>
read_axf1_file(const std::filesystem::path& path);

[[nodiscard]] std::expected<Axf1FileIndex, std::string>
read_axf1_index_metadata(const std::filesystem::path& path);

[[nodiscard]] std::expected<Axf1Chunk, std::string>
read_axf1_chunk(const std::filesystem::path& path, const Axf1ChunkIndexEntry& chunk);

[[nodiscard]] std::expected<Axf1Chunk, std::string>
read_axf1_chunk_profiled(const std::filesystem::path& path, const Axf1ChunkIndexEntry& chunk,
                         Axf1ChunkReadProfile& profile);

[[nodiscard]] std::expected<Axf1Chunk, std::string>
read_axf1_chunk_columns(const std::filesystem::path& path, const Axf1ChunkIndexEntry& chunk,
                        const std::vector<Axf1ColumnId>& columns);

[[nodiscard]] std::expected<Axf1Chunk, std::string>
read_axf1_chunk_columns_profiled(const std::filesystem::path& path,
                                 const Axf1ChunkIndexEntry& chunk,
                                 const std::vector<Axf1ColumnId>& columns,
                                 Axf1ChunkReadProfile& profile);

[[nodiscard]] std::string format_axf1_sam_record(const Axf1Record& record,
                                                  const std::string& reference);

void append_axf1_sam_record(std::string& output, const Axf1Record& record,
                            const std::string& reference);

MetadataEntry make_ref_contig_sha256_entry(
    const std::vector<std::pair<std::uint32_t, std::array<unsigned char, 32>>>& checksums,
    std::uint8_t flags = 0);

MetadataEntry make_encode_reference_path_entry(std::string_view path);

std::vector<std::pair<std::uint32_t, std::array<unsigned char, 32>>>
parse_ref_contig_sha256_entry(const MetadataEntry& entry);

} // namespace alignx::format
