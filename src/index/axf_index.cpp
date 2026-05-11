#include "index/axf_index.hpp"

#include <zlib.h>

#include <algorithm>
#include <fstream>
#include <limits>

namespace alignx::index {
namespace {

constexpr unsigned char kMagic[] = {'A', 'X', 'I', '1', '\0'};

void append_u32(std::vector<unsigned char>& bytes, std::uint32_t value) {
    for (std::size_t byte = 0; byte < sizeof(value); ++byte) {
        bytes.push_back(static_cast<unsigned char>((value >> (byte * 8U)) & 0xFFU));
    }
}

void append_u64(std::vector<unsigned char>& bytes, std::uint64_t value) {
    for (std::size_t byte = 0; byte < sizeof(value); ++byte) {
        bytes.push_back(static_cast<unsigned char>((value >> (byte * 8U)) & 0xFFU));
    }
}

std::uint32_t crc32_bytes(const std::vector<unsigned char>& bytes) {
    uLong crc = crc32(0L, Z_NULL, 0);
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const std::size_t remaining = bytes.size() - offset;
        const auto chunk_size =
            static_cast<uInt>(std::min<std::size_t>(remaining, std::numeric_limits<uInt>::max()));
        crc = crc32(crc, bytes.data() + offset, chunk_size);
        offset += chunk_size;
    }
    return static_cast<std::uint32_t>(crc);
}

class BinaryReader {
public:
    explicit BinaryReader(std::vector<unsigned char> bytes) : bytes_(std::move(bytes)) {}

    [[nodiscard]] std::expected<void, std::string> expect_magic() {
        if (bytes_.size() < sizeof(kMagic)) {
            return std::unexpected("truncated AXF index file");
        }
        for (std::size_t index = 0; index < sizeof(kMagic); ++index) {
            if (bytes_.at(index) != kMagic[index]) {
                return std::unexpected("invalid AXF index magic");
            }
        }
        offset_ = sizeof(kMagic);
        return {};
    }

    [[nodiscard]] std::expected<std::uint32_t, std::string> read_u32() {
        return read_little_endian<std::uint32_t>();
    }

    [[nodiscard]] std::expected<std::uint64_t, std::string> read_u64() {
        return read_little_endian<std::uint64_t>();
    }

    [[nodiscard]] std::size_t offset() const noexcept { return offset_; }
    [[nodiscard]] std::size_t size() const noexcept { return bytes_.size(); }
    [[nodiscard]] const std::vector<unsigned char>& bytes() const noexcept { return bytes_; }

private:
    template <typename UInt>
    [[nodiscard]] std::expected<UInt, std::string> read_little_endian() {
        if (bytes_.size() - offset_ < sizeof(UInt)) {
            return std::unexpected("truncated AXF index file");
        }

        UInt value = 0;
        for (std::size_t byte = 0; byte < sizeof(UInt); ++byte) {
            value |= static_cast<UInt>(bytes_.at(offset_ + byte)) << (byte * 8U);
        }
        offset_ += sizeof(UInt);
        return value;
    }

    std::vector<unsigned char> bytes_;
    std::size_t offset_ = 0;
};

std::expected<std::vector<unsigned char>, std::string>
read_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return std::unexpected("failed to open AXF index file: " + path.string());
    }

    input.seekg(0, std::ios::end);
    const std::streamoff size = input.tellg();
    if (size < 0) {
        return std::unexpected("failed to determine AXF index file size: " + path.string());
    }
    input.seekg(0, std::ios::beg);

    std::vector<unsigned char> bytes(static_cast<std::size_t>(size));
    if (!bytes.empty()) {
        input.read(reinterpret_cast<char*>(bytes.data()), size);
        if (!input) {
            return std::unexpected("failed to read AXF index file: " + path.string());
        }
    }
    return bytes;
}

std::expected<void, std::string> validate_query_bounds(std::uint32_t start, std::uint32_t end) {
    if (start >= end) {
        return std::unexpected("AXF index query requires start < end");
    }
    return {};
}

} // namespace

bool AxfInterval::overlaps(std::uint32_t query_start, std::uint32_t query_end) const noexcept {
    return start < query_end && query_start < end;
}

AxfIndex::AxfIndex(std::uint32_t reference_count) : references_(reference_count) {}

std::uint32_t AxfIndex::reference_count() const noexcept {
    return static_cast<std::uint32_t>(references_.size());
}

const std::vector<AxfInterval>& AxfIndex::intervals(std::uint32_t ref_id) const {
    return references_.at(ref_id);
}

void AxfIndex::add_interval(std::uint32_t ref_id, AxfInterval interval) {
    if (ref_id >= references_.size()) {
        references_.resize(static_cast<std::size_t>(ref_id) + 1);
    }
    references_.at(ref_id).push_back(interval);
}

