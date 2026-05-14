#include "format/axf1_file.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <limits>
#include <string_view>
#include <utility>

namespace alignx::format {
namespace {

constexpr std::array<unsigned char, 4> kMagic{'A', 'X', 'F', '1'};
constexpr std::uint32_t kVersionV1 = 1;
constexpr std::uint32_t kVersion = 2;
constexpr std::uint64_t kHeaderSize = kMagic.size() + sizeof(std::uint32_t) +
                                      sizeof(std::uint32_t) + sizeof(std::uint64_t) +
                                      sizeof(std::uint64_t);
constexpr std::uint16_t kColumnEntrySize =
    sizeof(std::uint16_t) + sizeof(std::uint16_t) + sizeof(std::uint64_t) + sizeof(std::uint64_t);
constexpr std::array<Axf1ColumnId, 11> kRequiredColumns{
    Axf1ColumnId::qname,    Axf1ColumnId::flag,
    Axf1ColumnId::pos,      Axf1ColumnId::mapq,
    Axf1ColumnId::cigar,    Axf1ColumnId::mate_reference,
    Axf1ColumnId::mate_pos, Axf1ColumnId::template_length,
    Axf1ColumnId::sequence, Axf1ColumnId::quality,
    Axf1ColumnId::tags};

struct ColumnEntry {
    Axf1ColumnId column_id = Axf1ColumnId::qname;
    Axf1CodecId codec_id = Axf1CodecId::raw;
    std::uint64_t offset = 0;
    std::uint64_t length = 0;
};

void append_u8(std::vector<unsigned char>& bytes, std::uint8_t value) {
    bytes.push_back(value);
}

void append_u16(std::vector<unsigned char>& bytes, std::uint16_t value) {
    for (std::size_t byte = 0; byte < sizeof(value); ++byte) {
        bytes.push_back(static_cast<unsigned char>((value >> (byte * 8U)) & 0xFFU));
    }
}

void append_u32(std::vector<unsigned char>& bytes, std::uint32_t value) {
    for (std::size_t byte = 0; byte < sizeof(value); ++byte) {
        bytes.push_back(static_cast<unsigned char>((value >> (byte * 8U)) & 0xFFU));
    }
}

void append_i32(std::vector<unsigned char>& bytes, std::int32_t value) {
    append_u32(bytes, static_cast<std::uint32_t>(value));
}

void append_u64(std::vector<unsigned char>& bytes, std::uint64_t value) {
    for (std::size_t byte = 0; byte < sizeof(value); ++byte) {
        bytes.push_back(static_cast<unsigned char>((value >> (byte * 8U)) & 0xFFU));
    }
}

void append_string(std::vector<unsigned char>& bytes, std::string_view value) {
    append_u32(bytes, static_cast<std::uint32_t>(value.size()));
    bytes.insert(bytes.end(), value.begin(), value.end());
}

void append_metadata_string(std::vector<unsigned char>& bytes, std::string_view value) {
    append_string(bytes, value);
}

class Reader {
public:
    explicit Reader(const std::vector<unsigned char>& bytes) : bytes_(bytes) {}

    [[nodiscard]] std::expected<void, std::string> expect_magic() {
        if (remaining() < kMagic.size()) {
            return std::unexpected("truncated AXF1 file");
        }
        for (std::size_t index = 0; index < kMagic.size(); ++index) {
            if (bytes_.at(index) != kMagic.at(index)) {
                return std::unexpected("invalid AXF1 magic");
            }
        }
        offset_ = kMagic.size();
        return {};
    }

    [[nodiscard]] std::expected<std::uint8_t, std::string> read_u8() {
        if (remaining() < 1) {
            return std::unexpected("truncated AXF1 file");
        }
        return bytes_.at(offset_++);
    }

    [[nodiscard]] std::expected<std::uint16_t, std::string> read_u16() {
        return read_little_endian<std::uint16_t>();
    }

    [[nodiscard]] std::expected<std::uint32_t, std::string> read_u32() {
        return read_little_endian<std::uint32_t>();
    }

    [[nodiscard]] std::expected<std::int32_t, std::string> read_i32() {
        auto value = read_u32();
        if (!value) {
            return std::unexpected(value.error());
        }
        return static_cast<std::int32_t>(*value);
    }

    [[nodiscard]] std::expected<std::uint64_t, std::string> read_u64() {
        return read_little_endian<std::uint64_t>();
    }

    [[nodiscard]] std::expected<std::string, std::string> read_string(std::size_t size) {
        if (remaining() < size) {
            return std::unexpected("truncated AXF1 file");
        }
        std::string value(reinterpret_cast<const char*>(bytes_.data() + offset_), size);
        offset_ += size;
        return value;
    }

    [[nodiscard]] std::expected<void, std::string> seek(std::uint64_t offset) {
        if (offset > bytes_.size()) {
            return std::unexpected("AXF1 offset points outside file");
        }
        offset_ = static_cast<std::size_t>(offset);
        return {};
    }

    [[nodiscard]] std::uint64_t offset() const noexcept { return offset_; }
    [[nodiscard]] std::uint64_t size() const noexcept { return bytes_.size(); }

private:
    [[nodiscard]] std::size_t remaining() const noexcept {
        return offset_ > bytes_.size() ? 0 : bytes_.size() - offset_;
    }

