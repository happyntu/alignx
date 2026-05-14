#include "convert/bam_to_axf.hpp"

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "format/axf1_file.hpp"
#include "format/axf_file.hpp"
#include "io/bam_reader.hpp"
#include "query/region.hpp"

namespace alignx::convert {
namespace {

struct PendingBlock {
    bool has_records = false;
    std::int32_t start_pos = -1;
    std::int32_t end_pos = -1;
    std::uint32_t record_count = 0;
    std::vector<unsigned char> payload;
};

struct PendingAxf1Chunk {
    bool has_records = false;
    std::int32_t start_pos = -1;
    std::int32_t end_pos = -1;
    std::vector<format::Axf1Record> records;
};

// Deliberately tiny for Phase 1 toy correctness coverage. Production AXF1
// chunk sizing should be replaced with a byte/span/record hybrid policy.
constexpr std::size_t kAxf1MvpMaxRecordsPerChunk = 1;

void append_line(PendingBlock& block, std::string_view line, const io::BamRecord& record) {
    if (!block.has_records) {
        block.start_pos = record.position;
        block.end_pos = record.end_position;
        block.has_records = true;
    } else {
        if (record.position < block.start_pos) {
            block.start_pos = record.position;
        }
        if (record.end_position > block.end_pos) {
            block.end_pos = record.end_position;
        }
    }

    block.record_count += 1;
    block.payload.insert(block.payload.end(), line.begin(), line.end());
    if (line.empty() || line.back() != '\n') {
        block.payload.push_back('\n');
    }
}

std::string_view trim_line_ending(std::string_view line) {
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) {
        line.remove_suffix(1);
    }
    return line;
}

std::vector<std::string_view> split_sam_fields(std::string_view line) {
    std::vector<std::string_view> fields;
    fields.reserve(12);
    line = trim_line_ending(line);
    std::size_t start = 0;
    for (;;) {
        const std::size_t tab = line.find('\t', start);
        if (tab == std::string_view::npos) {
            fields.push_back(line.substr(start));
            break;
        }
        fields.push_back(line.substr(start, tab - start));
        start = tab + 1;
    }
    return fields;
}

template <typename Int>
std::expected<Int, std::string> parse_int_field(std::string_view text, std::string_view label) {
    std::int64_t value = 0;
    const char* first = text.data();
    const char* last = text.data() + text.size();
    const auto parse = std::from_chars(first, last, value);
    if (parse.ec != std::errc{} || parse.ptr != last ||
        value < static_cast<std::int64_t>(std::numeric_limits<Int>::min()) ||
        value > static_cast<std::int64_t>(std::numeric_limits<Int>::max())) {
        return std::unexpected("invalid SAM " + std::string(label));
    }
    return static_cast<Int>(value);
}

std::string join_optional_tags(const std::vector<std::string_view>& fields) {
    std::string tags;
    for (std::size_t index = 11; index < fields.size(); ++index) {
        if (!tags.empty()) {
            tags.push_back('\t');
        }
        tags.append(fields[index]);
    }
    return tags;
}

std::expected<format::Axf1Record, std::string> parse_axf1_record(std::string_view sam_line) {
    const std::vector<std::string_view> fields = split_sam_fields(sam_line);
    if (fields.size() < 11) {
        return std::unexpected("SAM record has fewer than 11 fields");
    }

    auto flag = parse_int_field<std::uint16_t>(fields[1], "FLAG");
    auto pos = parse_int_field<std::int32_t>(fields[3], "POS");
    auto mapq = parse_int_field<std::uint8_t>(fields[4], "MAPQ");
    auto mate_pos = parse_int_field<std::int32_t>(fields[7], "PNEXT");
    auto template_length = parse_int_field<std::int32_t>(fields[8], "TLEN");
    if (!flag) {
        return std::unexpected(flag.error());
    }
    if (!pos) {
        return std::unexpected(pos.error());
    }
    if (!mapq) {
        return std::unexpected(mapq.error());
    }
    if (!mate_pos) {
        return std::unexpected(mate_pos.error());
    }
    if (!template_length) {
        return std::unexpected(template_length.error());
    }
    if (*pos <= 0) {
        return std::unexpected("SAM POS must be positive for mapped AXF1 records");
    }
    if (*mate_pos < 0) {
        return std::unexpected("SAM PNEXT must be non-negative");
    }

    return format::Axf1Record{.qname = std::string(fields[0]),
                              .flag = *flag,
                              .pos = *pos - 1,
                              .mapq = *mapq,
                              .cigar = std::string(fields[5]),
                              .mate_reference = std::string(fields[6]),
                              .mate_pos = *mate_pos == 0 ? 0 : *mate_pos - 1,
                              .template_length = *template_length,
                              .sequence = std::string(fields[9]),
                              .quality = std::string(fields[10]),
                              .tags = join_optional_tags(fields)};
}

void append_axf1_record(PendingAxf1Chunk& chunk, format::Axf1Record record,
                        const io::BamRecord& bam_record) {
    if (!chunk.has_records) {
        chunk.start_pos = bam_record.position;
        chunk.end_pos = bam_record.end_position;
        chunk.has_records = true;
    } else {
        if (bam_record.position < chunk.start_pos) {
            chunk.start_pos = bam_record.position;
        }
        if (bam_record.end_position > chunk.end_pos) {
            chunk.end_pos = bam_record.end_position;
        }
    }
    chunk.records.push_back(std::move(record));
}

void flush_axf1_chunk(format::Axf1File& file, std::uint32_t ref_id, PendingAxf1Chunk& chunk) {
    if (!chunk.has_records) {
        return;
    }

    file.chunks.push_back({.ref_id = ref_id,
                           .start_pos = chunk.start_pos,
                           .end_pos = chunk.end_pos,
                           .records = std::move(chunk.records)});
    chunk = PendingAxf1Chunk{};
}

bool overlaps_region(const io::BamRecord& record, const query::SamRegion& region) {
    return record.reference_name == region.reference && record.position < region.end &&
           region.start < record.end_position;
}

} // namespace

