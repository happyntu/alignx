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

struct Axf1File {
    std::vector<Axf1Reference> references;
    std::vector<Axf1Chunk> chunks;
};

[[nodiscard]] std::expected<void, std::string> write_axf1_file(const Axf1File& file,
                                                               const std::filesystem::path& path);

[[nodiscard]] std::expected<Axf1File, std::string>
read_axf1_file(const std::filesystem::path& path);

} // namespace alignx::format