    template <typename UInt>
    [[nodiscard]] std::expected<UInt, std::string> read_little_endian() {
        if (remaining() < sizeof(UInt)) {
            return std::unexpected("truncated AXF1 file");
        }
        UInt value = 0;
        for (std::size_t byte = 0; byte < sizeof(UInt); ++byte) {
            value |= static_cast<UInt>(bytes_.at(offset_ + byte)) << (byte * 8U);
        }
        offset_ += sizeof(UInt);
        return value;
    }

    const std::vector<unsigned char>& bytes_;
    std::size_t offset_ = 0;
};

class StreamReader {
public:
    StreamReader(std::ifstream& input, std::uint64_t file_size)
        : input_(input), file_size_(file_size) {}

    [[nodiscard]] std::expected<void, std::string> expect_magic() {
        unsigned char bytes[kMagic.size()]{};
        auto read = read_exact(bytes, sizeof(bytes));
        if (!read) {
            return std::unexpected(read.error());
        }
        for (std::size_t index = 0; index < kMagic.size(); ++index) {
            if (bytes[index] != kMagic.at(index)) {
                return std::unexpected("invalid AXF1 magic");
            }
        }
        return {};
    }

    [[nodiscard]] std::expected<std::uint8_t, std::string> read_u8() {
        unsigned char byte{};
        auto read = read_exact(&byte, sizeof(byte));
        if (!read) {
            return std::unexpected(read.error());
        }
        return byte;
    }

    [[nodiscard]] std::expected<std::uint16_t, std::string> read_u16() {
        return read_little_endian<std::uint16_t>();
    }

    [[nodiscard]] std::expected<std::uint32_t, std::string> read_u32() {
        return read_little_endian<std::uint32_t>();
    }

    [[nodiscard]] std::expected<std::int32_t, std::string> read_i32() {
        auto value = read_u32();
        if (!value) {
            return std::unexpected(value.error());
        }
        return static_cast<std::int32_t>(*value);
    }

    [[nodiscard]] std::expected<std::uint64_t, std::string> read_u64() {
        return read_little_endian<std::uint64_t>();
    }

    [[nodiscard]] std::expected<std::string, std::string> read_string(std::size_t size) {
        if (size > remaining()) {
            return std::unexpected("truncated AXF1 file");
        }
        std::string value(size, '\0');
        auto read = read_exact(reinterpret_cast<unsigned char*>(value.data()), size);
        if (!read) {
            return std::unexpected(read.error());
        }
        return value;
    }

    [[nodiscard]] std::expected<void, std::string> seek(std::uint64_t offset) {
        if (offset > file_size_) {
            return std::unexpected("AXF1 offset points outside file");
        }
        input_.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
        if (!input_) {
            return std::unexpected("failed to seek AXF1 file");
        }
        offset_ = offset;
        return {};
    }

    [[nodiscard]] std::uint64_t offset() const noexcept { return offset_; }
    [[nodiscard]] std::uint64_t size() const noexcept { return file_size_; }

private:
    [[nodiscard]] std::uint64_t remaining() const noexcept {
        return offset_ > file_size_ ? 0 : file_size_ - offset_;
    }

    [[nodiscard]] std::expected<void, std::string> read_exact(unsigned char* output,
                                                              std::size_t size) {
        if (size > remaining()) {
            return std::unexpected("truncated AXF1 file");
        }
        input_.read(reinterpret_cast<char*>(output), static_cast<std::streamsize>(size));
        if (!input_) {
            return std::unexpected("failed to read AXF1 file");
        }
        offset_ += size;
        return {};
    }

    template <typename UInt>
    [[nodiscard]] std::expected<UInt, std::string> read_little_endian() {
        unsigned char bytes[sizeof(UInt)]{};
        auto read = read_exact(bytes, sizeof(bytes));
        if (!read) {
            return std::unexpected(read.error());
        }
        UInt value = 0;
        for (std::size_t byte = 0; byte < sizeof(UInt); ++byte) {
            value |= static_cast<UInt>(bytes[byte]) << (byte * 8U);
        }
        return value;
    }

    std::ifstream& input_;
    std::uint64_t file_size_ = 0;
    std::uint64_t offset_ = 0;
};

std::expected<std::vector<unsigned char>, std::string>
read_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return std::unexpected("failed to open AXF1 file: " + path.string());
    }
    input.seekg(0, std::ios::end);
    const std::streamoff size = input.tellg();
    if (size < 0) {
        return std::unexpected("failed to determine AXF1 file size: " + path.string());
    }
    input.seekg(0, std::ios::beg);

    std::vector<unsigned char> bytes(static_cast<std::size_t>(size));
    if (!bytes.empty()) {
        input.read(reinterpret_cast<char*>(bytes.data()), size);
        if (!input) {
            return std::unexpected("failed to read AXF1 file: " + path.string());
        }
    }
    return bytes;
}

std::expected<std::uint64_t, std::string> axf1_file_size(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return std::unexpected("failed to open AXF1 file: " + path.string());
    }
    input.seekg(0, std::ios::end);
    const std::streamoff size = input.tellg();
    if (size < 0) {
        return std::unexpected("failed to determine AXF1 file size: " + path.string());
    }
    return static_cast<std::uint64_t>(size);
}

