#include "format/axf_file.hpp"

#include <algorithm>
#include <fstream>
#include <limits>
#include <string>
#include <utility>

namespace alignx::format {
namespace {

constexpr unsigned char kMagic[] = {'A', 'X', 'F', '0'};
constexpr std::uint32_t kVersion = 1;
constexpr std::uint64_t kHeaderSize = sizeof(kMagic) + sizeof(std::uint32_t) +
                                      sizeof(std::uint32_t) + sizeof(std::uint64_t) +
                                      sizeof(std::uint64_t);

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

std::expected<std::vector<unsigned char>, std::string>
read_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return std::unexpected("failed to open AXF file: " + path.string());
    }

    input.seekg(0, std::ios::end);
    const std::streamoff size = input.tellg();
    if (size < 0) {
        return std::unexpected("failed to determine AXF file size: " + path.string());
    }
    input.seekg(0, std::ios::beg);

    std::vector<unsigned char> bytes(static_cast<std::size_t>(size));
    if (!bytes.empty()) {
        input.read(reinterpret_cast<char*>(bytes.data()), size);
        if (!input) {
            return std::unexpected("failed to read AXF file: " + path.string());
        }
    }
    return bytes;
}

class BinaryReader {
public:
    explicit BinaryReader(const std::vector<unsigned char>& bytes) : bytes_(bytes) {}

    [[nodiscard]] std::expected<void, std::string> expect_magic() {
        if (bytes_.size() < sizeof(kMagic)) {
            return std::unexpected("truncated AXF file");
        }
        for (std::size_t index = 0; index < sizeof(kMagic); ++index) {
            if (bytes_.at(index) != kMagic[index]) {
                return std::unexpected("invalid AXF magic");
            }
        }
        offset_ = sizeof(kMagic);
        return {};
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
        if (bytes_.size() - offset_ < size) {
            return std::unexpected("truncated AXF file");
        }
        std::string value(reinterpret_cast<const char*>(bytes_.data() + offset_), size);
        offset_ += size;
        return value;
    }

    [[nodiscard]] std::expected<std::vector<unsigned char>, std::string>
    read_bytes_at(std::uint64_t offset, std::uint64_t size) const {
        if (offset > bytes_.size() || size > bytes_.size() - static_cast<std::size_t>(offset)) {
            return std::unexpected("AXF block payload points outside file");
        }
        const auto begin = bytes_.begin() + static_cast<std::ptrdiff_t>(offset);
        const auto end = begin + static_cast<std::ptrdiff_t>(size);
        return std::vector<unsigned char>(begin, end);
    }

    void seek(std::size_t offset) noexcept { offset_ = offset; }