std::expected<void, std::string> AxfIndex::sort_and_validate() {
    for (auto& intervals : references_) {
        std::sort(intervals.begin(), intervals.end(),
                  [](const AxfInterval& lhs, const AxfInterval& rhs) {
                      if (lhs.start != rhs.start) {
                          return lhs.start < rhs.start;
                      }
                      if (lhs.end != rhs.end) {
                          return lhs.end < rhs.end;
                      }
                      return lhs.chunk_offset < rhs.chunk_offset;
                  });

        for (const AxfInterval& interval : intervals) {
            if (interval.start >= interval.end) {
                return std::unexpected("AXF index interval requires start < end");
            }
        }
    }
    return {};
}

std::expected<std::vector<AxfInterval>, std::string>
AxfIndex::query(std::uint32_t ref_id, std::uint32_t start, std::uint32_t end) const {
    auto bounds = validate_query_bounds(start, end);
    if (!bounds) {
        return std::unexpected(bounds.error());
    }
    if (ref_id >= references_.size()) {
        return std::unexpected("AXF index reference id out of range");
    }

    std::vector<AxfInterval> result;
    for (const AxfInterval& interval : references_.at(ref_id)) {
        if (interval.start >= end) {
            break;
        }
        if (interval.overlaps(start, end)) {
            result.push_back(interval);
        }
    }
    return result;
}

std::expected<void, std::string> write_axf_index(const AxfIndex& index,
                                                 const std::filesystem::path& path) {
    std::vector<unsigned char> bytes;
    bytes.insert(bytes.end(), std::begin(kMagic), std::end(kMagic));
    append_u32(bytes, index.reference_count());

    for (std::uint32_t ref_id = 0; ref_id < index.reference_count(); ++ref_id) {
        const auto& intervals = index.intervals(ref_id);
        if (intervals.size() > std::numeric_limits<std::uint32_t>::max()) {
            return std::unexpected("too many AXF index intervals for one reference");
        }
        append_u32(bytes, static_cast<std::uint32_t>(intervals.size()));
        for (const AxfInterval& interval : intervals) {
            if (interval.start >= interval.end) {
                return std::unexpected("AXF index interval requires start < end");
            }
            append_u32(bytes, interval.start);
            append_u32(bytes, interval.end);
            append_u64(bytes, interval.chunk_offset);
            append_u64(bytes, interval.column_index_offset);
        }
    }

    append_u32(bytes, crc32_bytes(bytes));

    std::ofstream output(path, std::ios::binary);
    if (!output) {
        return std::unexpected("failed to open AXF index file for writing: " + path.string());
    }
    output.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    if (!output) {
        return std::unexpected("failed to write AXF index file: " + path.string());
    }
    return {};
}

std::expected<AxfIndex, std::string> read_axf_index(const std::filesystem::path& path) {
    auto bytes = read_file(path);
    if (!bytes) {
        return std::unexpected(bytes.error());
    }
    if (bytes->size() < sizeof(kMagic) + sizeof(std::uint32_t) + sizeof(std::uint32_t)) {
        return std::unexpected("truncated AXF index file");
    }

    const std::uint32_t expected_crc = crc32_bytes(
        std::vector<unsigned char>(bytes->begin(), bytes->end() - sizeof(std::uint32_t)));
    BinaryReader footer_reader(
        std::vector<unsigned char>(bytes->end() - sizeof(std::uint32_t), bytes->end()));
    auto actual_crc = footer_reader.read_u32();
    if (!actual_crc) {
        return std::unexpected(actual_crc.error());
    }
    if (*actual_crc != expected_crc) {
        return std::unexpected("AXF index CRC mismatch");
    }

    BinaryReader reader(std::move(*bytes));
    auto magic = reader.expect_magic();
    if (!magic) {
        return std::unexpected(magic.error());
    }

    auto ref_count = reader.read_u32();
    if (!ref_count) {
        return std::unexpected(ref_count.error());
    }

    AxfIndex index(*ref_count);
    for (std::uint32_t ref_id = 0; ref_id < *ref_count; ++ref_id) {
        auto interval_count = reader.read_u32();
        if (!interval_count) {
            return std::unexpected(interval_count.error());
        }

        for (std::uint32_t interval_index = 0; interval_index < *interval_count; ++interval_index) {
            auto start = reader.read_u32();
            auto end = reader.read_u32();
            auto chunk_offset = reader.read_u64();
            auto column_index_offset = reader.read_u64();
            if (!start || !end || !chunk_offset || !column_index_offset) {
                return std::unexpected("truncated AXF index file");
            }
            index.add_interval(ref_id, AxfInterval{.start = *start,
                                                   .end = *end,
                                                   .chunk_offset = *chunk_offset,
                                                   .column_index_offset = *column_index_offset});
        }
    }

    if (reader.offset() != reader.size() - sizeof(std::uint32_t)) {
        return std::unexpected("unexpected trailing bytes in AXF index file");
    }

    auto validation = index.sort_and_validate();
    if (!validation) {
        return std::unexpected(validation.error());
    }
    return index;
}

} // namespace alignx::index