std::expected<std::vector<unsigned char>, std::string>
read_file_range(const std::filesystem::path& path, std::uint64_t offset, std::uint64_t length) {
    auto size = axf1_file_size(path);
    if (!size) {
        return std::unexpected(size.error());
    }
    if (offset > *size || length > *size - offset) {
        return std::unexpected("AXF1 chunk points outside file");
    }
    if (length > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        return std::unexpected("AXF1 chunk is too large");
    }

    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return std::unexpected("failed to open AXF1 file: " + path.string());
    }
    input.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    if (!input) {
        return std::unexpected("failed to seek AXF1 file");
    }

    std::vector<unsigned char> bytes(static_cast<std::size_t>(length));
    if (!bytes.empty()) {
        input.read(reinterpret_cast<char*>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size()));
        if (!input) {
            return std::unexpected("failed to read AXF1 file: " + path.string());
        }
    }
    return bytes;
}

std::expected<void, std::string> write_file(const std::filesystem::path& path,
                                            const std::vector<unsigned char>& bytes) {
    std::ofstream output(path, std::ios::binary);
    if (!output) {
        return std::unexpected("failed to open AXF1 file for writing: " + path.string());
    }
    output.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    if (!output) {
        return std::unexpected("failed to write AXF1 file: " + path.string());
    }
    return {};
}

std::expected<void, std::string> validate_file(const Axf1File& file) {
    if (file.references.size() > std::numeric_limits<std::uint32_t>::max()) {
        return std::unexpected("too many AXF1 references");
    }
    if (file.chunks.size() > std::numeric_limits<std::uint64_t>::max()) {
        return std::unexpected("too many AXF1 chunks");
    }
    for (const Axf1Reference& reference : file.references) {
        if (reference.name.size() > std::numeric_limits<std::uint16_t>::max()) {
            return std::unexpected("AXF1 reference name is too long");
        }
    }
    if (file.metadata.source_path.size() > std::numeric_limits<std::uint32_t>::max() ||
        file.metadata.conversion_region.size() > std::numeric_limits<std::uint32_t>::max()) {
        return std::unexpected("AXF1 metadata string is too long");
    }
    for (const Axf1Chunk& chunk : file.chunks) {
        if (chunk.ref_id >= file.references.size()) {
            return std::unexpected("AXF1 chunk reference id out of range");
        }
        if (chunk.start_pos >= chunk.end_pos) {
            return std::unexpected("AXF1 chunk requires start_pos < end_pos");
        }
        if (chunk.records.size() > std::numeric_limits<std::uint32_t>::max()) {
            return std::unexpected("too many AXF1 records in chunk");
        }
        for (const Axf1Record& record : chunk.records) {
            if (record.pos < chunk.start_pos || record.pos >= chunk.end_pos) {
                return std::unexpected("AXF1 record POS is outside chunk interval");
            }
            for (std::string_view value :
                 {std::string_view(record.qname), std::string_view(record.cigar),
                  std::string_view(record.mate_reference), std::string_view(record.sequence),
                  std::string_view(record.quality), std::string_view(record.tags)}) {
                if (value.size() > std::numeric_limits<std::uint32_t>::max()) {
                    return std::unexpected("AXF1 string column value is too long");
                }
            }
        }
    }
    return {};
}

std::vector<unsigned char> encode_file_metadata(const Axf1FileMetadata& metadata) {
    std::vector<unsigned char> bytes;
    append_u8(bytes, metadata.is_subset ? 1 : 0);
    append_metadata_string(bytes, metadata.source_path);
    append_metadata_string(bytes, metadata.conversion_region);
    return bytes;
}

std::expected<Axf1FileMetadata, std::string> read_file_metadata(Reader& reader) {
    auto is_subset = reader.read_u8();
    if (!is_subset) {
        return std::unexpected("truncated AXF1 metadata");
    }
    if (*is_subset > 1) {
        return std::unexpected("invalid AXF1 subset metadata flag");
    }

    auto source_path_size = reader.read_u32();
    if (!source_path_size) {
        return std::unexpected("truncated AXF1 metadata");
    }
    auto source_path = reader.read_string(*source_path_size);
    if (!source_path) {
        return std::unexpected("truncated AXF1 metadata");
    }

    auto conversion_region_size = reader.read_u32();
    if (!conversion_region_size) {
        return std::unexpected("truncated AXF1 metadata");
    }
    auto conversion_region = reader.read_string(*conversion_region_size);
    if (!conversion_region) {
        return std::unexpected("truncated AXF1 metadata");
    }

    return Axf1FileMetadata{.source_path = std::move(*source_path),
                            .conversion_region = std::move(*conversion_region),
                            .is_subset = *is_subset == 1};
}

std::expected<Axf1FileIndexMetadata, std::string> read_file_index_metadata(StreamReader& reader) {
    auto is_subset = reader.read_u8();
    if (!is_subset) {
        return std::unexpected("truncated AXF1 metadata");
    }
    if (*is_subset > 1) {
        return std::unexpected("invalid AXF1 subset metadata flag");
    }

    auto source_path_size = reader.read_u32();
    if (!source_path_size) {
        return std::unexpected("truncated AXF1 metadata");
    }
    auto source_path = reader.read_string(*source_path_size);
    if (!source_path) {
        return std::unexpected("truncated AXF1 metadata");
    }

    auto conversion_region_size = reader.read_u32();
    if (!conversion_region_size) {
        return std::unexpected("truncated AXF1 metadata");
    }
    auto conversion_region = reader.read_string(*conversion_region_size);
    if (!conversion_region) {
        return std::unexpected("truncated AXF1 metadata");
    }

    return Axf1FileIndexMetadata{.source_path = std::move(*source_path),
                                 .conversion_region = std::move(*conversion_region),
                                 .is_subset = *is_subset == 1};
}