    [[nodiscard]] std::size_t offset() const noexcept { return offset_; }
    [[nodiscard]] std::size_t size() const noexcept { return bytes_.size(); }

private:
    template <typename UInt>
    [[nodiscard]] std::expected<UInt, std::string> read_little_endian() {
        if (bytes_.size() - offset_ < sizeof(UInt)) {
            return std::unexpected("truncated AXF file");
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
        unsigned char bytes[sizeof(kMagic)]{};
        auto read = read_exact(bytes, sizeof(bytes));
        if (!read) {
            return std::unexpected(read.error());
        }
        for (std::size_t index = 0; index < sizeof(kMagic); ++index) {
            if (bytes[index] != kMagic[index]) {
                return std::unexpected("invalid AXF magic");
            }
        }
        return {};
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
            return std::unexpected("truncated AXF file");
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
            return std::unexpected("AXF index offset points outside file");
        }
        input_.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
        if (!input_) {
            return std::unexpected("failed to seek AXF file");
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
            return std::unexpected("truncated AXF file");
        }
        input_.read(reinterpret_cast<char*>(output), static_cast<std::streamsize>(size));
        if (!input_) {
            return std::unexpected("failed to read AXF file");
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

std::expected<void, std::string> validate_file(const AxfFile& file) {
    if (file.references.size() > std::numeric_limits<std::uint32_t>::max()) {
        return std::unexpected("too many AXF references");
    }
    if (file.blocks.size() > std::numeric_limits<std::uint64_t>::max()) {
        return std::unexpected("too many AXF blocks");
    }

    for (const AxfReference& reference : file.references) {
        if (reference.name.size() > std::numeric_limits<std::uint16_t>::max()) {
            return std::unexpected("AXF reference name is too long");
        }
    }

    for (const AxfBlock& block : file.blocks) {
        if (block.ref_id >= file.references.size()) {
            return std::unexpected("AXF block reference id out of range");
        }
        if (block.start_pos >= block.end_pos) {
            return std::unexpected("AXF block requires start_pos < end_pos");
        }
    }

    return {};
}

std::expected<void, std::string> validate_index_metadata(const AxfFileIndex& index,
                                                         std::uint64_t file_size,
                                                         std::uint64_t index_offset) {
    for (const AxfReference& reference : index.references) {
        if (reference.name.size() > std::numeric_limits<std::uint16_t>::max()) {
            return std::unexpected("AXF reference name is too long");
        }
    }

    for (const AxfBlockIndexEntry& block : index.blocks) {
        if (block.ref_id >= index.references.size()) {
            return std::unexpected("AXF block reference id out of range");
        }
        if (block.start_pos >= block.end_pos) {
            return std::unexpected("AXF block requires start_pos < end_pos");
        }
        if (block.payload_offset > file_size ||
            block.payload_length > file_size - block.payload_offset) {
            return std::unexpected("AXF block payload points outside file");
        }
        if (block.payload_offset + block.payload_length > index_offset) {
            return std::unexpected("AXF block payload overlaps index");
        }
    }

    return {};
}

void build_reference_block_ranges(AxfFileIndex& index) {
    index.reference_block_ranges.assign(index.references.size(), {});
    std::size_t block_index = 0;
    for (std::size_t ref_id = 0; ref_id < index.references.size(); ++ref_id) {
        const std::size_t begin = block_index;
        while (block_index < index.blocks.size() && index.blocks[block_index].ref_id == ref_id) {
            ++block_index;
        }
        index.reference_block_ranges[ref_id] = {.begin = begin, .end = block_index};
    }
}

} // namespace

bool AxfBlock::overlaps(std::int32_t query_start, std::int32_t query_end) const noexcept {
    return start_pos < query_end && query_start < end_pos;
}

bool AxfBlockIndexEntry::overlaps(std::int32_t query_start, std::int32_t query_end) const noexcept {
    return start_pos < query_end && query_start < end_pos;
}

std::expected<std::vector<const AxfBlock*>, std::string>
AxfFile::query_blocks(std::uint32_t ref_id, std::int32_t start, std::int32_t end) const {
    if (start >= end) {
        return std::unexpected("AXF query requires start < end");
    }
    if (ref_id >= references.size()) {
        return std::unexpected("AXF query reference id out of range");
    }

    std::vector<const AxfBlock*> hits;
    for (const AxfBlock& block : blocks) {
        if (block.ref_id == ref_id && block.overlaps(start, end)) {
            hits.push_back(&block);
        }
    }
    return hits;
}

std::expected<std::vector<const AxfBlockIndexEntry*>, std::string>
AxfFileIndex::query_blocks(std::uint32_t ref_id, std::int32_t start, std::int32_t end) const {
    if (start >= end) {
        return std::unexpected("AXF query requires start < end");
    }
    if (ref_id >= references.size()) {
        return std::unexpected("AXF query reference id out of range");
    }

    std::vector<const AxfBlockIndexEntry*> hits;
    const AxfBlockRange range = reference_block_ranges.size() == references.size()
                                    ? reference_block_ranges.at(ref_id)
                                    : AxfBlockRange{.begin = 0, .end = blocks.size()};
    for (std::size_t index = range.begin; index < range.end; ++index) {
        const AxfBlockIndexEntry& block = blocks.at(index);
        if (block.ref_id == ref_id && block.overlaps(start, end)) {
            hits.push_back(&block);
        }
    }
    return hits;
}

AxfFileReader::AxfFileReader(std::filesystem::path path, AxfFileIndex index)
    : path_(std::move(path)), index_(std::move(index)) {}

std::expected<AxfFileReader, std::string> AxfFileReader::open(std::filesystem::path path) {
    auto index = read_axf_index_metadata(path);
    if (!index) {
        return std::unexpected(index.error());
    }
    return AxfFileReader(std::move(path), std::move(*index));
}

const AxfFileIndex& AxfFileReader::index() const noexcept {
    return index_;
}

std::expected<std::vector<const AxfBlockIndexEntry*>, std::string>
AxfFileReader::query_blocks(std::uint32_t ref_id, std::int32_t start, std::int32_t end) const {
    return index_.query_blocks(ref_id, start, end);
}

std::expected<std::vector<unsigned char>, std::string>
AxfFileReader::read_payload(const AxfBlockIndexEntry& block) const {
    return read_axf_block_payload(path_, block);
}

std::expected<void, std::string> write_axf_file(const AxfFile& file,
                                                const std::filesystem::path& path) {
    auto validation = validate_file(file);
    if (!validation) {
        return std::unexpected(validation.error());
    }

    std::vector<unsigned char> reference_bytes;
    for (const AxfReference& reference : file.references) {
        append_u16(reference_bytes, static_cast<std::uint16_t>(reference.name.size()));
        reference_bytes.insert(reference_bytes.end(), reference.name.begin(), reference.name.end());
        append_u32(reference_bytes, reference.length);
    }

    std::uint64_t payload_offset = kHeaderSize + reference_bytes.size();
    std::vector<unsigned char> payload_bytes;
    struct IndexEntry {
        AxfBlock block;
        std::uint64_t payload_offset = 0;
    };
    std::vector<IndexEntry> index_entries;
    index_entries.reserve(file.blocks.size());

    for (const AxfBlock& block : file.blocks) {
        index_entries.push_back({.block = block, .payload_offset = payload_offset});
        payload_bytes.insert(payload_bytes.end(), block.payload.begin(), block.payload.end());
        payload_offset += block.payload.size();
    }

    const std::uint64_t index_offset = payload_offset;

    std::vector<unsigned char> bytes;
    bytes.insert(bytes.end(), std::begin(kMagic), std::end(kMagic));
    append_u32(bytes, kVersion);
    append_u32(bytes, static_cast<std::uint32_t>(file.references.size()));
    append_u64(bytes, static_cast<std::uint64_t>(file.blocks.size()));
    append_u64(bytes, index_offset);
    bytes.insert(bytes.end(), reference_bytes.begin(), reference_bytes.end());
    bytes.insert(bytes.end(), payload_bytes.begin(), payload_bytes.end());

    for (const IndexEntry& entry : index_entries) {
        append_u32(bytes, entry.block.ref_id);
        append_i32(bytes, entry.block.start_pos);
        append_i32(bytes, entry.block.end_pos);
        append_u32(bytes, entry.block.record_count);
        append_u64(bytes, entry.payload_offset);
        append_u64(bytes, static_cast<std::uint64_t>(entry.block.payload.size()));
    }

    std::ofstream output(path, std::ios::binary);
    if (!output) {
        return std::unexpected("failed to open AXF file for writing: " + path.string());
    }
    output.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    if (!output) {
        return std::unexpected("failed to write AXF file: " + path.string());
    }
    return {};
}

std::expected<AxfFile, std::string> read_axf_file(const std::filesystem::path& path) {
    auto bytes = read_file(path);
    if (!bytes) {
        return std::unexpected(bytes.error());
    }

    BinaryReader reader(*bytes);
    auto magic = reader.expect_magic();
    if (!magic) {
        return std::unexpected(magic.error());
    }

    auto version = reader.read_u32();
    auto ref_count = reader.read_u32();
    auto block_count = reader.read_u64();
    auto index_offset = reader.read_u64();
    if (!version || !ref_count || !block_count || !index_offset) {
        return std::unexpected("truncated AXF file");
    }
    if (*version != kVersion) {
        return std::unexpected("unsupported AXF version");
    }
    if (*index_offset > bytes->size()) {
        return std::unexpected("AXF index offset points outside file");
    }

    AxfFile file;
    file.references.reserve(*ref_count);
    for (std::uint32_t index = 0; index < *ref_count; ++index) {
        auto name_length = reader.read_u16();
        if (!name_length) {
            return std::unexpected(name_length.error());
        }
        auto name = reader.read_string(*name_length);
        auto length = reader.read_u32();
        if (!name || !length) {
            return std::unexpected("truncated AXF file");
        }
        file.references.push_back({.name = *name, .length = *length});
    }

    if (reader.offset() > *index_offset) {
        return std::unexpected("AXF references overlap index");
    }

    reader.seek(static_cast<std::size_t>(*index_offset));
    file.blocks.reserve(static_cast<std::size_t>(*block_count));
    for (std::uint64_t index = 0; index < *block_count; ++index) {
        auto ref_id = reader.read_u32();
        auto start_pos = reader.read_i32();
        auto end_pos = reader.read_i32();
        auto record_count = reader.read_u32();
        auto payload_offset = reader.read_u64();
        auto payload_length = reader.read_u64();
        if (!ref_id || !start_pos || !end_pos || !record_count || !payload_offset ||
            !payload_length) {
            return std::unexpected("truncated AXF file");
        }

        auto payload = reader.read_bytes_at(*payload_offset, *payload_length);
        if (!payload) {
            return std::unexpected(payload.error());
        }
        file.blocks.push_back({.ref_id = *ref_id,
                               .start_pos = *start_pos,
                               .end_pos = *end_pos,
                               .record_count = *record_count,
                               .payload = std::move(*payload)});
    }

    if (reader.offset() != reader.size()) {
        return std::unexpected("unexpected trailing bytes in AXF file");
    }

    auto validation = validate_file(file);
    if (!validation) {
        return std::unexpected(validation.error());
    }
    std::sort(file.blocks.begin(), file.blocks.end(), [](const AxfBlock& lhs, const AxfBlock& rhs) {
        if (lhs.ref_id != rhs.ref_id) {
            return lhs.ref_id < rhs.ref_id;
        }
        if (lhs.start_pos != rhs.start_pos) {
            return lhs.start_pos < rhs.start_pos;
        }
        return lhs.end_pos < rhs.end_pos;
    });
    return file;
}

std::expected<AxfFileIndex, std::string>
read_axf_index_metadata(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return std::unexpected("failed to open AXF file: " + path.string());
    }

    input.seekg(0, std::ios::end);
    const std::streamoff size = input.tellg();
    if (size < 0) {
        return std::unexpected("failed to determine AXF file size: " + path.string());
    }
    input.seekg(0, std::ios::beg);

    StreamReader reader(input, static_cast<std::uint64_t>(size));
    auto magic = reader.expect_magic();
    if (!magic) {
        return std::unexpected(magic.error());
    }

    auto version = reader.read_u32();
    auto ref_count = reader.read_u32();
    auto block_count = reader.read_u64();
    auto index_offset = reader.read_u64();
    if (!version || !ref_count || !block_count || !index_offset) {
        return std::unexpected("truncated AXF file");
    }
    if (*version != kVersion) {
        return std::unexpected("unsupported AXF version");
    }
    if (*index_offset > reader.size()) {
        return std::unexpected("AXF index offset points outside file");
    }

    AxfFileIndex index;
    index.references.reserve(*ref_count);
    for (std::uint32_t ref_id = 0; ref_id < *ref_count; ++ref_id) {
        auto name_length = reader.read_u16();
        if (!name_length) {
            return std::unexpected(name_length.error());
        }
        auto name = reader.read_string(*name_length);
        auto length = reader.read_u32();
        if (!name || !length) {
            return std::unexpected("truncated AXF file");
        }
        index.references.push_back({.name = *name, .length = *length});
    }

    if (reader.offset() > *index_offset) {
        return std::unexpected("AXF references overlap index");
    }

    auto seek = reader.seek(*index_offset);
    if (!seek) {
        return std::unexpected(seek.error());
    }

    index.blocks.reserve(static_cast<std::size_t>(*block_count));
    for (std::uint64_t block_index = 0; block_index < *block_count; ++block_index) {
        auto ref_id = reader.read_u32();
        auto start_pos = reader.read_i32();
        auto end_pos = reader.read_i32();
        auto record_count = reader.read_u32();
        auto payload_offset = reader.read_u64();
        auto payload_length = reader.read_u64();
        if (!ref_id || !start_pos || !end_pos || !record_count || !payload_offset ||
            !payload_length) {
            return std::unexpected("truncated AXF file");
        }
        index.blocks.push_back({.ref_id = *ref_id,
                                .start_pos = *start_pos,
                                .end_pos = *end_pos,
                                .record_count = *record_count,
                                .payload_offset = *payload_offset,
                                .payload_length = *payload_length});
    }

    if (reader.offset() != reader.size()) {
        return std::unexpected("unexpected trailing bytes in AXF file");
    }

    auto validation = validate_index_metadata(index, reader.size(), *index_offset);
    if (!validation) {
        return std::unexpected(validation.error());
    }
    std::sort(index.blocks.begin(), index.blocks.end(),
              [](const AxfBlockIndexEntry& lhs, const AxfBlockIndexEntry& rhs) {
                  if (lhs.ref_id != rhs.ref_id) {
                      return lhs.ref_id < rhs.ref_id;
                  }
                  if (lhs.start_pos != rhs.start_pos) {
                      return lhs.start_pos < rhs.start_pos;
                  }
                  return lhs.end_pos < rhs.end_pos;
              });
    build_reference_block_ranges(index);
    return index;
}

std::expected<std::vector<unsigned char>, std::string>
read_axf_block_payload(const std::filesystem::path& path, const AxfBlockIndexEntry& block) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return std::unexpected("failed to open AXF file: " + path.string());
    }

    input.seekg(0, std::ios::end);
    const std::streamoff size = input.tellg();
    if (size < 0) {
        return std::unexpected("failed to determine AXF file size: " + path.string());
    }
    const auto file_size = static_cast<std::uint64_t>(size);
    if (block.payload_offset > file_size ||
        block.payload_length > file_size - block.payload_offset) {
        return std::unexpected("AXF block payload points outside file");
    }
    if (block.payload_length >
        static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        return std::unexpected("AXF block payload is too large");
    }

    input.seekg(static_cast<std::streamoff>(block.payload_offset), std::ios::beg);
    if (!input) {
        return std::unexpected("failed to seek AXF file");
    }

    std::vector<unsigned char> payload(static_cast<std::size_t>(block.payload_length));
    if (!payload.empty()) {
        input.read(reinterpret_cast<char*>(payload.data()),
                   static_cast<std::streamsize>(payload.size()));
        if (!input) {
            return std::unexpected("failed to read AXF file");
        }
    }
    return payload;
}

} // namespace alignx::format
