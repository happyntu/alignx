#include "index/csi_reader.hpp"

#include <zlib.h>

#include <array>
#include <fstream>
#include <utility>
#include <vector>

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

    [[nodiscard]] std::expected<std::vector<unsigned char>, std::string>
    read_bytes(std::size_t count) {
        if (bytes_.size() - offset_ < count) {
            return std::unexpected("truncated CSI file");
        }
        std::vector<unsigned char> out(bytes_.begin() + static_cast<std::ptrdiff_t>(offset_),
                                       bytes_.begin() +
                                           static_cast<std::ptrdiff_t>(offset_ + count));
        offset_ += count;
        return out;
    }

    [[nodiscard]] bool empty() const noexcept { return offset_ == bytes_.size(); }

private:
    template <typename UInt>
    [[nodiscard]] std::expected<UInt, std::string> read_little_endian() {
        if (bytes_.size() - offset_ < sizeof(UInt)) {
            return std::unexpected("truncated CSI file");
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
read_gzip_or_plain_file(const std::filesystem::path& path) {
    gzFile file = gzopen(path.string().c_str(), "rb");
    if (file == nullptr) {
        return std::unexpected("failed to open CSI file: " + path.string());
    }

    std::vector<unsigned char> bytes;
    std::array<unsigned char, 8192> buffer{};
    for (;;) {
        const int read_count = gzread(file, reinterpret_cast<void*>(buffer.data()),
                                      static_cast<unsigned int>(buffer.size()));
        if (read_count < 0) {
            int error_number = 0;
            const char* message = gzerror(file, &error_number);
            gzclose(file);
            return std::unexpected(std::string("failed to decompress CSI file: ") +
                                   (message != nullptr ? message : path.string()));
        }
        if (read_count == 0) {
            break;
        }
        bytes.insert(bytes.end(), buffer.begin(), buffer.begin() + read_count);
    }

    if (gzclose(file) != Z_OK) {
        return std::unexpected("failed to close CSI file after reading: " + path.string());
    }
    return bytes;
}

std::expected<std::uint32_t, std::string> read_count(BinaryReader& reader, const char* field_name) {
    auto value = reader.read_i32();
    if (!value) {
        return std::unexpected(value.error());
    }
    if (*value < 0) {
        return std::unexpected(std::string("negative ") + field_name + " in CSI file");
    }
    return static_cast<std::uint32_t>(*value);
}

std::expected<std::int32_t, std::string> read_nonnegative_i32(BinaryReader& reader,
                                                              const char* field_name) {
    auto value = reader.read_i32();
    if (!value) {
        return std::unexpected(value.error());
    }
    if (*value < 0) {
        return std::unexpected(std::string("negative ") + field_name + " in CSI file");
    }
    return *value;
}

} // namespace

std::size_t CsiIndex::reference_count() const noexcept {
    return references.size();
}

std::expected<CsiIndex, std::string> read_csi_index(const std::filesystem::path& path) {
    auto bytes = read_gzip_or_plain_file(path);
    if (!bytes) {
        return std::unexpected(bytes.error());
    }

    if (bytes->size() < 20) {
        return std::unexpected("truncated CSI file");
    }
    if ((*bytes)[0] != 'C' || (*bytes)[1] != 'S' || (*bytes)[2] != 'I' || (*bytes)[3] != 1) {
        return std::unexpected("invalid CSI magic: " + path.string());
    }

    BinaryReader reader(std::move(*bytes));
    (void)reader.read_u32();

    auto min_shift = read_nonnegative_i32(reader, "min_shift");
    if (!min_shift) {
        return std::unexpected(min_shift.error());
    }
    auto depth = read_nonnegative_i32(reader, "depth");
    if (!depth) {
        return std::unexpected(depth.error());
    }
    auto l_aux = read_count(reader, "aux length");
    if (!l_aux) {
        return std::unexpected(l_aux.error());
    }
    auto aux = reader.read_bytes(*l_aux);
    if (!aux) {
        return std::unexpected(aux.error());
    }
    auto n_ref = read_count(reader, "reference count");
    if (!n_ref) {
        return std::unexpected(n_ref.error());
    }

    CsiIndex index;
    index.min_shift = *min_shift;
    index.depth = *depth;
    index.aux = std::move(*aux);
    index.references.reserve(*n_ref);

    for (std::uint32_t ref_id = 0; ref_id < *n_ref; ++ref_id) {
        auto n_bin = read_count(reader, "bin count");
        if (!n_bin) {
            return std::unexpected(n_bin.error());
        }

        CsiReference reference;
        reference.bins.reserve(*n_bin);

        for (std::uint32_t bin_index = 0; bin_index < *n_bin; ++bin_index) {
            auto bin_id = reader.read_u32();
            if (!bin_id) {
                return std::unexpected(bin_id.error());
            }
            auto loffset = reader.read_u64();
            if (!loffset) {
                return std::unexpected(loffset.error());
            }
            auto n_chunk = read_count(reader, "chunk count");
            if (!n_chunk) {
                return std::unexpected(n_chunk.error());
            }

            CsiBin bin{.id = *bin_id, .loffset = *loffset};
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
                bin.chunks.push_back(CsiChunk{.begin = *begin, .end = *end});
            }
            reference.bins.push_back(std::move(bin));
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
        return std::unexpected("unexpected trailing bytes in CSI file");
    }

    return index;
}

} // namespace alignx::index