std::vector<unsigned char> encode_strings(const std::vector<Axf1Record>& records,
                                          std::string Axf1Record::* field) {
    std::vector<unsigned char> bytes;
    for (const Axf1Record& record : records) {
        append_string(bytes, record.*field);
    }
    return bytes;
}

std::vector<unsigned char> encode_u16(const std::vector<Axf1Record>& records,
                                      std::uint16_t Axf1Record::* field) {
    std::vector<unsigned char> bytes;
    for (const Axf1Record& record : records) {
        append_u16(bytes, record.*field);
    }
    return bytes;
}

std::vector<unsigned char> encode_i32(const std::vector<Axf1Record>& records,
                                      std::int32_t Axf1Record::* field) {
    std::vector<unsigned char> bytes;
    for (const Axf1Record& record : records) {
        append_i32(bytes, record.*field);
    }
    return bytes;
}

std::vector<unsigned char> encode_u8(const std::vector<Axf1Record>& records,
                                     std::uint8_t Axf1Record::* field) {
    std::vector<unsigned char> bytes;
    for (const Axf1Record& record : records) {
        append_u8(bytes, record.*field);
    }
    return bytes;
}

std::vector<std::pair<Axf1ColumnId, std::vector<unsigned char>>>
encode_columns(const Axf1Chunk& chunk) {
    return {
        {Axf1ColumnId::qname, encode_strings(chunk.records, &Axf1Record::qname)},
        {Axf1ColumnId::flag, encode_u16(chunk.records, &Axf1Record::flag)},
        {Axf1ColumnId::pos, encode_i32(chunk.records, &Axf1Record::pos)},
        {Axf1ColumnId::mapq, encode_u8(chunk.records, &Axf1Record::mapq)},
        {Axf1ColumnId::cigar, encode_strings(chunk.records, &Axf1Record::cigar)},
        {Axf1ColumnId::mate_reference, encode_strings(chunk.records, &Axf1Record::mate_reference)},
        {Axf1ColumnId::mate_pos, encode_i32(chunk.records, &Axf1Record::mate_pos)},
        {Axf1ColumnId::template_length, encode_i32(chunk.records, &Axf1Record::template_length)},
        {Axf1ColumnId::sequence, encode_strings(chunk.records, &Axf1Record::sequence)},
        {Axf1ColumnId::quality, encode_strings(chunk.records, &Axf1Record::quality)},
        {Axf1ColumnId::tags, encode_strings(chunk.records, &Axf1Record::tags)}};
}

std::vector<unsigned char> write_chunk(const Axf1Chunk& chunk) {
    const auto encoded_columns = encode_columns(chunk);
    std::vector<ColumnEntry> entries;
    entries.reserve(encoded_columns.size());

    std::uint64_t payload_offset = 0;
    for (const auto& [column_id, payload] : encoded_columns) {
        entries.push_back({.column_id = column_id,
                           .codec_id = Axf1CodecId::raw,
                           .offset = payload_offset,
                           .length = static_cast<std::uint64_t>(payload.size())});
        payload_offset += payload.size();
    }

    std::vector<unsigned char> bytes;
    append_u32(bytes, chunk.ref_id);
    append_i32(bytes, chunk.start_pos);
    append_i32(bytes, chunk.end_pos);
    append_u32(bytes, static_cast<std::uint32_t>(chunk.records.size()));
    append_u16(bytes, static_cast<std::uint16_t>(entries.size()));
    for (const ColumnEntry& entry : entries) {
        append_u16(bytes, static_cast<std::uint16_t>(entry.column_id));
        append_u16(bytes, static_cast<std::uint16_t>(entry.codec_id));
        append_u64(bytes, entry.offset);
        append_u64(bytes, entry.length);
    }
    for (const auto& [unused_column_id, payload] : encoded_columns) {
        (void)unused_column_id;
        bytes.insert(bytes.end(), payload.begin(), payload.end());
    }
    return bytes;
}

std::expected<std::vector<std::string>, std::string>
decode_string_column(const std::vector<unsigned char>& bytes, std::uint32_t record_count) {
    Reader reader(bytes);
    std::vector<std::string> values;
    values.reserve(record_count);
    for (std::uint32_t index = 0; index < record_count; ++index) {
        auto size = reader.read_u32();
        if (!size) {
            return std::unexpected("AXF1 column value count mismatch");
        }
        auto value = reader.read_string(*size);
        if (!value) {
            return std::unexpected("AXF1 string column value is truncated");
        }
        values.push_back(std::move(*value));
    }
    if (reader.offset() != reader.size()) {
        return std::unexpected("AXF1 string column has trailing bytes");
    }
    return values;
}

template <typename Value>
std::expected<std::vector<Value>, std::string>
decode_fixed_column(const std::vector<unsigned char>& bytes, std::uint32_t record_count,
                    std::expected<Value, std::string> (Reader::*read_value)()) {
    Reader reader(bytes);
    std::vector<Value> values;
    values.reserve(record_count);
    for (std::uint32_t index = 0; index < record_count; ++index) {
        auto value = (reader.*read_value)();
        if (!value) {
            return std::unexpected("AXF1 column value count mismatch");
        }
        values.push_back(*value);
    }
    if (reader.offset() != reader.size()) {
        return std::unexpected("AXF1 fixed column has trailing bytes");
    }
    return values;
}