std::expected<void, std::string> convert_bam_to_axf_mvp(const std::filesystem::path& input_bam,
                                                        const std::filesystem::path& output_axf,
                                                        const std::optional<std::string>& region) {
    std::optional<query::SamRegion> parsed_region;
    if (region.has_value()) {
        auto parsed = query::parse_sam_region(*region);
        if (!parsed) {
            return std::unexpected(parsed.error());
        }
        parsed_region = std::move(*parsed);
    }

    auto reader = io::BamReader::open(input_bam);
    if (!reader) {
        return std::unexpected(reader.error());
    }

    if (region.has_value()) {
        auto fetch = reader->fetch(*region);
        if (!fetch) {
            return std::unexpected(fetch.error());
        }
    }

    auto references = reader->references();
    if (!references) {
        return std::unexpected(references.error());
    }

    format::AxfFile file;
    file.references.reserve(references->size());
    for (const io::BamReference& reference : *references) {
        file.references.push_back({.name = reference.name, .length = reference.length});
    }

    std::vector<PendingBlock> blocks(references->size());
    for (;;) {
        auto record_view = reader->next_record_view();
        if (!record_view) {
            return std::unexpected(record_view.error());
        }
        if (!record_view->has_value()) {
            break;
        }
        const io::BamRecordView& view = **record_view;
        if (view.record.is_unmapped() || view.record.position < 0 ||
            view.record.end_position <= view.record.position) {
            continue;
        }
        if (parsed_region.has_value() && !overlaps_region(view.record, *parsed_region)) {
            continue;
        }

        std::size_t ref_id = references->size();
        for (std::size_t index = 0; index < references->size(); ++index) {
            if (references->at(index).name == view.record.reference_name) {
                ref_id = index;
                break;
            }
        }
        if (ref_id >= references->size()) {
            return std::unexpected("failed to map BAM record reference name to reference id");
        }
        append_line(blocks.at(ref_id), view.sam_line, view.record);
    }

    for (std::size_t ref_id = 0; ref_id < blocks.size(); ++ref_id) {
        const PendingBlock& block = blocks[ref_id];
        if (!block.has_records) {
            continue;
        }
        file.blocks.push_back({.ref_id = static_cast<std::uint32_t>(ref_id),
                               .start_pos = block.start_pos,
                               .end_pos = block.end_pos,
                               .record_count = block.record_count,
                               .payload = block.payload});
    }

    return format::write_axf_file(file, output_axf);
}

std::expected<void, std::string> convert_bam_to_axf1_mvp(const std::filesystem::path& input_bam,
                                                         const std::filesystem::path& output_axf,
                                                         const std::optional<std::string>& region) {
    std::optional<query::SamRegion> parsed_region;
    if (region.has_value()) {
        auto parsed = query::parse_sam_region(*region);
        if (!parsed) {
            return std::unexpected(parsed.error());
        }
        parsed_region = std::move(*parsed);
    }

    auto reader = io::BamReader::open(input_bam);
    if (!reader) {
        return std::unexpected(reader.error());
    }

    if (region.has_value()) {
        auto fetch = reader->fetch(*region);
        if (!fetch) {
            return std::unexpected(fetch.error());
        }
    }

    auto references = reader->references();
    if (!references) {
        return std::unexpected(references.error());
    }

    format::Axf1File file;
    file.references.reserve(references->size());
    for (const io::BamReference& reference : *references) {
        file.references.push_back({.name = reference.name, .length = reference.length});
    }

    std::vector<PendingAxf1Chunk> chunks(references->size());
    for (;;) {
        auto record_view = reader->next_record_view();
        if (!record_view) {
            return std::unexpected(record_view.error());
        }
        if (!record_view->has_value()) {
            break;
        }
        const io::BamRecordView& view = **record_view;
        if (view.record.is_unmapped() || view.record.position < 0 ||
            view.record.end_position <= view.record.position) {
            continue;
        }
        if (parsed_region.has_value() && !overlaps_region(view.record, *parsed_region)) {
            continue;
        }

        std::size_t ref_id = references->size();
        for (std::size_t index = 0; index < references->size(); ++index) {
            if (references->at(index).name == view.record.reference_name) {
                ref_id = index;
                break;
            }
        }
        if (ref_id >= references->size()) {
            return std::unexpected("failed to map BAM record reference name to reference id");
        }

        auto axf1_record = parse_axf1_record(view.sam_line);
        if (!axf1_record) {
            return std::unexpected(axf1_record.error());
        }
        PendingAxf1Chunk& chunk = chunks.at(ref_id);
        append_axf1_record(chunk, std::move(*axf1_record), view.record);
        if (chunk.records.size() >= kAxf1MvpMaxRecordsPerChunk) {
            flush_axf1_chunk(file, static_cast<std::uint32_t>(ref_id), chunk);
        }
    }

    for (std::size_t ref_id = 0; ref_id < chunks.size(); ++ref_id) {
        flush_axf1_chunk(file, static_cast<std::uint32_t>(ref_id), chunks[ref_id]);
    }

    return format::write_axf1_file(file, output_axf);
}

} // namespace alignx::convert
