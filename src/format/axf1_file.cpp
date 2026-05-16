#include "format/axf1_file.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <fstream>
#include <limits>
#include <map>
#include <string_view>
#include <utility>

#ifdef ALIGNX_HAVE_ZSTD
#include <zstd.h>
#endif

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

struct EncodedColumn {
    Axf1ColumnId column_id = Axf1ColumnId::qname;
    Axf1CodecId codec_id = Axf1CodecId::raw;
    std::vector<unsigned char> payload;
};

enum class Axf1CompressionId : std::uint64_t {
    stored = 0,
    zstd = 1,
    lz4 = 2,
};

struct DecodedPayloadEnvelope {
    Axf1CodecId base_codec_id = Axf1CodecId::raw;
    std::vector<unsigned char> payload;
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

void append_varint_u64(std::vector<unsigned char>& bytes, std::uint64_t value) {
    while (value >= 0x80U) {
        bytes.push_back(static_cast<unsigned char>((value & 0x7FU) | 0x80U));
        value >>= 7U;
    }
    bytes.push_back(static_cast<unsigned char>(value));
}

void append_zigzag_varint_i64(std::vector<unsigned char>& bytes, std::int64_t value) {
    const auto encoded = static_cast<std::uint64_t>(
        (value << 1) ^ (value >> 63));
    append_varint_u64(bytes, encoded);
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

    [[nodiscard]] std::expected<std::uint64_t, std::string> read_varint_u64() {
        std::uint64_t value = 0;
        for (std::size_t byte_index = 0; byte_index < 10; ++byte_index) {
            auto byte = read_u8();
            if (!byte) {
                return std::unexpected("truncated AXF1 varint");
            }
            if (byte_index == 9 && (*byte & 0xFEU) != 0) {
                return std::unexpected("AXF1 varint overflow");
            }
            value |= static_cast<std::uint64_t>(*byte & 0x7FU) << (byte_index * 7U);
            if ((*byte & 0x80U) == 0) {
                return value;
            }
        }
        return std::unexpected("AXF1 varint overflow");
    }

    [[nodiscard]] std::expected<std::int64_t, std::string> read_zigzag_varint_i64() {
        auto raw = read_varint_u64();
        if (!raw) {
            return std::unexpected(raw.error());
        }
        const auto v = *raw;
        return static_cast<std::int64_t>((v >> 1) ^ (~(v & 1) + 1));
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

std::size_t common_prefix_length(std::string_view a, std::string_view b) {
    const auto limit = std::min(a.size(), b.size());
    for (std::size_t i = 0; i < limit; ++i) {
        if (a[i] != b[i]) return i;
    }
    return limit;
}

std::expected<std::vector<unsigned char>, std::string>
encode_qname_dict_payload(const std::vector<Axf1Record>& records) {
    if (records.empty()) {
        return std::vector<unsigned char>{};
    }

    std::map<std::string_view, std::uint32_t> unique_map;
    for (const auto& record : records) {
        unique_map.try_emplace(std::string_view{record.qname}, 0);
    }
    std::uint32_t sorted_index = 0;
    for (auto& [key, idx] : unique_map) {
        idx = sorted_index++;
    }

    std::vector<std::string_view> sorted_keys;
    sorted_keys.reserve(unique_map.size());
    for (const auto& [key, idx] : unique_map) {
        sorted_keys.push_back(key);
    }

    std::vector<unsigned char> bytes;
    append_varint_u64(bytes, sorted_keys.size());

    append_varint_u64(bytes, sorted_keys[0].size());
    bytes.insert(bytes.end(), sorted_keys[0].begin(), sorted_keys[0].end());

    for (std::size_t i = 1; i < sorted_keys.size(); ++i) {
        const auto shared = common_prefix_length(sorted_keys[i - 1], sorted_keys[i]);
        const auto suffix_len = sorted_keys[i].size() - shared;
        append_varint_u64(bytes, shared);
        append_varint_u64(bytes, suffix_len);
        bytes.insert(bytes.end(), sorted_keys[i].begin() + static_cast<std::ptrdiff_t>(shared),
                     sorted_keys[i].end());
    }

    for (const auto& record : records) {
        append_varint_u64(bytes, unique_map.at(std::string_view{record.qname}));
    }

    return bytes;
}

EncodedColumn encode_qname_column(const std::vector<Axf1Record>& records) {
    auto raw = encode_strings(records, &Axf1Record::qname);
    auto dict = encode_qname_dict_payload(records);
    if (dict && dict->size() < raw.size()) {
        return {.column_id = Axf1ColumnId::qname,
                .codec_id = Axf1CodecId::qname_dict,
                .payload = std::move(*dict)};
    }
    return {.column_id = Axf1ColumnId::qname,
            .codec_id = Axf1CodecId::raw,
            .payload = std::move(raw)};
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

std::expected<std::vector<unsigned char>, std::string>
encode_pos_delta_varint(const std::vector<Axf1Record>& records) {
    std::vector<unsigned char> bytes;
    if (records.empty()) {
        return bytes;
    }

    std::int32_t previous_pos = records.front().pos;
    if (previous_pos < 0) {
        return std::unexpected("AXF1 POS delta codec requires non-negative positions");
    }
    append_varint_u64(bytes, static_cast<std::uint64_t>(previous_pos));

    for (std::size_t index = 1; index < records.size(); ++index) {
        const std::int32_t current_pos = records.at(index).pos;
        if (current_pos < previous_pos) {
            return std::unexpected("AXF1 POS delta codec requires monotonic positions");
        }
        append_varint_u64(bytes, static_cast<std::uint64_t>(current_pos - previous_pos));
        previous_pos = current_pos;
    }
    return bytes;
}

EncodedColumn encode_pos_column(const std::vector<Axf1Record>& records) {
    auto encoded = encode_pos_delta_varint(records);
    if (encoded) {
        return {.column_id = Axf1ColumnId::pos,
                .codec_id = Axf1CodecId::pos_delta_varint,
                .payload = std::move(*encoded)};
    }
    return {.column_id = Axf1ColumnId::pos,
            .codec_id = Axf1CodecId::raw,
            .payload = encode_i32(records, &Axf1Record::pos)};
}

std::uint8_t bit_width_u16(std::uint16_t value) {
    std::uint8_t width = 0;
    while (value != 0) {
        ++width;
        value >>= 1U;
    }
    return width;
}

std::vector<unsigned char> encode_flag_bitpack_payload(const std::vector<Axf1Record>& records,
                                                       std::uint8_t bit_width) {
    const std::size_t bit_count = records.size() * static_cast<std::size_t>(bit_width);
    std::vector<unsigned char> bytes(1 + ((bit_count + 7U) / 8U), 0);
    bytes.at(0) = bit_width;
    if (bit_width == 0) {
        return bytes;
    }

    std::size_t bit_offset = 0;
    for (const Axf1Record& record : records) {
        for (std::uint8_t bit = 0; bit < bit_width; ++bit) {
            if (((record.flag >> bit) & 1U) != 0) {
                bytes.at(1 + bit_offset / 8U) |=
                    static_cast<unsigned char>(1U << (bit_offset % 8U));
            }
            ++bit_offset;
        }
    }
    return bytes;
}

EncodedColumn encode_flag_column(const std::vector<Axf1Record>& records) {
    std::uint8_t bit_width = 0;
    for (const Axf1Record& record : records) {
        bit_width = std::max(bit_width, bit_width_u16(record.flag));
    }

    auto encoded = encode_flag_bitpack_payload(records, bit_width);
    const auto raw = encode_u16(records, &Axf1Record::flag);
    if (encoded.size() < raw.size()) {
        return {.column_id = Axf1ColumnId::flag,
                .codec_id = Axf1CodecId::flag_bitpack,
                .payload = std::move(encoded)};
    }
    return {.column_id = Axf1ColumnId::flag, .codec_id = Axf1CodecId::raw, .payload = raw};
}

std::vector<unsigned char> encode_u8(const std::vector<Axf1Record>& records,
                                     std::uint8_t Axf1Record::* field) {
    std::vector<unsigned char> bytes;
    for (const Axf1Record& record : records) {
        append_u8(bytes, record.*field);
    }
    return bytes;
}

std::vector<unsigned char> encode_mapq_rle_payload(const std::vector<Axf1Record>& records) {
    std::vector<unsigned char> bytes;
    if (records.empty()) {
        return bytes;
    }

    std::uint8_t current_mapq = records.front().mapq;
    std::uint64_t run_length = 1;
    for (std::size_t index = 1; index < records.size(); ++index) {
        if (records.at(index).mapq == current_mapq) {
            ++run_length;
            continue;
        }
        append_varint_u64(bytes, run_length);
        append_u8(bytes, current_mapq);
        current_mapq = records.at(index).mapq;
        run_length = 1;
    }
    append_varint_u64(bytes, run_length);
    append_u8(bytes, current_mapq);
    return bytes;
}

EncodedColumn encode_mapq_column(const std::vector<Axf1Record>& records) {
    auto encoded = encode_mapq_rle_payload(records);
    const auto raw = encode_u8(records, &Axf1Record::mapq);
    if (encoded.size() < raw.size()) {
        return {.column_id = Axf1ColumnId::mapq,
                .codec_id = Axf1CodecId::mapq_rle,
                .payload = std::move(encoded)};
    }
    return {.column_id = Axf1ColumnId::mapq, .codec_id = Axf1CodecId::raw, .payload = raw};
}

std::expected<std::uint8_t, std::string> encode_cigar_op(char op) {
    switch (op) {
    case 'M':
        return 0;
    case 'I':
        return 1;
    case 'D':
        return 2;
    case 'N':
        return 3;
    case 'S':
        return 4;
    case 'H':
        return 5;
    case 'P':
        return 6;
    case '=':
        return 7;
    case 'X':
        return 8;
    default:
        return std::unexpected("unsupported AXF1 CIGAR operation");
    }
}

std::expected<std::vector<std::pair<std::uint64_t, std::uint8_t>>, std::string>
parse_cigar_tokens(std::string_view cigar) {
    if (cigar.empty() || cigar == "*") {
        return std::unexpected("AXF1 CIGAR token codec requires concrete CIGAR");
    }

    std::vector<std::pair<std::uint64_t, std::uint8_t>> tokens;
    std::uint64_t length = 0;
    bool reading_length = false;
    bool leading_zero = false;
    for (char ch : cigar) {
        if (ch >= '0' && ch <= '9') {
            if (!reading_length) {
                reading_length = true;
                leading_zero = ch == '0';
            } else if (leading_zero) {
                return std::unexpected("AXF1 CIGAR token codec rejects leading zero lengths");
            }
            if (length >
                (std::numeric_limits<std::uint64_t>::max() - static_cast<std::uint64_t>(ch - '0')) /
                    10U) {
                return std::unexpected("AXF1 CIGAR token length overflow");
            }
            length = length * 10U + static_cast<std::uint64_t>(ch - '0');
            continue;
        }

        if (!reading_length || length == 0) {
            return std::unexpected("invalid AXF1 CIGAR token length");
        }
        auto op = encode_cigar_op(ch);
        if (!op) {
            return std::unexpected(op.error());
        }
        tokens.emplace_back(length, *op);
        length = 0;
        reading_length = false;
        leading_zero = false;
    }

    if (reading_length) {
        return std::unexpected("AXF1 CIGAR token codec rejects trailing length");
    }
    if (tokens.empty()) {
        return std::unexpected("AXF1 CIGAR token codec requires operations");
    }
    return tokens;
}

std::expected<std::vector<unsigned char>, std::string>
encode_cigar_token_payload(const std::vector<Axf1Record>& records) {
    std::vector<unsigned char> bytes;
    for (const Axf1Record& record : records) {
        auto tokens = parse_cigar_tokens(record.cigar);
        if (!tokens) {
            return std::unexpected(tokens.error());
        }
        append_varint_u64(bytes, tokens->size());
        for (const auto& [length, op] : *tokens) {
            append_varint_u64(bytes, length);
            append_u8(bytes, op);
        }
    }
    return bytes;
}

std::expected<std::vector<unsigned char>, std::string>
encode_cigar_dict_payload(const std::vector<Axf1Record>& records) {
    if (records.empty()) {
        return std::vector<unsigned char>{};
    }

    std::map<std::string_view, std::uint32_t> unique_map;
    for (const auto& record : records) {
        unique_map.try_emplace(std::string_view{record.cigar}, 0);
    }
    std::uint32_t sorted_index = 0;
    for (auto& [key, idx] : unique_map) {
        idx = sorted_index++;
    }

    std::vector<unsigned char> bytes;
    append_varint_u64(bytes, unique_map.size());

    for (const auto& [key, idx] : unique_map) {
        append_varint_u64(bytes, key.size());
        bytes.insert(bytes.end(), key.begin(), key.end());
    }

    for (const auto& record : records) {
        append_varint_u64(bytes, unique_map.at(std::string_view{record.cigar}));
    }

    return bytes;
}

EncodedColumn encode_cigar_column(const std::vector<Axf1Record>& records) {
    auto raw = encode_strings(records, &Axf1Record::cigar);
    auto token = encode_cigar_token_payload(records);
    auto dict = encode_cigar_dict_payload(records);

    auto best_size = raw.size();
    auto best_id = Axf1CodecId::raw;

    if (token && token->size() < best_size) {
        best_size = token->size();
        best_id = Axf1CodecId::cigar_token;
    }
    if (dict && dict->size() < best_size) {
        best_id = Axf1CodecId::cigar_dict;
    }

    if (best_id == Axf1CodecId::cigar_dict) {
        return {.column_id = Axf1ColumnId::cigar,
                .codec_id = Axf1CodecId::cigar_dict,
                .payload = std::move(*dict)};
    }
    if (best_id == Axf1CodecId::cigar_token) {
        return {.column_id = Axf1ColumnId::cigar,
                .codec_id = Axf1CodecId::cigar_token,
                .payload = std::move(*token)};
    }
    return {.column_id = Axf1ColumnId::cigar,
            .codec_id = Axf1CodecId::raw,
            .payload = std::move(raw)};
}

std::expected<std::uint8_t, std::string> encode_acgt_base_2bit(char base) {
    switch (base) {
    case 'A':
        return 0;
    case 'C':
        return 1;
    case 'G':
        return 2;
    case 'T':
        return 3;
    default:
        return std::unexpected("AXF1 SEQ 2-bit literal requires uppercase A/C/G/T");
    }
}

std::expected<std::vector<unsigned char>, std::string>
encode_seq_2bit_literal_payload(const std::vector<Axf1Record>& records) {
    std::vector<unsigned char> bytes;
    for (const Axf1Record& record : records) {
        if (record.sequence.empty()) {
            return std::unexpected("AXF1 SEQ 2-bit literal requires non-empty sequence");
        }

        append_varint_u64(bytes, record.sequence.size());
        std::uint8_t packed = 0;
        std::uint8_t bases_in_byte = 0;
        for (char base : record.sequence) {
            auto encoded_base = encode_acgt_base_2bit(base);
            if (!encoded_base) {
                return std::unexpected(encoded_base.error());
            }
            packed |= static_cast<std::uint8_t>(*encoded_base << (bases_in_byte * 2U));
            ++bases_in_byte;
            if (bases_in_byte == 4) {
                append_u8(bytes, packed);
                packed = 0;
                bases_in_byte = 0;
            }
        }
        if (bases_in_byte != 0) {
            append_u8(bytes, packed);
        }
    }
    return bytes;
}

EncodedColumn encode_sequence_column(const std::vector<Axf1Record>& records) {
    const auto raw = encode_strings(records, &Axf1Record::sequence);
    auto encoded = encode_seq_2bit_literal_payload(records);
    if (encoded && encoded->size() < raw.size()) {
        return {.column_id = Axf1ColumnId::sequence,
                .codec_id = Axf1CodecId::seq_2bit_literal,
                .payload = std::move(*encoded)};
    }
    return {.column_id = Axf1ColumnId::sequence, .codec_id = Axf1CodecId::raw, .payload = raw};
}

std::expected<std::vector<unsigned char>, std::string>
encode_qual_rle_payload(const std::vector<Axf1Record>& records) {
    std::vector<unsigned char> bytes;
    for (const Axf1Record& record : records) {
        if (record.quality.empty() || record.quality == "*") {
            return std::unexpected("AXF1 QUAL RLE requires concrete quality");
        }

        append_varint_u64(bytes, record.quality.size());
        char current = record.quality.front();
        std::uint64_t run_length = 1;
        for (std::size_t index = 1; index < record.quality.size(); ++index) {
            if (record.quality.at(index) == current) {
                ++run_length;
                continue;
            }
            append_varint_u64(bytes, run_length);
            append_u8(bytes, static_cast<std::uint8_t>(current));
            current = record.quality.at(index);
            run_length = 1;
        }
        append_varint_u64(bytes, run_length);
        append_u8(bytes, static_cast<std::uint8_t>(current));
    }
    return bytes;
}

std::uint8_t bit_width_for_alphabet_size(std::size_t alphabet_size) {
    if (alphabet_size <= 1) {
        return 0;
    }
    std::uint8_t width = 0;
    std::size_t values = 1;
    while (values < alphabet_size) {
        values <<= 1U;
        ++width;
    }
    return width;
}

std::expected<std::vector<unsigned char>, std::string>
encode_qual_pack_payload(const std::vector<Axf1Record>& records) {
    std::array<bool, 256> present{};
    std::size_t alphabet_size = 0;
    for (const Axf1Record& record : records) {
        if (record.quality.empty() || record.quality == "*") {
            return std::unexpected("AXF1 QUAL pack requires concrete quality");
        }
        for (unsigned char value : record.quality) {
            if (!present.at(value)) {
                present.at(value) = true;
                ++alphabet_size;
            }
        }
    }
    if (alphabet_size == 0 || alphabet_size > 128) {
        return std::unexpected("AXF1 QUAL pack alphabet is unsupported");
    }

    std::array<std::uint8_t, 256> code_by_value{};
    std::vector<std::uint8_t> alphabet;
    alphabet.reserve(alphabet_size);
    for (std::size_t value = 0; value < present.size(); ++value) {
        if (present.at(value)) {
            code_by_value.at(value) = static_cast<std::uint8_t>(alphabet.size());
            alphabet.push_back(static_cast<std::uint8_t>(value));
        }
    }

    const std::uint8_t bit_width = bit_width_for_alphabet_size(alphabet.size());
    std::vector<unsigned char> bytes;
    append_varint_u64(bytes, alphabet.size());
    for (std::uint8_t value : alphabet) {
        append_u8(bytes, value);
    }

    for (const Axf1Record& record : records) {
        append_varint_u64(bytes, record.quality.size());
        if (bit_width == 0) {
            continue;
        }

        std::uint8_t packed = 0;
        std::uint8_t bits_in_byte = 0;
        for (unsigned char value : record.quality) {
            std::uint8_t code = code_by_value.at(value);
            for (std::uint8_t bit = 0; bit < bit_width; ++bit) {
                if (((code >> bit) & 1U) != 0) {
                    packed |= static_cast<std::uint8_t>(1U << bits_in_byte);
                }
                ++bits_in_byte;
                if (bits_in_byte == 8) {
                    append_u8(bytes, packed);
                    packed = 0;
                    bits_in_byte = 0;
                }
            }
        }
        if (bits_in_byte != 0) {
            append_u8(bytes, packed);
        }
    }

    return bytes;
}

void bin_quality_illumina8(std::string& quality) {
    for (auto& c : quality) {
        const int q = static_cast<unsigned char>(c) - 33;
        int binned;
        if (q <= 1) binned = 0;
        else if (q <= 9) binned = 6;
        else if (q <= 19) binned = 15;
        else if (q <= 24) binned = 22;
        else if (q <= 29) binned = 27;
        else if (q <= 34) binned = 33;
        else if (q <= 39) binned = 37;
        else binned = 40;
        c = static_cast<char>(binned + 33);
    }
}

EncodedColumn encode_quality_column(const std::vector<Axf1Record>& records) {
    const auto raw = encode_strings(records, &Axf1Record::quality);
    EncodedColumn best{
        .column_id = Axf1ColumnId::quality, .codec_id = Axf1CodecId::raw, .payload = raw};
    auto encoded = encode_qual_rle_payload(records);
    if (encoded && encoded->size() < raw.size()) {
        best = {.column_id = Axf1ColumnId::quality,
                .codec_id = Axf1CodecId::qual_rle,
                .payload = std::move(*encoded)};
    }
    auto packed = encode_qual_pack_payload(records);
    if (packed && packed->size() < best.payload.size()) {
        best = {.column_id = Axf1ColumnId::quality,
                .codec_id = Axf1CodecId::qual_pack,
                .payload = std::move(*packed)};
    }
    return best;
}

std::expected<std::vector<unsigned char>, std::string>
encode_compressed_payload_envelope(Axf1CodecId base_codec_id, Axf1Compression compression,
                                   const std::vector<unsigned char>& payload) {
    std::vector<unsigned char> envelope;
    append_varint_u64(envelope, static_cast<std::uint16_t>(base_codec_id));
    if (compression == Axf1Compression::none) {
        append_varint_u64(envelope, static_cast<std::uint64_t>(Axf1CompressionId::stored));
        append_varint_u64(envelope, static_cast<std::uint64_t>(payload.size()));
        append_varint_u64(envelope, static_cast<std::uint64_t>(payload.size()));
        envelope.insert(envelope.end(), payload.begin(), payload.end());
        return envelope;
    }
    if (compression != Axf1Compression::zstd) {
        return std::unexpected("unsupported AXF1 writer compression");
    }

#ifdef ALIGNX_HAVE_ZSTD
    const std::size_t bound = ZSTD_compressBound(payload.size());
    std::vector<unsigned char> compressed(bound);
    const std::size_t compressed_size =
        ZSTD_compress(compressed.data(), compressed.size(), payload.data(), payload.size(), 1);
    if (ZSTD_isError(compressed_size) != 0) {
        return std::unexpected(std::string("failed to compress AXF1 zstd payload: ") +
                               ZSTD_getErrorName(compressed_size));
    }
    compressed.resize(compressed_size);
    append_varint_u64(envelope, static_cast<std::uint64_t>(Axf1CompressionId::zstd));
    append_varint_u64(envelope, static_cast<std::uint64_t>(payload.size()));
    append_varint_u64(envelope, static_cast<std::uint64_t>(compressed.size()));
    envelope.insert(envelope.end(), compressed.begin(), compressed.end());
    return envelope;
#else
    return std::unexpected("AXF1 zstd writer compression requires ALIGNX_ENABLE_ZSTD=ON");
#endif
}

std::expected<EncodedColumn, std::string>
encode_quality_column(const std::vector<Axf1Record>& records, const Axf1WriteOptions& options) {
    const std::vector<Axf1Record>* source = &records;
    std::vector<Axf1Record> binned_records;

    if (options.quality_lossy == Axf1QualityLossy::illumina8) {
        binned_records.reserve(records.size());
        for (const auto& record : records) {
            Axf1Record r;
            r.quality = record.quality;
            bin_quality_illumina8(r.quality);
            binned_records.push_back(std::move(r));
        }
        source = &binned_records;
    }

    if (options.quality_compression != Axf1Compression::zstd) {
        if (options.quality_compression == Axf1Compression::none) {
            return encode_quality_column(*source);
        }
        return std::unexpected("unsupported AXF1 writer quality compression");
    }

    auto best = encode_quality_column(*source);

#ifndef ALIGNX_HAVE_ZSTD
    return std::unexpected("AXF1 zstd writer compression requires ALIGNX_ENABLE_ZSTD=ON");
#else
    auto packed = encode_qual_pack_payload(*source);
    if (!packed) {
        return best;
    }
    auto envelope = encode_compressed_payload_envelope(Axf1CodecId::qual_pack,
                                                       options.quality_compression, *packed);
    if (!envelope) {
        return std::unexpected(envelope.error());
    }
    if (envelope->size() >= best.payload.size()) {
        return best;
    }
    return EncodedColumn{.column_id = Axf1ColumnId::quality,
                         .codec_id = Axf1CodecId::qual_pack_compressed,
                         .payload = std::move(*envelope)};
#endif
}

struct ParsedTag {
    char key0 = 0;
    char key1 = 0;
    char type = 0;
    std::string_view value;
};

struct TagKeyEntry {
    char key0 = 0;
    char key1 = 0;
    char type = 0;
};

std::expected<std::vector<ParsedTag>, std::string>
parse_tags(std::string_view tags) {
    std::vector<ParsedTag> result;
    if (tags.empty()) {
        return result;
    }
    std::size_t pos = 0;
    while (pos < tags.size()) {
        auto tab = tags.find('\t', pos);
        auto field = tags.substr(pos, tab == std::string_view::npos ? std::string_view::npos : tab - pos);
        if (field.size() < 5 || field[2] != ':' || field[4] != ':') {
            return std::unexpected("malformed SAM tag");
        }
        result.push_back({.key0 = static_cast<char>(field[0]),
                          .key1 = static_cast<char>(field[1]),
                          .type = static_cast<char>(field[3]),
                          .value = field.substr(5)});
        pos = (tab == std::string_view::npos) ? tags.size() : tab + 1;
    }
    return result;
}

std::expected<std::vector<unsigned char>, std::string>
encode_tags_per_stream_payload(const std::vector<Axf1Record>& records) {
    if (records.empty()) {
        return std::vector<unsigned char>{};
    }

    const auto record_count = records.size();
    std::vector<std::vector<ParsedTag>> all_tags;
    all_tags.reserve(record_count);
    for (const auto& record : records) {
        auto parsed = parse_tags(record.tags);
        if (!parsed) {
            return std::unexpected(parsed.error());
        }
        all_tags.push_back(std::move(*parsed));
    }

    std::vector<TagKeyEntry> canonical;
    std::map<std::pair<char, char>, std::size_t> key_index;
    for (const auto& record_tags : all_tags) {
        for (const auto& tag : record_tags) {
            auto key = std::make_pair(tag.key0, tag.key1);
            auto it = key_index.find(key);
            if (it == key_index.end()) {
                key_index[key] = canonical.size();
                canonical.push_back({.key0 = tag.key0, .key1 = tag.key1, .type = tag.type});
            } else if (canonical[it->second].type != tag.type) {
                return std::unexpected("tag type conflict");
            }
        }
    }

    if (canonical.empty()) {
        std::vector<unsigned char> bytes;
        append_varint_u64(bytes, 0);
        return bytes;
    }

    for (const auto& record_tags : all_tags) {
        std::size_t last_canonical_index = 0;
        bool first = true;
        for (const auto& tag : record_tags) {
            auto idx = key_index[std::make_pair(tag.key0, tag.key1)];
            if (!first && idx <= last_canonical_index) {
                return std::unexpected("tag order mismatch");
            }
            last_canonical_index = idx;
            first = false;
        }
    }

    const auto tag_count = canonical.size();
    const auto bitmap_bytes = (record_count + 7) / 8;

    std::vector<std::vector<bool>> presence(tag_count, std::vector<bool>(record_count, false));
    std::vector<std::vector<std::string_view>> values(tag_count);

    for (std::size_t r = 0; r < record_count; ++r) {
        for (const auto& tag : all_tags[r]) {
            auto idx = key_index[std::make_pair(tag.key0, tag.key1)];
            presence[idx][r] = true;
            values[idx].push_back(tag.value);
        }
    }

    std::vector<unsigned char> bytes;
    append_varint_u64(bytes, tag_count);
    for (const auto& entry : canonical) {
        bytes.push_back(static_cast<unsigned char>(entry.key0));
        bytes.push_back(static_cast<unsigned char>(entry.key1));
        bytes.push_back(static_cast<unsigned char>(entry.type));
    }

    for (std::size_t t = 0; t < tag_count; ++t) {
        bool all_present = (values[t].size() == record_count);
        bytes.push_back(all_present ? 0x01 : 0x00);
        if (!all_present) {
            for (std::size_t byte_idx = 0; byte_idx < bitmap_bytes; ++byte_idx) {
                unsigned char bits = 0;
                for (std::size_t bit = 0; bit < 8 && byte_idx * 8 + bit < record_count; ++bit) {
                    if (presence[t][byte_idx * 8 + bit]) {
                        bits |= static_cast<unsigned char>(1U << bit);
                    }
                }
                bytes.push_back(bits);
            }
        }
    }

    for (std::size_t t = 0; t < tag_count; ++t) {
        append_varint_u64(bytes, values[t].size());
        if (canonical[t].type == 'i') {
            for (const auto& val : values[t]) {
                std::int64_t int_val = 0;
                auto [ptr, ec] = std::from_chars(val.data(), val.data() + val.size(), int_val);
                if (ec != std::errc{} || ptr != val.data() + val.size()) {
                    return std::unexpected("invalid integer tag value");
                }
                append_zigzag_varint_i64(bytes, int_val);
            }
        } else {
            for (const auto& val : values[t]) {
                append_varint_u64(bytes, val.size());
                bytes.insert(bytes.end(), val.begin(), val.end());
            }
        }
    }

    return bytes;
}

EncodedColumn encode_tags_column(const std::vector<Axf1Record>& records) {
    auto raw = encode_strings(records, &Axf1Record::tags);
    auto per_stream = encode_tags_per_stream_payload(records);
    if (per_stream && per_stream->size() < raw.size()) {
        return {.column_id = Axf1ColumnId::tags,
                .codec_id = Axf1CodecId::tags_per_stream,
                .payload = std::move(*per_stream)};
    }
    return {.column_id = Axf1ColumnId::tags,
            .codec_id = Axf1CodecId::raw,
            .payload = std::move(raw)};
}

std::expected<std::vector<EncodedColumn>, std::string>
encode_columns(const Axf1Chunk& chunk, const Axf1WriteOptions& options) {
    auto flag_column = encode_flag_column(chunk.records);
    auto mapq_column = encode_mapq_column(chunk.records);
    auto cigar_column = encode_cigar_column(chunk.records);
    auto sequence_column = encode_sequence_column(chunk.records);
    auto quality_column = encode_quality_column(chunk.records, options);
    if (!quality_column) {
        return std::unexpected(quality_column.error());
    }
    return std::vector<EncodedColumn>{
        encode_qname_column(chunk.records),
        std::move(flag_column),
        encode_pos_column(chunk.records),
        std::move(mapq_column),
        std::move(cigar_column),
        {.column_id = Axf1ColumnId::mate_reference,
         .codec_id = Axf1CodecId::raw,
         .payload = encode_strings(chunk.records, &Axf1Record::mate_reference)},
        {.column_id = Axf1ColumnId::mate_pos,
         .codec_id = Axf1CodecId::raw,
         .payload = encode_i32(chunk.records, &Axf1Record::mate_pos)},
        {.column_id = Axf1ColumnId::template_length,
         .codec_id = Axf1CodecId::raw,
         .payload = encode_i32(chunk.records, &Axf1Record::template_length)},
        std::move(sequence_column),
        std::move(*quality_column),
        encode_tags_column(chunk.records)};
}

std::expected<std::vector<unsigned char>, std::string>
write_chunk(const Axf1Chunk& chunk, const Axf1WriteOptions& options) {
    auto encoded_columns = encode_columns(chunk, options);
    if (!encoded_columns) {
        return std::unexpected(encoded_columns.error());
    }
    std::vector<ColumnEntry> entries;
    entries.reserve(encoded_columns->size());

    std::uint64_t payload_offset = 0;
    for (const EncodedColumn& column : *encoded_columns) {
        entries.push_back({.column_id = column.column_id,
                           .codec_id = column.codec_id,
                           .offset = payload_offset,
                           .length = static_cast<std::uint64_t>(column.payload.size())});
        payload_offset += column.payload.size();
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
    for (const EncodedColumn& column : *encoded_columns) {
        bytes.insert(bytes.end(), column.payload.begin(), column.payload.end());
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

std::expected<std::vector<std::string>, std::string>
decode_qname_dict_column(const std::vector<unsigned char>& bytes, std::uint32_t record_count) {
    Reader reader(bytes);

    auto dict_size_val = reader.read_varint_u64();
    if (!dict_size_val) {
        return std::unexpected("AXF1 QNAME dict: truncated dict_size");
    }
    const auto dict_size = static_cast<std::uint32_t>(*dict_size_val);
    if (dict_size == 0) {
        return std::unexpected("AXF1 QNAME dict has zero entries");
    }
    if (dict_size > record_count) {
        return std::unexpected("AXF1 QNAME dict size exceeds record count");
    }

    std::vector<std::string> dictionary;
    dictionary.reserve(dict_size);

    auto first_len = reader.read_varint_u64();
    if (!first_len) {
        return std::unexpected("AXF1 QNAME dict: truncated first entry length");
    }
    auto first_str = reader.read_string(static_cast<std::uint32_t>(*first_len));
    if (!first_str) {
        return std::unexpected("AXF1 QNAME dict: truncated first entry");
    }
    dictionary.push_back(std::move(*first_str));

    for (std::uint32_t i = 1; i < dict_size; ++i) {
        auto shared_len = reader.read_varint_u64();
        if (!shared_len) {
            return std::unexpected("AXF1 QNAME dict: truncated shared prefix length");
        }
        if (*shared_len > dictionary[i - 1].size()) {
            return std::unexpected("AXF1 QNAME dict shared prefix exceeds previous entry");
        }
        auto suffix_len = reader.read_varint_u64();
        if (!suffix_len) {
            return std::unexpected("AXF1 QNAME dict: truncated suffix length");
        }
        auto suffix = reader.read_string(static_cast<std::uint32_t>(*suffix_len));
        if (!suffix) {
            return std::unexpected("AXF1 QNAME dict: truncated suffix");
        }
        std::string entry = dictionary[i - 1].substr(0, static_cast<std::size_t>(*shared_len));
        entry += *suffix;
        dictionary.push_back(std::move(entry));
    }

    std::vector<std::string> values;
    values.reserve(record_count);
    for (std::uint32_t j = 0; j < record_count; ++j) {
        auto idx = reader.read_varint_u64();
        if (!idx) {
            return std::unexpected("AXF1 QNAME dict: truncated index");
        }
        if (*idx >= dict_size) {
            return std::unexpected("AXF1 QNAME dict index out of range");
        }
        values.push_back(dictionary[static_cast<std::size_t>(*idx)]);
    }

    if (reader.offset() != reader.size()) {
        return std::unexpected("AXF1 QNAME dict column has trailing bytes");
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

std::expected<std::vector<std::uint16_t>, std::string>
decode_flag_bitpack_column(const std::vector<unsigned char>& bytes, std::uint32_t record_count) {
    if (bytes.empty()) {
        return std::unexpected("truncated AXF1 FLAG bitpack column");
    }

    const std::uint8_t bit_width = bytes.at(0);
    if (bit_width > 16) {
        return std::unexpected("invalid AXF1 FLAG bitpack width");
    }

    const std::size_t bit_count = static_cast<std::size_t>(record_count) * bit_width;
    const std::size_t expected_size = 1 + ((bit_count + 7U) / 8U);
    if (bytes.size() < expected_size) {
        return std::unexpected("truncated AXF1 FLAG bitpack column");
    }
    if (bytes.size() > expected_size) {
        return std::unexpected("AXF1 FLAG bitpack column has trailing bytes");
    }

    std::vector<std::uint16_t> values;
    values.reserve(record_count);
    std::size_t bit_offset = 0;
    for (std::uint32_t index = 0; index < record_count; ++index) {
        std::uint16_t value = 0;
        for (std::uint8_t bit = 0; bit < bit_width; ++bit) {
            const auto byte = bytes.at(1 + bit_offset / 8U);
            if (((byte >> (bit_offset % 8U)) & 1U) != 0) {
                value |= static_cast<std::uint16_t>(1U << bit);
            }
            ++bit_offset;
        }
        values.push_back(value);
    }
    return values;
}

std::expected<std::uint64_t, std::string>
read_varint_u64(Reader& reader, std::string_view truncated_error, std::string_view overflow_error) {
    std::uint64_t value = 0;
    for (std::size_t byte_index = 0; byte_index < 10; ++byte_index) {
        auto byte = reader.read_u8();
        if (!byte) {
            return std::unexpected(std::string(truncated_error));
        }
        if (byte_index == 9 && (*byte & 0xFEU) != 0) {
            return std::unexpected(std::string(overflow_error));
        }
        value |= static_cast<std::uint64_t>(*byte & 0x7FU) << (byte_index * 7U);
        if ((*byte & 0x80U) == 0) {
            return value;
        }
    }
    return std::unexpected(std::string(overflow_error));
}

std::expected<DecodedPayloadEnvelope, std::string>
decode_compressed_payload_envelope(const std::vector<unsigned char>& bytes) {
    Reader reader(bytes);
    auto base_codec_id = read_varint_u64(reader, "truncated AXF1 compressed payload base codec",
                                         "AXF1 compressed payload base codec overflow");
    if (!base_codec_id) {
        return std::unexpected(base_codec_id.error());
    }
    auto compression_id = read_varint_u64(reader, "truncated AXF1 compressed payload compression",
                                          "AXF1 compressed payload compression overflow");
    if (!compression_id) {
        return std::unexpected(compression_id.error());
    }
    auto uncompressed_size =
        read_varint_u64(reader, "truncated AXF1 compressed payload uncompressed size",
                        "AXF1 compressed payload uncompressed size overflow");
    if (!uncompressed_size) {
        return std::unexpected(uncompressed_size.error());
    }
    auto compressed_size = read_varint_u64(reader, "truncated AXF1 compressed payload size",
                                           "AXF1 compressed payload size overflow");
    if (!compressed_size) {
        return std::unexpected(compressed_size.error());
    }
    if (*uncompressed_size > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()) ||
        *compressed_size > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        return std::unexpected("AXF1 compressed payload size is too large");
    }
    if (*compressed_size > reader.size() - reader.offset()) {
        return std::unexpected("truncated AXF1 compressed payload bytes");
    }

    auto payload = reader.read_string(static_cast<std::size_t>(*compressed_size));
    if (!payload) {
        return std::unexpected("truncated AXF1 compressed payload bytes");
    }
    if (reader.offset() != reader.size()) {
        return std::unexpected("AXF1 compressed payload has trailing bytes");
    }
    std::vector<unsigned char> compressed_payload(payload->begin(), payload->end());
    std::vector<unsigned char> decoded_payload;
    if (*compression_id == static_cast<std::uint64_t>(Axf1CompressionId::stored)) {
        if (*uncompressed_size != *compressed_size) {
            return std::unexpected("AXF1 compressed payload size mismatch");
        }
        decoded_payload = std::move(compressed_payload);
    } else if (*compression_id == static_cast<std::uint64_t>(Axf1CompressionId::zstd)) {
#ifdef ALIGNX_HAVE_ZSTD
        decoded_payload.resize(static_cast<std::size_t>(*uncompressed_size));
        const std::size_t result =
            ZSTD_decompress(decoded_payload.data(), decoded_payload.size(),
                            compressed_payload.data(), compressed_payload.size());
        if (ZSTD_isError(result) != 0) {
            return std::unexpected(std::string("failed to decompress AXF1 zstd payload: ") +
                                   ZSTD_getErrorName(result));
        }
        if (result != decoded_payload.size()) {
            return std::unexpected("AXF1 zstd decompressed size mismatch");
        }
#else
        return std::unexpected("unsupported AXF1 compressed payload compression");
#endif
    } else {
        return std::unexpected("unsupported AXF1 compressed payload compression");
    }
    return DecodedPayloadEnvelope{.base_codec_id = static_cast<Axf1CodecId>(*base_codec_id),
                                  .payload = std::move(decoded_payload)};
}

std::expected<std::vector<unsigned char>, std::string>
decode_compressed_base_payload(const std::vector<unsigned char>& bytes,
                               Axf1CodecId expected_base_codec_id,
                               std::string_view unsupported_base_error) {
    auto envelope = decode_compressed_payload_envelope(bytes);
    if (!envelope) {
        return std::unexpected(envelope.error());
    }
    if (envelope->base_codec_id != expected_base_codec_id) {
        return std::unexpected(std::string(unsupported_base_error));
    }
    return std::move(envelope->payload);
}

std::expected<std::vector<std::int32_t>, std::string>
decode_pos_delta_varint_column(const std::vector<unsigned char>& bytes,
                               std::uint32_t record_count) {
    Reader reader(bytes);
    std::vector<std::int32_t> values;
    values.reserve(record_count);
    std::uint64_t current_pos = 0;
    for (std::uint32_t index = 0; index < record_count; ++index) {
        auto encoded_value = read_varint_u64(reader, "truncated AXF1 POS delta varint",
                                             "AXF1 POS delta varint overflow");
        if (!encoded_value) {
            return std::unexpected(encoded_value.error());
        }
        if (index == 0) {
            current_pos = *encoded_value;
        } else if (*encoded_value >
                   static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max()) -
                       current_pos) {
            return std::unexpected("AXF1 POS delta value is too large");
        } else {
            current_pos += *encoded_value;
        }
        if (current_pos > static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max())) {
            return std::unexpected("AXF1 POS delta value is too large");
        }
        values.push_back(static_cast<std::int32_t>(current_pos));
    }
    if (reader.offset() != reader.size()) {
        return std::unexpected("AXF1 POS delta column has trailing bytes");
    }
    return values;
}

std::expected<std::vector<std::uint8_t>, std::string>
decode_mapq_rle_column(const std::vector<unsigned char>& bytes, std::uint32_t record_count) {
    Reader reader(bytes);
    std::vector<std::uint8_t> values;
    values.reserve(record_count);

    while (reader.offset() < reader.size()) {
        auto run_length = read_varint_u64(reader, "truncated AXF1 MAPQ RLE run length",
                                          "AXF1 MAPQ RLE run length overflow");
        if (!run_length) {
            return std::unexpected(run_length.error());
        }
        if (*run_length == 0) {
            return std::unexpected("AXF1 MAPQ RLE run length is zero");
        }
        if (*run_length > record_count - values.size()) {
            return std::unexpected("AXF1 MAPQ RLE value count mismatch");
        }
        auto mapq = reader.read_u8();
        if (!mapq) {
            return std::unexpected("truncated AXF1 MAPQ RLE value");
        }
        values.insert(values.end(), static_cast<std::size_t>(*run_length), *mapq);
    }

    if (values.size() != record_count) {
        return std::unexpected("AXF1 MAPQ RLE value count mismatch");
    }
    return values;
}

std::expected<char, std::string> decode_cigar_op(std::uint8_t op) {
    switch (op) {
    case 0:
        return 'M';
    case 1:
        return 'I';
    case 2:
        return 'D';
    case 3:
        return 'N';
    case 4:
        return 'S';
    case 5:
        return 'H';
    case 6:
        return 'P';
    case 7:
        return '=';
    case 8:
        return 'X';
    default:
        return std::unexpected("unknown AXF1 CIGAR token operation");
    }
}

std::expected<std::vector<std::string>, std::string>
decode_cigar_token_column(const std::vector<unsigned char>& bytes, std::uint32_t record_count) {
    Reader reader(bytes);
    std::vector<std::string> values;
    values.reserve(record_count);

    for (std::uint32_t record_index = 0; record_index < record_count; ++record_index) {
        auto op_count = read_varint_u64(reader, "truncated AXF1 CIGAR token op count",
                                        "AXF1 CIGAR token op count overflow");
        if (!op_count) {
            return std::unexpected(op_count.error());
        }
        if (*op_count == 0) {
            return std::unexpected("AXF1 CIGAR token op count is zero");
        }
        if (*op_count > static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max())) {
            return std::unexpected("AXF1 CIGAR token op count is too large");
        }

        std::string cigar;
        for (std::uint64_t op_index = 0; op_index < *op_count; ++op_index) {
            auto length = read_varint_u64(reader, "truncated AXF1 CIGAR token length",
                                          "AXF1 CIGAR token length overflow");
            if (!length) {
                return std::unexpected(length.error());
            }
            if (*length == 0) {
                return std::unexpected("AXF1 CIGAR token length is zero");
            }
            auto op = reader.read_u8();
            if (!op) {
                return std::unexpected("truncated AXF1 CIGAR token operation");
            }
            auto decoded_op = decode_cigar_op(*op);
            if (!decoded_op) {
                return std::unexpected(decoded_op.error());
            }
            cigar.append(std::to_string(*length));
            cigar.push_back(*decoded_op);
        }
        values.push_back(std::move(cigar));
    }

    if (reader.offset() != reader.size()) {
        return std::unexpected("AXF1 CIGAR token column has trailing bytes");
    }
    return values;
}

std::expected<std::vector<std::string>, std::string>
decode_cigar_dict_column(const std::vector<unsigned char>& bytes, std::uint32_t record_count) {
    Reader reader(bytes);

    auto dict_size_val = reader.read_varint_u64();
    if (!dict_size_val) {
        return std::unexpected("AXF1 CIGAR dict: truncated dict_size");
    }
    const auto dict_size = static_cast<std::uint32_t>(*dict_size_val);
    if (dict_size == 0) {
        return std::unexpected("AXF1 CIGAR dict has zero entries");
    }
    if (dict_size > record_count) {
        return std::unexpected("AXF1 CIGAR dict size exceeds record count");
    }

    std::vector<std::string> dictionary;
    dictionary.reserve(dict_size);

    for (std::uint32_t i = 0; i < dict_size; ++i) {
        auto entry_len = reader.read_varint_u64();
        if (!entry_len) {
            return std::unexpected("AXF1 CIGAR dict: truncated entry length");
        }
        auto entry_str = reader.read_string(static_cast<std::uint32_t>(*entry_len));
        if (!entry_str) {
            return std::unexpected("AXF1 CIGAR dict: truncated entry");
        }
        dictionary.push_back(std::move(*entry_str));
    }

    std::vector<std::string> values;
    values.reserve(record_count);
    for (std::uint32_t j = 0; j < record_count; ++j) {
        auto idx = reader.read_varint_u64();
        if (!idx) {
            return std::unexpected("AXF1 CIGAR dict: truncated index");
        }
        if (*idx >= dict_size) {
            return std::unexpected("AXF1 CIGAR dict index out of range");
        }
        values.push_back(dictionary[static_cast<std::size_t>(*idx)]);
    }

    if (reader.offset() != reader.size()) {
        return std::unexpected("AXF1 CIGAR dict column has trailing bytes");
    }
    return values;
}

std::expected<char, std::string> decode_acgt_base_2bit(std::uint8_t value) {
    switch (value) {
    case 0:
        return 'A';
    case 1:
        return 'C';
    case 2:
        return 'G';
    case 3:
        return 'T';
    default:
        return std::unexpected("invalid AXF1 SEQ 2-bit base");
    }
}

std::expected<std::vector<std::string>, std::string>
decode_seq_2bit_literal_column(const std::vector<unsigned char>& bytes,
                               std::uint32_t record_count) {
    Reader reader(bytes);
    std::vector<std::string> values;
    values.reserve(record_count);

    for (std::uint32_t index = 0; index < record_count; ++index) {
        auto length = read_varint_u64(reader, "truncated AXF1 SEQ 2-bit length",
                                      "AXF1 SEQ 2-bit length overflow");
        if (!length) {
            return std::unexpected(length.error());
        }
        if (*length > static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max())) {
            return std::unexpected("AXF1 SEQ 2-bit length is too large");
        }

        std::string sequence;
        sequence.reserve(static_cast<std::size_t>(*length));
        for (std::uint64_t base_index = 0; base_index < *length; ++base_index) {
            std::uint8_t packed = 0;
            if (base_index % 4U == 0) {
                auto byte = reader.read_u8();
                if (!byte) {
                    return std::unexpected("truncated AXF1 SEQ 2-bit bases");
                }
                packed = *byte;
            } else {
                packed = bytes.at(reader.offset() - 1U);
            }

            auto base = decode_acgt_base_2bit(
                static_cast<std::uint8_t>((packed >> ((base_index % 4U) * 2U)) & 0x03U));
            if (!base) {
                return std::unexpected(base.error());
            }
            sequence.push_back(*base);
        }
        values.push_back(std::move(sequence));
    }

    if (reader.offset() != reader.size()) {
        return std::unexpected("AXF1 SEQ 2-bit column has trailing bytes");
    }
    return values;
}

std::expected<std::vector<std::string>, std::string>
decode_qual_rle_column(const std::vector<unsigned char>& bytes, std::uint32_t record_count) {
    Reader reader(bytes);
    std::vector<std::string> values;
    values.reserve(record_count);

    for (std::uint32_t record_index = 0; record_index < record_count; ++record_index) {
        auto length = read_varint_u64(reader, "truncated AXF1 QUAL RLE length",
                                      "AXF1 QUAL RLE length overflow");
        if (!length) {
            return std::unexpected(length.error());
        }
        if (*length > static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max())) {
            return std::unexpected("AXF1 QUAL RLE length is too large");
        }

        std::string quality;
        quality.reserve(static_cast<std::size_t>(*length));
        while (quality.size() < *length) {
            auto run_length = read_varint_u64(reader, "truncated AXF1 QUAL RLE run length",
                                              "AXF1 QUAL RLE run length overflow");
            if (!run_length) {
                return std::unexpected(run_length.error());
            }
            if (*run_length == 0) {
                return std::unexpected("AXF1 QUAL RLE run length is zero");
            }
            if (*run_length > *length - quality.size()) {
                return std::unexpected("AXF1 QUAL RLE decoded length mismatch");
            }
            auto value = reader.read_u8();
            if (!value) {
                return std::unexpected("truncated AXF1 QUAL RLE value");
            }
            quality.append(static_cast<std::size_t>(*run_length), static_cast<char>(*value));
        }
        values.push_back(std::move(quality));
    }

    if (reader.offset() != reader.size()) {
        return std::unexpected("AXF1 QUAL RLE column has trailing bytes");
    }
    return values;
}

std::expected<std::vector<std::string>, std::string>
decode_qual_pack_column(const std::vector<unsigned char>& bytes, std::uint32_t record_count) {
    Reader reader(bytes);
    auto alphabet_count = read_varint_u64(reader, "truncated AXF1 QUAL pack alphabet count",
                                          "AXF1 QUAL pack alphabet count overflow");
    if (!alphabet_count) {
        return std::unexpected(alphabet_count.error());
    }
    if (*alphabet_count == 0) {
        return std::unexpected("AXF1 QUAL pack alphabet is empty");
    }
    if (*alphabet_count > 128) {
        return std::unexpected("AXF1 QUAL pack alphabet is too large");
    }

    std::vector<std::uint8_t> alphabet;
    alphabet.reserve(static_cast<std::size_t>(*alphabet_count));
    std::uint8_t previous = 0;
    for (std::uint64_t index = 0; index < *alphabet_count; ++index) {
        auto value = reader.read_u8();
        if (!value) {
            return std::unexpected("truncated AXF1 QUAL pack alphabet");
        }
        if (index > 0 && *value <= previous) {
            return std::unexpected("AXF1 QUAL pack alphabet is not strictly ascending");
        }
        alphabet.push_back(*value);
        previous = *value;
    }

    const std::uint8_t bit_width = bit_width_for_alphabet_size(alphabet.size());
    std::vector<std::string> values;
    values.reserve(record_count);
    for (std::uint32_t record_index = 0; record_index < record_count; ++record_index) {
        auto length = read_varint_u64(reader, "truncated AXF1 QUAL pack length",
                                      "AXF1 QUAL pack length overflow");
        if (!length) {
            return std::unexpected(length.error());
        }
        if (*length > static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max())) {
            return std::unexpected("AXF1 QUAL pack length is too large");
        }

        std::string quality;
        quality.reserve(static_cast<std::size_t>(*length));
        if (bit_width == 0) {
            quality.assign(static_cast<std::size_t>(*length), static_cast<char>(alphabet.front()));
            values.push_back(std::move(quality));
            continue;
        }

        std::uint8_t packed = 0;
        std::uint8_t bits_remaining = 0;
        for (std::uint64_t index = 0; index < *length; ++index) {
            std::uint8_t code = 0;
            for (std::uint8_t bit = 0; bit < bit_width; ++bit) {
                if (bits_remaining == 0) {
                    auto byte = reader.read_u8();
                    if (!byte) {
                        return std::unexpected("truncated AXF1 QUAL pack codes");
                    }
                    packed = *byte;
                    bits_remaining = 8;
                }
                if ((packed & 1U) != 0) {
                    code |= static_cast<std::uint8_t>(1U << bit);
                }
                packed >>= 1U;
                --bits_remaining;
            }
            if (code >= alphabet.size()) {
                return std::unexpected("AXF1 QUAL pack code is outside alphabet");
            }
            quality.push_back(static_cast<char>(alphabet.at(code)));
        }
        values.push_back(std::move(quality));
    }

    if (reader.offset() != reader.size()) {
        return std::unexpected("AXF1 QUAL pack column has trailing bytes");
    }
    return values;
}

std::expected<std::vector<std::string>, std::string>
decode_tags_per_stream_column(const std::vector<unsigned char>& bytes,
                              std::uint32_t record_count) {
    Reader reader(bytes);

    auto tag_count_val = reader.read_varint_u64();
    if (!tag_count_val) {
        return std::unexpected("AXF1 TAG stream: truncated tag_count");
    }
    const auto tag_count = static_cast<std::uint32_t>(*tag_count_val);

    if (tag_count == 0) {
        if (reader.offset() != reader.size()) {
            return std::unexpected("AXF1 TAG stream column has trailing bytes");
        }
        return std::vector<std::string>(record_count);
    }

    struct TagHeader {
        char key0 = 0;
        char key1 = 0;
        char type = 0;
    };
    std::vector<TagHeader> headers(tag_count);
    for (std::uint32_t t = 0; t < tag_count; ++t) {
        auto k0 = reader.read_u8();
        if (!k0) {
            return std::unexpected("AXF1 TAG stream: truncated tag key");
        }
        auto k1 = reader.read_u8();
        if (!k1) {
            return std::unexpected("AXF1 TAG stream: truncated tag key");
        }
        auto tp = reader.read_u8();
        if (!tp) {
            return std::unexpected("AXF1 TAG stream: truncated tag type");
        }
        headers[t] = {.key0 = static_cast<char>(*k0),
                       .key1 = static_cast<char>(*k1),
                       .type = static_cast<char>(*tp)};
    }

    const auto bitmap_bytes = (record_count + 7U) / 8U;
    std::vector<std::vector<bool>> presence(tag_count);
    for (std::uint32_t t = 0; t < tag_count; ++t) {
        auto flag = reader.read_u8();
        if (!flag) {
            return std::unexpected("AXF1 TAG stream: truncated presence flag");
        }
        if (*flag == 0x01) {
            presence[t].assign(record_count, true);
        } else if (*flag == 0x00) {
            presence[t].resize(record_count, false);
            for (std::uint32_t byte_idx = 0; byte_idx < bitmap_bytes; ++byte_idx) {
                auto bits = reader.read_u8();
                if (!bits) {
                    return std::unexpected("AXF1 TAG stream: truncated presence bitmap");
                }
                for (std::uint32_t bit = 0; bit < 8 && byte_idx * 8 + bit < record_count; ++bit) {
                    if ((*bits >> bit) & 1U) {
                        presence[t][byte_idx * 8 + bit] = true;
                    }
                }
            }
        } else {
            return std::unexpected("AXF1 TAG stream: invalid presence flag");
        }
    }

    std::vector<std::vector<std::string>> tag_values(tag_count);
    for (std::uint32_t t = 0; t < tag_count; ++t) {
        auto present_count_val = reader.read_varint_u64();
        if (!present_count_val) {
            return std::unexpected("AXF1 TAG stream: truncated present_count");
        }
        const auto present_count = static_cast<std::uint32_t>(*present_count_val);
        std::uint32_t expected_count = 0;
        for (std::uint32_t r = 0; r < record_count; ++r) {
            if (presence[t][r]) {
                ++expected_count;
            }
        }
        if (present_count != expected_count) {
            return std::unexpected("AXF1 TAG stream presence count mismatch");
        }

        tag_values[t].reserve(present_count);
        if (headers[t].type == 'i') {
            for (std::uint32_t v = 0; v < present_count; ++v) {
                auto int_val = reader.read_zigzag_varint_i64();
                if (!int_val) {
                    return std::unexpected("AXF1 TAG stream: truncated integer value");
                }
                tag_values[t].push_back(std::to_string(*int_val));
            }
        } else {
            for (std::uint32_t v = 0; v < present_count; ++v) {
                auto len = reader.read_varint_u64();
                if (!len) {
                    return std::unexpected("AXF1 TAG stream: truncated string value length");
                }
                auto str = reader.read_string(static_cast<std::size_t>(*len));
                if (!str) {
                    return std::unexpected("AXF1 TAG stream: truncated string value");
                }
                tag_values[t].push_back(std::move(*str));
            }
        }
    }

    if (reader.offset() != reader.size()) {
        return std::unexpected("AXF1 TAG stream column has trailing bytes");
    }

    std::vector<std::string> results(record_count);
    std::vector<std::uint32_t> value_cursors(tag_count, 0);
    for (std::uint32_t r = 0; r < record_count; ++r) {
        std::string& out = results[r];
        for (std::uint32_t t = 0; t < tag_count; ++t) {
            if (!presence[t][r]) {
                continue;
            }
            if (!out.empty()) {
                out.push_back('\t');
            }
            out.push_back(headers[t].key0);
            out.push_back(headers[t].key1);
            out.push_back(':');
            out.push_back(headers[t].type);
            out.push_back(':');
            out.append(tag_values[t][value_cursors[t]]);
            ++value_cursors[t];
        }
    }

    return results;
}

bool is_known_column(Axf1ColumnId column_id) {
    return std::find(kRequiredColumns.begin(), kRequiredColumns.end(), column_id) !=
           kRequiredColumns.end();
}

bool is_supported_column_codec(Axf1ColumnId column_id, Axf1CodecId codec_id) {
    if (codec_id == Axf1CodecId::raw) {
        return true;
    }
    return (column_id == Axf1ColumnId::qname && codec_id == Axf1CodecId::qname_dict) ||
           (column_id == Axf1ColumnId::pos && codec_id == Axf1CodecId::pos_delta_varint) ||
           (column_id == Axf1ColumnId::flag && codec_id == Axf1CodecId::flag_bitpack) ||
           (column_id == Axf1ColumnId::mapq && codec_id == Axf1CodecId::mapq_rle) ||
           (column_id == Axf1ColumnId::cigar && codec_id == Axf1CodecId::cigar_token) ||
           (column_id == Axf1ColumnId::cigar && codec_id == Axf1CodecId::cigar_dict) ||
           (column_id == Axf1ColumnId::sequence && codec_id == Axf1CodecId::seq_2bit_literal) ||
           (column_id == Axf1ColumnId::quality && codec_id == Axf1CodecId::qual_rle) ||
           (column_id == Axf1ColumnId::quality && codec_id == Axf1CodecId::qual_pack) ||
           (column_id == Axf1ColumnId::quality && codec_id == Axf1CodecId::qual_pack_compressed) ||
           (column_id == Axf1ColumnId::tags && codec_id == Axf1CodecId::tags_per_stream);
}

bool contains_column(const std::vector<Axf1ColumnId>& columns, Axf1ColumnId column_id) {
    return std::find(columns.begin(), columns.end(), column_id) != columns.end();
}

std::expected<void, std::string> validate_column_entries(const std::vector<ColumnEntry>& entries) {
    for (const ColumnEntry& entry : entries) {
        if (!is_known_column(entry.column_id)) {
            return std::unexpected("unknown AXF1 column id");
        }
        if (!is_supported_column_codec(entry.column_id, entry.codec_id)) {
            return std::unexpected("unsupported AXF1 column codec");
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
        if (!is_supported_column_codec(entry.column_id, entry.codec_id)) {
            return std::unexpected("unsupported AXF1 column codec");
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

std::expected<void, std::string>
decode_column_into_chunk(Axf1Chunk& chunk, std::uint32_t record_count,
                         const ColumnEntry& entry,
                         const std::vector<unsigned char>& payload) {
    switch (entry.column_id) {
    case Axf1ColumnId::qname: {
        auto values = entry.codec_id == Axf1CodecId::qname_dict
                          ? decode_qname_dict_column(payload, record_count)
                          : decode_string_column(payload, record_count);
        if (!values) {
            return std::unexpected(values.error());
        }
        for (std::uint32_t i = 0; i < record_count; ++i) {
            chunk.records[i].qname = std::move(values->at(i));
        }
        break;
    }
    case Axf1ColumnId::flag: {
        auto values = entry.codec_id == Axf1CodecId::flag_bitpack
                          ? decode_flag_bitpack_column(payload, record_count)
                          : decode_fixed_column<std::uint16_t>(payload, record_count,
                                                               &Reader::read_u16);
        if (!values) {
            return std::unexpected(values.error());
        }
        for (std::uint32_t i = 0; i < record_count; ++i) {
            chunk.records[i].flag = values->at(i);
        }
        break;
    }
    case Axf1ColumnId::pos: {
        auto values =
            entry.codec_id == Axf1CodecId::pos_delta_varint
                ? decode_pos_delta_varint_column(payload, record_count)
                : decode_fixed_column<std::int32_t>(payload, record_count, &Reader::read_i32);
        if (!values) {
            return std::unexpected(values.error());
        }
        for (std::uint32_t i = 0; i < record_count; ++i) {
            chunk.records[i].pos = values->at(i);
        }
        break;
    }
    case Axf1ColumnId::mapq: {
        auto values =
            entry.codec_id == Axf1CodecId::mapq_rle
                ? decode_mapq_rle_column(payload, record_count)
                : decode_fixed_column<std::uint8_t>(payload, record_count, &Reader::read_u8);
        if (!values) {
            return std::unexpected(values.error());
        }
        for (std::uint32_t i = 0; i < record_count; ++i) {
            chunk.records[i].mapq = values->at(i);
        }
        break;
    }
    case Axf1ColumnId::mate_reference: {
        auto values = decode_string_column(payload, record_count);
        if (!values) {
            return std::unexpected(values.error());
        }
        for (std::uint32_t i = 0; i < record_count; ++i) {
            chunk.records[i].mate_reference = std::move(values->at(i));
        }
        break;
    }
    case Axf1ColumnId::tags: {
        auto values = entry.codec_id == Axf1CodecId::tags_per_stream
                          ? decode_tags_per_stream_column(payload, record_count)
                          : decode_string_column(payload, record_count);
        if (!values) {
            return std::unexpected(values.error());
        }
        for (std::uint32_t i = 0; i < record_count; ++i) {
            chunk.records[i].tags = std::move(values->at(i));
        }
        break;
    }
    case Axf1ColumnId::cigar: {
        std::expected<std::vector<std::string>, std::string> values;
        if (entry.codec_id == Axf1CodecId::cigar_dict) {
            values = decode_cigar_dict_column(payload, record_count);
        } else if (entry.codec_id == Axf1CodecId::cigar_token) {
            values = decode_cigar_token_column(payload, record_count);
        } else {
            values = decode_string_column(payload, record_count);
        }
        if (!values) {
            return std::unexpected(values.error());
        }
        for (std::uint32_t i = 0; i < record_count; ++i) {
            chunk.records[i].cigar = std::move(values->at(i));
        }
        break;
    }
    case Axf1ColumnId::sequence: {
        auto values = entry.codec_id == Axf1CodecId::seq_2bit_literal
                          ? decode_seq_2bit_literal_column(payload, record_count)
                          : decode_string_column(payload, record_count);
        if (!values) {
            return std::unexpected(values.error());
        }
        for (std::uint32_t i = 0; i < record_count; ++i) {
            chunk.records[i].sequence = std::move(values->at(i));
        }
        break;
    }
    case Axf1ColumnId::quality: {
        std::expected<std::vector<std::string>, std::string> values =
            std::unexpected("unsupported AXF1 QUAL codec");
        if (entry.codec_id == Axf1CodecId::qual_rle) {
            values = decode_qual_rle_column(payload, record_count);
        } else if (entry.codec_id == Axf1CodecId::qual_pack) {
            values = decode_qual_pack_column(payload, record_count);
        } else if (entry.codec_id == Axf1CodecId::qual_pack_compressed) {
            auto base_payload =
                decode_compressed_base_payload(payload, Axf1CodecId::qual_pack,
                                               "unsupported AXF1 compressed QUAL base codec");
            if (!base_payload) {
                return std::unexpected(base_payload.error());
            }
            values = decode_qual_pack_column(*base_payload, record_count);
        } else {
            values = decode_string_column(payload, record_count);
        }
        if (!values) {
            return std::unexpected(values.error());
        }
        for (std::uint32_t i = 0; i < record_count; ++i) {
            chunk.records[i].quality = std::move(values->at(i));
        }
        break;
    }
    case Axf1ColumnId::mate_pos:
    case Axf1ColumnId::template_length: {
        auto values =
            decode_fixed_column<std::int32_t>(payload, record_count, &Reader::read_i32);
        if (!values) {
            return std::unexpected(values.error());
        }
        for (std::uint32_t i = 0; i < record_count; ++i) {
            if (entry.column_id == Axf1ColumnId::mate_pos) {
                chunk.records[i].mate_pos = values->at(i);
            } else {
                chunk.records[i].template_length = values->at(i);
            }
        }
        break;
    }
    }
    return {};
}

std::expected<Axf1Chunk, std::string>
decode_chunk_bytes(const std::vector<unsigned char>& chunk_bytes,
                   const Axf1ChunkIndexEntry& index_entry,
                   const std::vector<Axf1ColumnId>& requested_columns,
                   Axf1ChunkReadProfile* profile = nullptr) {
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

    if (profile != nullptr) {
        profile->bytes_read = index_entry.chunk_length;
        profile->total_columns = static_cast<std::uint16_t>(entries.size());
        profile->selected_columns = 0;
        profile->total_payload_bytes = 0;
        profile->selected_payload_bytes = 0;
        for (const ColumnEntry& entry : entries) {
            profile->total_payload_bytes += entry.length;
            if (contains_column(requested_columns, entry.column_id)) {
                profile->selected_columns += 1;
                profile->selected_payload_bytes += entry.length;
            }
        }
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
        auto decode = decode_column_into_chunk(chunk, *record_count, entry, *payload);
        if (!decode) {
            return std::unexpected(decode.error());
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

Axf1FileReader::Axf1FileReader(std::filesystem::path path, Axf1FileIndex index,
                               std::unique_ptr<std::ifstream> stream, std::uint64_t file_size)
    : path_(std::move(path)), index_(std::move(index)), stream_(std::move(stream)),
      file_size_(file_size) {}

Axf1FileReader::Axf1FileReader(Axf1FileReader&&) noexcept = default;
Axf1FileReader& Axf1FileReader::operator=(Axf1FileReader&&) noexcept = default;
Axf1FileReader::~Axf1FileReader() = default;

std::expected<Axf1FileReader, std::string> Axf1FileReader::open(std::filesystem::path path) {
    auto index = read_axf1_index_metadata(path);
    if (!index) {
        return std::unexpected(index.error());
    }
    auto stream = std::make_unique<std::ifstream>(path, std::ios::binary);
    if (!*stream) {
        return std::unexpected("failed to open AXF1 file: " + path.string());
    }
    stream->seekg(0, std::ios::end);
    const auto size = stream->tellg();
    if (size < 0) {
        return std::unexpected("failed to determine AXF1 file size: " + path.string());
    }
    return Axf1FileReader(std::move(path), std::move(*index), std::move(stream),
                          static_cast<std::uint64_t>(size));
}

const Axf1FileIndex& Axf1FileReader::index() const noexcept {
    return index_;
}

std::expected<std::vector<const Axf1ChunkIndexEntry*>, std::string>
Axf1FileReader::query_chunks(std::uint32_t ref_id, std::int32_t start, std::int32_t end) const {
    return index_.query_chunks(ref_id, start, end);
}

std::expected<std::vector<unsigned char>, std::string>
Axf1FileReader::read_range(std::uint64_t offset, std::uint64_t length) {
    if (offset > file_size_ || length > file_size_ - offset) {
        return std::unexpected("AXF1 read range points outside file");
    }
    if (length > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        return std::unexpected("AXF1 read range is too large");
    }
    stream_->seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    if (!*stream_) {
        return std::unexpected("failed to seek AXF1 file");
    }
    std::vector<unsigned char> bytes(static_cast<std::size_t>(length));
    if (!bytes.empty()) {
        stream_->read(reinterpret_cast<char*>(bytes.data()),
                      static_cast<std::streamsize>(bytes.size()));
        if (!*stream_) {
            return std::unexpected("failed to read AXF1 file: " + path_.string());
        }
    }
    return bytes;
}

std::expected<Axf1Chunk, std::string>
Axf1FileReader::read_chunk(const Axf1ChunkIndexEntry& chunk) {
    return read_axf1_chunk(path_, chunk);
}

std::expected<Axf1Chunk, std::string>
Axf1FileReader::read_chunk_profiled(const Axf1ChunkIndexEntry& chunk,
                                    Axf1ChunkReadProfile& profile) {
    return read_axf1_chunk_profiled(path_, chunk, profile);
}

std::expected<Axf1Chunk, std::string>
Axf1FileReader::read_chunk_columns(const Axf1ChunkIndexEntry& chunk,
                                   const std::vector<Axf1ColumnId>& columns) {
    return read_axf1_chunk_columns(path_, chunk, columns);
}

std::expected<Axf1Chunk, std::string>
Axf1FileReader::read_chunk_columns_profiled(const Axf1ChunkIndexEntry& chunk,
                                            const std::vector<Axf1ColumnId>& columns,
                                            Axf1ChunkReadProfile& profile) {
    auto bytes = read_range(chunk.chunk_offset, chunk.chunk_length);
    if (!bytes) {
        return std::unexpected(bytes.error());
    }
    return decode_chunk_bytes(*bytes, chunk, columns, &profile);
}

std::expected<Axf1Chunk, std::string>
Axf1FileReader::read_chunk_columns_selective(const Axf1ChunkIndexEntry& chunk,
                                             const std::vector<Axf1ColumnId>& columns,
                                             Axf1ChunkReadProfile& profile) {
    constexpr std::uint64_t kChunkFixedHeader = 4 + 4 + 4 + 4 + 2; // 18 bytes

    auto header_bytes = read_range(chunk.chunk_offset, kChunkFixedHeader);
    if (!header_bytes) {
        return std::unexpected(header_bytes.error());
    }
    Reader header_reader(*header_bytes);
    auto ref_id = header_reader.read_u32();
    auto start_pos = header_reader.read_i32();
    auto end_pos = header_reader.read_i32();
    auto record_count = header_reader.read_u32();
    auto column_count = header_reader.read_u16();
    if (!ref_id || !start_pos || !end_pos || !record_count || !column_count) {
        return std::unexpected("truncated AXF1 chunk");
    }
    if (*ref_id != chunk.ref_id || *start_pos != chunk.start_pos || *end_pos != chunk.end_pos ||
        *record_count != chunk.record_count) {
        return std::unexpected("AXF1 chunk metadata does not match index");
    }

    const std::uint64_t entries_size =
        static_cast<std::uint64_t>(*column_count) * kColumnEntrySize;
    auto entries_bytes = read_range(chunk.chunk_offset + kChunkFixedHeader, entries_size);
    if (!entries_bytes) {
        return std::unexpected(entries_bytes.error());
    }
    Reader entries_reader(*entries_bytes);
    std::vector<ColumnEntry> entries;
    entries.reserve(*column_count);
    for (std::uint16_t col = 0; col < *column_count; ++col) {
        auto cid = entries_reader.read_u16();
        auto codec = entries_reader.read_u16();
        auto offset = entries_reader.read_u64();
        auto length = entries_reader.read_u64();
        if (!cid || !codec || !offset || !length) {
            return std::unexpected("truncated AXF1 column entry");
        }
        entries.push_back({.column_id = static_cast<Axf1ColumnId>(*cid),
                           .codec_id = static_cast<Axf1CodecId>(*codec),
                           .offset = *offset,
                           .length = *length});
    }

    auto column_validation = validate_selected_column_entries(entries, columns);
    if (!column_validation) {
        return std::unexpected(column_validation.error());
    }

    const std::uint64_t payload_file_offset = chunk.chunk_offset + kChunkFixedHeader + entries_size;
    std::uint64_t bytes_actually_read = kChunkFixedHeader + entries_size;

    profile.bytes_read = 0;
    profile.total_columns = static_cast<std::uint16_t>(entries.size());
    profile.selected_columns = 0;
    profile.total_payload_bytes = 0;
    profile.selected_payload_bytes = 0;
    for (const ColumnEntry& entry : entries) {
        profile.total_payload_bytes += entry.length;
        if (contains_column(columns, entry.column_id)) {
            profile.selected_columns += 1;
            profile.selected_payload_bytes += entry.length;
        }
    }

    Axf1Chunk result{.ref_id = *ref_id, .start_pos = *start_pos, .end_pos = *end_pos};
    result.records.resize(*record_count);

    for (const ColumnEntry& entry : entries) {
        if (!contains_column(columns, entry.column_id)) {
            continue;
        }
        auto payload = read_range(payload_file_offset + entry.offset, entry.length);
        if (!payload) {
            return std::unexpected(payload.error());
        }
        bytes_actually_read += entry.length;

        auto decode = decode_column_into_chunk(result, *record_count, entry, *payload);
        if (!decode) {
            return std::unexpected(decode.error());
        }
    }

    profile.bytes_read = bytes_actually_read;
    return result;
}

std::expected<void, std::string> write_axf1_file(const Axf1File& file,
                                                 const std::filesystem::path& path) {
    return write_axf1_file(file, path, Axf1WriteOptions{});
}

std::expected<void, std::string> write_axf1_file(const Axf1File& file,
                                                 const std::filesystem::path& path,
                                                 const Axf1WriteOptions& options) {
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
        auto bytes = write_chunk(chunk, options);
        if (!bytes) {
            return std::unexpected(bytes.error());
        }
        index_entries.push_back({.ref_id = chunk.ref_id,
                                 .start_pos = chunk.start_pos,
                                 .end_pos = chunk.end_pos,
                                 .record_count = static_cast<std::uint32_t>(chunk.records.size()),
                                 .chunk_offset = chunk_offset,
                                 .chunk_length = static_cast<std::uint64_t>(bytes->size())});
        chunk_bytes.insert(chunk_bytes.end(), bytes->begin(), bytes->end());
        chunk_offset += bytes->size();
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
read_axf1_chunk_profiled(const std::filesystem::path& path, const Axf1ChunkIndexEntry& chunk,
                         Axf1ChunkReadProfile& profile) {
    return read_axf1_chunk_columns_profiled(
        path, chunk, std::vector<Axf1ColumnId>(kRequiredColumns.begin(), kRequiredColumns.end()),
        profile);
}

std::expected<Axf1Chunk, std::string>
read_axf1_chunk_columns(const std::filesystem::path& path, const Axf1ChunkIndexEntry& chunk,
                        const std::vector<Axf1ColumnId>& columns) {
    Axf1ChunkReadProfile unused_profile;
    return read_axf1_chunk_columns_profiled(path, chunk, columns, unused_profile);
}

std::expected<Axf1Chunk, std::string>
read_axf1_chunk_columns_profiled(const std::filesystem::path& path,
                                 const Axf1ChunkIndexEntry& chunk,
                                 const std::vector<Axf1ColumnId>& columns,
                                 Axf1ChunkReadProfile& profile) {
    auto bytes = read_file_range(path, chunk.chunk_offset, chunk.chunk_length);
    if (!bytes) {
        return std::unexpected(bytes.error());
    }
    return decode_chunk_bytes(*bytes, chunk, columns, &profile);
}

std::string format_axf1_sam_record(const Axf1Record& record, const std::string& reference) {
    std::string line;
    line.reserve(record.qname.size() + reference.size() + record.cigar.size() +
                 record.mate_reference.size() + record.sequence.size() + record.quality.size() +
                 record.tags.size() + 64);
    line.append(record.qname);
    line.push_back('\t');
    line.append(std::to_string(record.flag));
    line.push_back('\t');
    line.append(reference);
    line.push_back('\t');
    line.append(std::to_string(record.pos + 1));
    line.push_back('\t');
    line.append(std::to_string(record.mapq));
    line.push_back('\t');
    line.append(record.cigar);
    line.push_back('\t');
    line.append(record.mate_reference);
    line.push_back('\t');
    line.append(std::to_string(record.mate_pos <= 0 ? 0 : record.mate_pos + 1));
    line.push_back('\t');
    line.append(std::to_string(record.template_length));
    line.push_back('\t');
    line.append(record.sequence);
    line.push_back('\t');
    line.append(record.quality);
    if (!record.tags.empty()) {
        line.push_back('\t');
        line.append(record.tags);
    }
    line.push_back('\n');
    return line;
}

} // namespace alignx::format