bool is_known_column(Axf1ColumnId column_id) {
    return std::find(kRequiredColumns.begin(), kRequiredColumns.end(), column_id) !=
           kRequiredColumns.end();
}

bool contains_column(const std::vector<Axf1ColumnId>& columns, Axf1ColumnId column_id) {
    return std::find(columns.begin(), columns.end(), column_id) != columns.end();
}

std::expected<void, std::string> validate_column_entries(const std::vector<ColumnEntry>& entries) {
    for (const ColumnEntry& entry : entries) {
        if (!is_known_column(entry.column_id)) {
            return std::unexpected("unknown AXF1 column id");
        }
    }

    for (Axf1ColumnId required : kRequiredColumns) {
        const auto count =
            std::count_if(entries.begin(), entries.end(), [required](const ColumnEntry& entry) {
                return entry.column_id == required;
            });
        if (count == 0) {
            return std::unexpected("AXF1 chunk is missing required column");
        }
        if (count > 1) {
            return std::unexpected("duplicate AXF1 required column");
        }
    }
    return {};
}

std::expected<void, std::string>
validate_selected_column_entries(const std::vector<ColumnEntry>& entries,
                                 const std::vector<Axf1ColumnId>& requested_columns) {
    for (const ColumnEntry& entry : entries) {
        if (!is_known_column(entry.column_id)) {
            return std::unexpected("unknown AXF1 column id");
        }
    }

    for (Axf1ColumnId requested : requested_columns) {
        const auto count =
            std::count_if(entries.begin(), entries.end(), [requested](const ColumnEntry& entry) {
                return entry.column_id == requested;
            });
        if (count == 0) {
            return std::unexpected("AXF1 chunk is missing requested column");
        }
        if (count > 1) {
            return std::unexpected("duplicate AXF1 requested column");
        }
    }
    return {};
}

std::expected<std::vector<unsigned char>, std::string>
slice_column_payload(const std::vector<unsigned char>& chunk_payload, const ColumnEntry& entry) {
    if (entry.offset > chunk_payload.size() ||
        entry.length > chunk_payload.size() - static_cast<std::size_t>(entry.offset)) {
        return std::unexpected("AXF1 column payload points outside chunk");
    }
    const auto begin = chunk_payload.begin() + static_cast<std::ptrdiff_t>(entry.offset);
    const auto end = begin + static_cast<std::ptrdiff_t>(entry.length);
    return std::vector<unsigned char>(begin, end);
}

std::expected<Axf1Chunk, std::string>
decode_chunk_bytes(const std::vector<unsigned char>& chunk_bytes,
                   const Axf1ChunkIndexEntry& index_entry,
                   const std::vector<Axf1ColumnId>& requested_columns) {
    Reader reader(chunk_bytes);

    auto ref_id = reader.read_u32();
    auto start_pos = reader.read_i32();
    auto end_pos = reader.read_i32();
    auto record_count = reader.read_u32();
    auto column_count = reader.read_u16();
    if (!ref_id || !start_pos || !end_pos || !record_count || !column_count) {
        return std::unexpected("truncated AXF1 chunk");
    }
    if (*ref_id != index_entry.ref_id || *start_pos != index_entry.start_pos ||
        *end_pos != index_entry.end_pos || *record_count != index_entry.record_count) {
        return std::unexpected("AXF1 chunk metadata does not match index");
    }

    std::vector<ColumnEntry> entries;
    entries.reserve(*column_count);
    for (std::uint16_t index = 0; index < *column_count; ++index) {
        auto column_id = reader.read_u16();
        auto codec_id = reader.read_u16();
        auto offset = reader.read_u64();
        auto length = reader.read_u64();
        if (!column_id || !codec_id || !offset || !length) {
            return std::unexpected("truncated AXF1 column entry");
        }
        if (*codec_id != static_cast<std::uint16_t>(Axf1CodecId::raw)) {
            return std::unexpected("unsupported AXF1 column codec");
        }
        entries.push_back({.column_id = static_cast<Axf1ColumnId>(*column_id),
                           .codec_id = static_cast<Axf1CodecId>(*codec_id),
                           .offset = *offset,
                           .length = *length});
    }

    auto column_validation = requested_columns.size() == kRequiredColumns.size()
                                 ? validate_column_entries(entries)
                                 : validate_selected_column_entries(entries, requested_columns);
    if (!column_validation) {
        return std::unexpected(column_validation.error());
    }

    const std::size_t payload_start = static_cast<std::size_t>(reader.offset());
    const std::vector<unsigned char> chunk_payload(
        chunk_bytes.begin() + static_cast<std::ptrdiff_t>(payload_start), chunk_bytes.end());

    Axf1Chunk chunk{.ref_id = *ref_id, .start_pos = *start_pos, .end_pos = *end_pos};
    chunk.records.resize(*record_count);

    for (const ColumnEntry& entry : entries) {
        if (!contains_column(requested_columns, entry.column_id)) {
            continue;
        }
        auto payload = slice_column_payload(chunk_payload, entry);
        if (!payload) {
            return std::unexpected(payload.error());
        }
        switch (entry.column_id) {
        case Axf1ColumnId::qname: {
            auto values = decode_string_column(*payload, *record_count);
            if (!values) {
                return std::unexpected(values.error());
            }
            for (std::uint32_t index = 0; index < *record_count; ++index) {
                chunk.records[index].qname = std::move(values->at(index));
            }
            break;
        }
        case Axf1ColumnId::flag: {
            auto values =
                decode_fixed_column<std::uint16_t>(*payload, *record_count, &Reader::read_u16);
            if (!values) {
                return std::unexpected(values.error());
            }
            for (std::uint32_t index = 0; index < *record_count; ++index) {
                chunk.records[index].flag = values->at(index);
            }
            break;
        }
        case Axf1ColumnId::pos: {
            auto values =
                decode_fixed_column<std::int32_t>(*payload, *record_count, &Reader::read_i32);
            if (!values) {
                return std::unexpected(values.error());
            }
            for (std::uint32_t index = 0; index < *record_count; ++index) {
                chunk.records[index].pos = values->at(index);
            }
            break;
        }
        case Axf1ColumnId::mapq: {
            auto values =
                decode_fixed_column<std::uint8_t>(*payload, *record_count, &Reader::read_u8);
            if (!values) {
                return std::unexpected(values.error());
            }
            for (std::uint32_t index = 0; index < *record_count; ++index) {
                chunk.records[index].mapq = values->at(index);
            }
            break;
        }
        case Axf1ColumnId::cigar:
        case Axf1ColumnId::mate_reference:
        case Axf1ColumnId::sequence:
        case Axf1ColumnId::quality:
        case Axf1ColumnId::tags: {
            auto values = decode_string_column(*payload, *record_count);
            if (!values) {
                return std::unexpected(values.error());
            }
            for (std::uint32_t index = 0; index < *record_count; ++index) {
                if (entry.column_id == Axf1ColumnId::cigar) {
                    chunk.records[index].cigar = std::move(values->at(index));
                } else if (entry.column_id == Axf1ColumnId::mate_reference) {
                    chunk.records[index].mate_reference = std::move(values->at(index));
                } else if (entry.column_id == Axf1ColumnId::sequence) {
                    chunk.records[index].sequence = std::move(values->at(index));
                } else if (entry.column_id == Axf1ColumnId::quality) {
                    chunk.records[index].quality = std::move(values->at(index));
                } else {
                    chunk.records[index].tags = std::move(values->at(index));
                }
            }
            break;
        }
        case Axf1ColumnId::mate_pos:
        case Axf1ColumnId::template_length: {
            auto values =
                decode_fixed_column<std::int32_t>(*payload, *record_count, &Reader::read_i32);
            if (!values) {
                return std::unexpected(values.error());
            }
            for (std::uint32_t index = 0; index < *record_count; ++index) {
                if (entry.column_id == Axf1ColumnId::mate_pos) {
                    chunk.records[index].mate_pos = values->at(index);
                } else {
                    chunk.records[index].template_length = values->at(index);
                }
            }
            break;
        }
        }
    }

    return chunk;
}

} // namespace

