#include "query/axf1_view.hpp"

#include <cstdint>
#include <limits>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

#include "format/axf1_file.hpp"
#include "query/region.hpp"
#include "query/sam_utils.hpp"

namespace alignx::query {
namespace {

std::expected<std::uint32_t, std::string>
find_reference_id(const std::vector<format::Axf1Reference>& references,
                  std::string_view reference) {
    for (std::size_t index = 0; index < references.size(); ++index) {
        if (references[index].name == reference) {
            return static_cast<std::uint32_t>(index);
        }
    }
    return std::unexpected("reference not found in AXF1: " + std::string(reference));
}

std::expected<bool, std::string> record_overlaps_region(const format::Axf1Record& record,
                                                        const SamRegion& region) {
    auto span = cigar_reference_span(record.cigar);
    if (!span) {
        return std::unexpected(span.error());
    }
    if (record.pos > std::numeric_limits<std::int32_t>::max() - *span) {
        return std::unexpected("AXF1 record reference span is too large");
    }
    return half_open_intervals_overlap(record.pos, record.pos + *span, region.start, region.end);
}

std::string format_sam_record(const format::Axf1Record& record, const std::string& reference) {
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

} // namespace

std::expected<void, std::string> write_axf1_region_sam(const std::filesystem::path& input,
                                                       const std::string& region,
                                                       std::ostream& out) {
    auto parsed_region = parse_sam_region(region);
    if (!parsed_region) {
        return std::unexpected(parsed_region.error());
    }

    auto file = format::read_axf1_file(input);
    if (!file) {
        return std::unexpected(file.error());
    }

    auto ref_id = find_reference_id(file->references, parsed_region->reference);
    if (!ref_id) {
        return std::unexpected(ref_id.error());
    }

    std::string output;
    for (const format::Axf1Chunk& chunk : file->chunks) {
        if (chunk.ref_id != *ref_id ||
            !half_open_intervals_overlap(chunk.start_pos, chunk.end_pos, parsed_region->start,
                                         parsed_region->end)) {
            continue;
        }

        for (const format::Axf1Record& record : chunk.records) {
            auto overlaps = record_overlaps_region(record, *parsed_region);
            if (!overlaps) {
                return std::unexpected(overlaps.error());
            }
            if (*overlaps) {
                output.append(format_sam_record(record, file->references.at(*ref_id).name));
            }
        }
    }

    out.write(output.data(), static_cast<std::streamsize>(output.size()));
    return {};
}

} // namespace alignx::query
