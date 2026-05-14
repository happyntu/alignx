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
constexpr std::uint32_t kVersion = 1;
constexpr std::uint64_t kHeaderSize = kMagic.size() + sizeof(std::uint32_t) +
                                      sizeof(std::uint32_t) + sizeof(std::uint64_t) +
                                      sizeof(std::uint64_t);
constexpr std::uint16_t kColumnEntrySize =
    sizeof(std::uint16_t) + sizeof(std::uint16_t) + sizeof(std::uint64_t) + sizeof(std::uint64_t);

struct ColumnEntry {
    Axf1ColumnId column_id = Axf1ColumnId::qname;
    Axf1CodecId codec_id = Axf1CodecId::raw;
    std::uint64_t offset = 0;
    std::uint64_t length = 0;
};

struct ChunkIndexEntry {
    std::uint32_t ref_id = 0;
    std::int32_t start_pos = -1;
    std::int32_t end_pos = -1;
    std::uint32_t record_count = 0;
    std::uint64_t chunk_offset = 0;
    std::uint64_t chunk_length = 0;
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
            return std::unexpected(size.error());
        }
        auto value = reader.read_string(*size);
        if (!value) {
            return std::unexpected(value.error());
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
            return std::unexpected(value.error());
        }
        values.push_back(*value);
    }
    if (reader.offset() != reader.size()) {
        return std::unexpected("AXF1 fixed column has trailing bytes");
    }
    return values;
}

bool has_column(const std::vector<ColumnEntry>& entries, Axf1ColumnId column_id) {
    return std::any_of(entries.begin(), entries.end(), [column_id](const ColumnEntry& entry) {
        return entry.column_id == column_id;
    });
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

std::expected<Axf1Chunk, std::string> read_chunk(const std::vector<unsigned char>& file_bytes,
                                                 const ChunkIndexEntry& index_entry) {
    if (index_entry.chunk_offset > file_bytes.size() ||
        index_entry.chunk_length >
            file_bytes.size() - static_cast<std::size_t>(index_entry.chunk_offset)) {
        return std::unexpected("AXF1 chunk points outside file");
    }

    const auto chunk_begin =
        file_bytes.begin() + static_cast<std::ptrdiff_t>(index_entry.chunk_offset);
    const auto chunk_end = chunk_begin + static_cast<std::ptrdiff_t>(index_entry.chunk_length);
    const std::vector<unsigned char> chunk_bytes(chunk_begin, chunk_end);
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

    for (Axf1ColumnId required :
         {Axf1ColumnId::qname, Axf1ColumnId::flag, Axf1ColumnId::pos, Axf1ColumnId::mapq,
          Axf1ColumnId::cigar, Axf1ColumnId::mate_reference, Axf1ColumnId::mate_pos,
          Axf1ColumnId::template_length, Axf1ColumnId::sequence, Axf1ColumnId::quality,
          Axf1ColumnId::tags}) {
        if (!has_column(entries, required)) {
            return std::unexpected("AXF1 chunk is missing required column");
        }
    }

    const std::size_t payload_start = static_cast<std::size_t>(reader.offset());
    const std::vector<unsigned char> chunk_payload(
        chunk_bytes.begin() + static_cast<std::ptrdiff_t>(payload_start), chunk_bytes.end());

    Axf1Chunk chunk{.ref_id = *ref_id, .start_pos = *start_pos, .end_pos = *end_pos};
    chunk.records.resize(*record_count);

    for (const ColumnEntry& entry : entries) {
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

    std::vector<unsigned char> chunk_bytes;
    std::vector<ChunkIndexEntry> index_entries;
    std::uint64_t chunk_offset = kHeaderSize + reference_bytes.size();
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
    output.insert(output.end(), chunk_bytes.begin(), chunk_bytes.end());
    for (const ChunkIndexEntry& entry : index_entries) {
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
    if (*version != kVersion) {
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

    auto seek = reader.seek(*index_offset);
    if (!seek) {
        return std::unexpected(seek.error());
    }
    std::vector<ChunkIndexEntry> index_entries;
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
    for (const ChunkIndexEntry& entry : index_entries) {
        auto chunk = read_chunk(*bytes, entry);
        if (!chunk) {
            return std::unexpected(chunk.error());
        }
        file.chunks.push_back(std::move(*chunk));
    }
    return file;
}

} // namespace alignx::format