bool Axf1ChunkIndexEntry::overlaps(std::int32_t query_start,
                                   std::int32_t query_end) const noexcept {
    return start_pos < query_end && query_start < end_pos;
}

std::expected<std::vector<const Axf1ChunkIndexEntry*>, std::string>
Axf1FileIndex::query_chunks(std::uint32_t ref_id, std::int32_t start, std::int32_t end) const {
    if (start >= end) {
        return std::unexpected("AXF1 query requires start < end");
    }
    if (ref_id >= references.size()) {
        return std::unexpected("AXF1 query reference id out of range");
    }

    std::vector<const Axf1ChunkIndexEntry*> hits;
    for (const Axf1ChunkIndexEntry& chunk : chunks) {
        if (chunk.ref_id == ref_id && chunk.overlaps(start, end)) {
            hits.push_back(&chunk);
        }
    }
    return hits;
}

Axf1FileReader::Axf1FileReader(std::filesystem::path path, Axf1FileIndex index)
    : path_(std::move(path)), index_(std::move(index)) {}

std::expected<Axf1FileReader, std::string> Axf1FileReader::open(std::filesystem::path path) {
    auto index = read_axf1_index_metadata(path);
    if (!index) {
        return std::unexpected(index.error());
    }
    return Axf1FileReader(std::move(path), std::move(*index));
}

const Axf1FileIndex& Axf1FileReader::index() const noexcept {
    return index_;
}

std::expected<std::vector<const Axf1ChunkIndexEntry*>, std::string>
Axf1FileReader::query_chunks(std::uint32_t ref_id, std::int32_t start, std::int32_t end) const {
    return index_.query_chunks(ref_id, start, end);
}

std::expected<Axf1Chunk, std::string>
Axf1FileReader::read_chunk(const Axf1ChunkIndexEntry& chunk) const {
    return read_axf1_chunk(path_, chunk);
}

std::expected<Axf1Chunk, std::string>
Axf1FileReader::read_chunk_columns(const Axf1ChunkIndexEntry& chunk,
                                   const std::vector<Axf1ColumnId>& columns) const {
    return read_axf1_chunk_columns(path_, chunk, columns);
}

