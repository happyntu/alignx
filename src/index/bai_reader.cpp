#include "index/bai_reader.hpp"

#include <array>
#include <fstream>
#include <limits>

namespace alignx::index {
namespace {

class BinaryReader {
public:
    explicit BinaryReader(std::vector<unsigned char> bytes) : bytes_(std::move(bytes)) {}

    [[nodiscard]] std::expected<std::uint32_t, std::string> read_u32() {
        auto value = read_little_endian<std::uint32_t>();
        if (!value) {
            return std::unexpected(value.error());
        }
        return *value;
    }

    [[nodiscard]] std::expected<std::uint64_t, std::string> read_u64() {
        auto value = read_little_endian<std::uint64_t>();
        if (!value) {
            return std::unexpected(value.error());
        }
        return *value;
    }

    [[nodiscard]] std::expected<std::int32_t, std::string> read_i32() {
        auto value = read_little_endian<std::uint32_t>();
        if (!value) {
            return std::unexpected(value.error());
        }
        return static_cast<std::int32_t>(*value);
    }

    [[nodiscard]] bool empty() const noexcept { return offset_ == bytes_.size(); }

private:
    template <typename UInt>
    [[nodiscard]] std::expected<UInt, std::string> read_little_endian() {
        if (bytes_.size() - offset_ < sizeof(UInt)) {
            return std::unexpected("truncated BAI file");
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
        return std::unexpected("failed to open BAI file: " + path.string());
    }

    input.seekg(0, std::ios::end);
    const std::streamoff size = input.tellg();
    if (size < 0) {
        return std::unexpected("failed to determine BAI file size: " + path.string());
    }
    input.seekg(0, std::ios::beg);

    std::vector<unsigned char> bytes(static_cast<std::size_t>(size));
    if (!bytes.empty()) {
        input.read(reinterpret_cast<char*>(bytes.data()), size);
        if (!input) {
            return std::unexpected("failed to read BAI file: " + path.string());
        }
    }
    return bytes;
}

std::expected<std::uint32_t, std::string> read_count(BinaryReader& reader, const char* field_name) {
    auto value = reader.read_i32();
    if (!value) {
        return std::unexpected(value.error());
    }
    if (*value < 0) {
        return std::unexpected(std::string("negative ") + field_name + " in BAI file");
    }
    return static_cast<std::uint32_t>(*value);
}

} // namespace

std::size_t BaiIndex::reference_count() const noexcept {
    return references.size();
}

std::expected<BaiIndex, std::string> read_bai_index(const std::filesystem::path& path) {
    auto bytes = read_file(path);
    if (!bytes) {
        return std::unexpected(bytes.error());
    }

    if (bytes->size() < 8) {
        return std::unexpected("truncated BAI file");
    }
    if ((*bytes)[0] != 'B' || (*bytes)[1] != 'A' || (*bytes)[2] != 'I' || (*bytes)[3] != 1) {
        return std::unexpected("invalid BAI magic: " + path.string());
    }

    BinaryReader reader(std::move(*bytes));
    (void)reader.read_u32();

    auto n_ref = read_count(reader, "reference count");
    if (!n_ref) {
        return std::unexpected(n_ref.error());
    }

    BaiIndex index;
    index.references.reserve(*n_ref);

    for (std::uint32_t ref_id = 0; ref_id < *n_ref; ++ref_id) {
        auto n_bin = read_count(reader, "bin count");
        if (!n_bin) {
            return std::unexpected(n_bin.error());
        }

        BaiReference reference;
        reference.bins.reserve(*n_bin);

        for (std::uint32_t bin_index = 0; bin_index < *n_bin; ++bin_index) {
            auto bin_id = reader.read_u32();
            if (!bin_id) {
                return std::unexpected(bin_id.error());
            }
            auto n_chunk = read_count(reader, "chunk count");
            if (!n_chunk) {
                return std::unexpected(n_chunk.error());
            }

            BaiBin bin{.id = *bin_id};
            bin.chunks.reserve(*n_chunk);
            for (std::uint32_t chunk_index = 0; chunk_index < *n_chunk; ++chunk_index) {
                auto begin = reader.read_u64();
                if (!begin) {
                    return std::unexpected(begin.error());
                }
                auto end = reader.read_u64();
                if (!end) {
                    return std::unexpected(end.error());
                }
                bin.chunks.push_back(BaiChunk{.begin = *begin, .end = *end});
            }
            reference.bins.push_back(std::move(bin));
        }

        auto n_intv = read_count(reader, "linear index interval count");
        if (!n_intv) {
            return std::unexpected(n_intv.error());
        }
        reference.linear_offsets.reserve(*n_intv);
        for (std::uint32_t interval_index = 0; interval_index < *n_intv; ++interval_index) {
            auto offset = reader.read_u64();
            if (!offset) {
                return std::unexpected(offset.error());
            }
            reference.linear_offsets.push_back(*offset);
        }

        index.references.push_back(std::move(reference));
    }

    if (!reader.empty()) {
        auto n_no_coor = reader.read_u64();
        if (!n_no_coor) {
            return std::unexpected(n_no_coor.error());
        }
        index.unplaced_unmapped_count = *n_no_coor;
    }

    if (!reader.empty()) {
        return std::unexpected("unexpected trailing bytes in BAI file");
    }

    return index;
}

} // namespace alignx::index
