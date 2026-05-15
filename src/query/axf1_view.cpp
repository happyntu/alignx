#include "query/axf1_view.hpp"

#include <chrono>
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

using Clock = std::chrono::steady_clock;

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
    Axf1ViewProfile unused_profile;
    return write_axf1_region_sam_profiled(input, region, out, unused_profile);
}

std::expected<void, std::string>
write_axf1_region_sam_profiled(const std::filesystem::path& input, const std::string& region,
                               std::ostream& out, Axf1ViewProfile& profile) {
    auto parsed_region = parse_sam_region(region);
    if (!parsed_region) {
        return std::unexpected(parsed_region.error());
    }

    const auto open_start = Clock::now();
    auto reader = format::Axf1FileReader::open(input);
    profile.open_time += Clock::now() - open_start;
    if (!reader) {
        return std::unexpected(reader.error());
    }

    const auto ref_lookup_start = Clock::now();
    auto ref_id = find_reference_id(reader->index().references, parsed_region->reference);
    profile.reference_lookup_time += Clock::now() - ref_lookup_start;
    if (!ref_id) {
        return std::unexpected(ref_id.error());
    }

    const auto chunk_query_start = Clock::now();
    auto hits = reader->query_chunks(*ref_id, parsed_region->start, parsed_region->end);
    profile.chunk_query_time += Clock::now() - chunk_query_start;
    if (!hits) {
        return std::unexpected(hits.error());
    }
    profile.chunks_selected += hits->size();

    std::string output;
    for (const format::Axf1ChunkIndexEntry* chunk_entry : *hits) {
        format::Axf1ChunkReadProfile selective_profile;
        const auto selective_decode_start = Clock::now();
        auto filter_chunk = reader->read_chunk_columns_profiled(
            *chunk_entry, {format::Axf1ColumnId::pos, format::Axf1ColumnId::cigar},
            selective_profile);
        profile.selective_decode_time += Clock::now() - selective_decode_start;
        if (!filter_chunk) {
            return std::unexpected(filter_chunk.error());
        }
        profile.selective_bytes_read += selective_profile.bytes_read;
        profile.selective_payload_bytes += selective_profile.selected_payload_bytes;

        std::vector<std::size_t> matching_records;
        const auto filter_start = Clock::now();
        for (std::size_t record_index = 0; record_index < filter_chunk->records.size();
             ++record_index) {
            profile.records_scanned += 1;
            auto overlaps =
                record_overlaps_region(filter_chunk->records.at(record_index), *parsed_region);
            if (!overlaps) {
                return std::unexpected(overlaps.error());
            }
            if (*overlaps) {
                matching_records.push_back(record_index);
                profile.records_matched += 1;
            }
        }
        profile.filter_time += Clock::now() - filter_start;
        if (matching_records.empty()) {
            continue;
        }
        profile.chunks_with_matches += 1;

        format::Axf1ChunkReadProfile full_profile;
        const auto full_decode_start = Clock::now();
        auto output_chunk = reader->read_chunk_profiled(*chunk_entry, full_profile);
        profile.full_decode_time += Clock::now() - full_decode_start;
        if (!output_chunk) {
            return std::unexpected(output_chunk.error());
        }
        profile.full_chunk_bytes_read += full_profile.bytes_read;
        profile.full_payload_bytes += full_profile.selected_payload_bytes;

        const auto format_start = Clock::now();
        for (std::size_t record_index : matching_records) {
            output.append(format_sam_record(output_chunk->records.at(record_index),
                                            reader->index().references.at(*ref_id).name));
            profile.records_output += 1;
        }
        profile.format_time += Clock::now() - format_start;
    }

    const auto write_start = Clock::now();
    out.write(output.data(), static_cast<std::streamsize>(output.size()));
    profile.write_time += Clock::now() - write_start;
    profile.stdout_bytes += output.size();
    return {};
}

} // namespace alignx::query