std::expected<void, std::string> write_axf1_file(const Axf1File& file,
                                                 const std::filesystem::path& path) {
    auto validation = validate_file(file);
    if (!validation) {
        return std::unexpected(validation.error());
    }

    std::vector<unsigned char> reference_bytes;
    for (const Axf1Reference& reference : file.references) {
        append_u16(reference_bytes, static_cast<std::uint16_t>(reference.name.size()));
        reference_bytes.insert(reference_bytes.end(), reference.name.begin(), reference.name.end());
        append_u32(reference_bytes, reference.length);
    }
    const std::vector<unsigned char> metadata_bytes = encode_file_metadata(file.metadata);

    std::vector<unsigned char> chunk_bytes;
    std::vector<Axf1ChunkIndexEntry> index_entries;
    std::uint64_t chunk_offset = kHeaderSize + reference_bytes.size() + metadata_bytes.size();
    for (const Axf1Chunk& chunk : file.chunks) {
        std::vector<unsigned char> bytes = write_chunk(chunk);
        index_entries.push_back({.ref_id = chunk.ref_id,
                                 .start_pos = chunk.start_pos,
                                 .end_pos = chunk.end_pos,
                                 .record_count = static_cast<std::uint32_t>(chunk.records.size()),
                                 .chunk_offset = chunk_offset,
                                 .chunk_length = static_cast<std::uint64_t>(bytes.size())});
        chunk_bytes.insert(chunk_bytes.end(), bytes.begin(), bytes.end());
        chunk_offset += bytes.size();
    }
    const std::uint64_t index_offset = chunk_offset;

    std::vector<unsigned char> output;
    output.insert(output.end(), kMagic.begin(), kMagic.end());
    append_u32(output, kVersion);
    append_u32(output, static_cast<std::uint32_t>(file.references.size()));
    append_u64(output, static_cast<std::uint64_t>(file.chunks.size()));
    append_u64(output, index_offset);
    output.insert(output.end(), reference_bytes.begin(), reference_bytes.end());
    output.insert(output.end(), metadata_bytes.begin(), metadata_bytes.end());
    output.insert(output.end(), chunk_bytes.begin(), chunk_bytes.end());
    for (const Axf1ChunkIndexEntry& entry : index_entries) {
        append_u32(output, entry.ref_id);
        append_i32(output, entry.start_pos);
        append_i32(output, entry.end_pos);
        append_u32(output, entry.record_count);
        append_u64(output, entry.chunk_offset);
        append_u64(output, entry.chunk_length);
    }
    return write_file(path, output);
}

std::expected<Axf1File, std::string> read_axf1_file(const std::filesystem::path& path) {
    auto bytes = read_file(path);
    if (!bytes) {
        return std::unexpected(bytes.error());
    }

    Reader reader(*bytes);
    auto magic = reader.expect_magic();
    if (!magic) {
        return std::unexpected(magic.error());
    }
    auto version = reader.read_u32();
    auto ref_count = reader.read_u32();
    auto chunk_count = reader.read_u64();
    auto index_offset = reader.read_u64();
    if (!version || !ref_count || !chunk_count || !index_offset) {
        return std::unexpected("truncated AXF1 file");
    }
    if (*version != kVersionV1 && *version != kVersion) {
        return std::unexpected("unsupported AXF1 version");
    }
    if (*index_offset > reader.size()) {
        return std::unexpected("AXF1 index offset points outside file");
    }

    Axf1File file;
    file.references.reserve(*ref_count);
    for (std::uint32_t ref_id = 0; ref_id < *ref_count; ++ref_id) {
        auto name_length = reader.read_u16();
        if (!name_length) {
            return std::unexpected(name_length.error());
        }
        auto name = reader.read_string(*name_length);
        auto length = reader.read_u32();
        if (!name || !length) {
            return std::unexpected("truncated AXF1 file");
        }
        file.references.push_back({.name = *name, .length = *length});
    }
    if (reader.offset() > *index_offset) {
        return std::unexpected("AXF1 references overlap index");
    }
    if (*version == kVersion) {
        auto metadata = read_file_metadata(reader);
        if (!metadata) {
            return std::unexpected(metadata.error());
        }
        file.metadata = std::move(*metadata);
    }
    if (reader.offset() > *index_offset) {
        return std::unexpected("AXF1 metadata overlaps index");
    }

    auto seek = reader.seek(*index_offset);
    if (!seek) {
        return std::unexpected(seek.error());
    }
    std::vector<Axf1ChunkIndexEntry> index_entries;
    index_entries.reserve(static_cast<std::size_t>(*chunk_count));
    for (std::uint64_t index = 0; index < *chunk_count; ++index) {
        auto ref_id = reader.read_u32();
        auto start_pos = reader.read_i32();
        auto end_pos = reader.read_i32();
        auto record_count = reader.read_u32();
        auto chunk_offset = reader.read_u64();
        auto chunk_length = reader.read_u64();
        if (!ref_id || !start_pos || !end_pos || !record_count || !chunk_offset || !chunk_length) {
            return std::unexpected("truncated AXF1 file");
        }
        if (*ref_id >= file.references.size()) {
            return std::unexpected("AXF1 chunk reference id out of range");
        }
        if (*start_pos >= *end_pos) {
            return std::unexpected("AXF1 chunk requires start_pos < end_pos");
        }
        if (*chunk_offset > reader.size() || *chunk_length > reader.size() - *chunk_offset) {
            return std::unexpected("AXF1 chunk points outside file");
        }
        if (*chunk_offset + *chunk_length > *index_offset) {
            return std::unexpected("AXF1 chunk overlaps index");
        }
        index_entries.push_back({.ref_id = *ref_id,
                                 .start_pos = *start_pos,
                                 .end_pos = *end_pos,
                                 .record_count = *record_count,
                                 .chunk_offset = *chunk_offset,
                                 .chunk_length = *chunk_length});
    }
    if (reader.offset() != reader.size()) {
        return std::unexpected("unexpected trailing bytes in AXF1 file");
    }

    file.chunks.reserve(index_entries.size());
    for (const Axf1ChunkIndexEntry& entry : index_entries) {
        const auto chunk_begin = bytes->begin() + static_cast<std::ptrdiff_t>(entry.chunk_offset);
        const auto chunk_end = chunk_begin + static_cast<std::ptrdiff_t>(entry.chunk_length);
        const std::vector<unsigned char> chunk_bytes(chunk_begin, chunk_end);
        auto chunk = decode_chunk_bytes(
            chunk_bytes, entry,
            std::vector<Axf1ColumnId>(kRequiredColumns.begin(), kRequiredColumns.end()));
        if (!chunk) {
            return std::unexpected(chunk.error());
        }
        file.chunks.push_back(std::move(*chunk));
    }
    return file;
}

std::expected<Axf1FileIndex, std::string>
read_axf1_index_metadata(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return std::unexpected("failed to open AXF1 file: " + path.string());
    }
    input.seekg(0, std::ios::end);
    const std::streamoff size = input.tellg();
    if (size < 0) {
        return std::unexpected("failed to determine AXF1 file size: " + path.string());
    }
    input.seekg(0, std::ios::beg);

    StreamReader reader(input, static_cast<std::uint64_t>(size));
    auto magic = reader.expect_magic();
    if (!magic) {
        return std::unexpected(magic.error());
    }
    auto version = reader.read_u32();
    auto ref_count = reader.read_u32();
    auto chunk_count = reader.read_u64();
    auto index_offset = reader.read_u64();
    if (!version || !ref_count || !chunk_count || !index_offset) {
        return std::unexpected("truncated AXF1 file");
    }
    if (*version != kVersionV1 && *version != kVersion) {
        return std::unexpected("unsupported AXF1 version");
    }
    if (*index_offset > reader.size()) {
        return std::unexpected("AXF1 index offset points outside file");
    }

    Axf1FileIndex index;
    index.references.reserve(*ref_count);
    for (std::uint32_t ref_id = 0; ref_id < *ref_count; ++ref_id) {
        auto name_length = reader.read_u16();
        if (!name_length) {
            return std::unexpected(name_length.error());
        }
        auto name = reader.read_string(*name_length);
        auto length = reader.read_u32();
        if (!name || !length) {
            return std::unexpected("truncated AXF1 file");
        }
        index.references.push_back({.name = *name, .length = *length});
    }
    if (reader.offset() > *index_offset) {
        return std::unexpected("AXF1 references overlap index");
    }
    if (*version == kVersion) {
        auto metadata = read_file_index_metadata(reader);
        if (!metadata) {
            return std::unexpected(metadata.error());
        }
        index.metadata = std::move(*metadata);
    }
    if (reader.offset() > *index_offset) {
        return std::unexpected("AXF1 metadata overlaps index");
    }

    auto seek = reader.seek(*index_offset);
    if (!seek) {
        return std::unexpected(seek.error());
    }
    index.chunks.reserve(static_cast<std::size_t>(*chunk_count));
    for (std::uint64_t entry_index = 0; entry_index < *chunk_count; ++entry_index) {
        auto ref_id = reader.read_u32();
        auto start_pos = reader.read_i32();
        auto end_pos = reader.read_i32();
        auto record_count = reader.read_u32();
        auto chunk_offset = reader.read_u64();
        auto chunk_length = reader.read_u64();
        if (!ref_id || !start_pos || !end_pos || !record_count || !chunk_offset || !chunk_length) {
            return std::unexpected("truncated AXF1 file");
        }
        if (*ref_id >= index.references.size()) {
            return std::unexpected("AXF1 chunk reference id out of range");
        }
        if (*start_pos >= *end_pos) {
            return std::unexpected("AXF1 chunk requires start_pos < end_pos");
        }
        if (*chunk_offset > reader.size() || *chunk_length > reader.size() - *chunk_offset) {
            return std::unexpected("AXF1 chunk points outside file");
        }
        if (*chunk_offset + *chunk_length > *index_offset) {
            return std::unexpected("AXF1 chunk overlaps index");
        }
        index.chunks.push_back({.ref_id = *ref_id,
                                .start_pos = *start_pos,
                                .end_pos = *end_pos,
                                .record_count = *record_count,
                                .chunk_offset = *chunk_offset,
                                .chunk_length = *chunk_length});
    }
    if (reader.offset() != reader.size()) {
        return std::unexpected("unexpected trailing bytes in AXF1 file");
    }

    return index;
}

std::expected<Axf1Chunk, std::string> read_axf1_chunk(const std::filesystem::path& path,
                                                      const Axf1ChunkIndexEntry& chunk) {
    return read_axf1_chunk_columns(
        path, chunk, std::vector<Axf1ColumnId>(kRequiredColumns.begin(), kRequiredColumns.end()));
}

std::expected<Axf1Chunk, std::string>
read_axf1_chunk_columns(const std::filesystem::path& path, const Axf1ChunkIndexEntry& chunk,
                        const std::vector<Axf1ColumnId>& columns) {
    auto bytes = read_file_range(path, chunk.chunk_offset, chunk.chunk_length);
    if (!bytes) {
        return std::unexpected(bytes.error());
    }
    return decode_chunk_bytes(*bytes, chunk, columns);
}

} // namespace alignx::format
