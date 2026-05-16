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

void merge_output_columns(format::Axf1Chunk& target, format::Axf1Chunk&& source) {
    for (std::size_t index = 0; index < target.records.size(); ++index) {
        auto& target_record = target.records.at(index);
        auto& source_record = source.records.at(index);
        target_record.qname = std::move(source_record.qname);
        target_record.flag = source_record.flag;
        target_record.mapq = source_record.mapq;
        target_record.mate_reference = std::move(source_record.mate_reference);
        target_record.mate_pos = source_record.mate_pos;
        target_record.template_length = source_record.template_length;
        target_record.sequence = std::move(source_record.sequence);
        target_record.quality = std::move(source_record.quality);
        target_record.tags = std::move(source_record.tags);
    }
}

} // namespace

std::expected<void, std::string> write_axf1_region_sam(const std::filesystem::path& input,
                                                       const std::string& region,
                                                       std::ostream& out,
                                                       const RecordFilter& filter) {
    Axf1ViewProfile unused_profile;
    return write_axf1_region_sam_profiled(input, region, out, unused_profile, filter);
}

std::expected<void, std::string>
write_axf1_region_sam_profiled(const std::filesystem::path& input, const std::string& region,
                               std::ostream& out, Axf1ViewProfile& profile,
                               const RecordFilter& filter) {
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

    std::vector<format::Axf1ColumnId> filter_columns = {format::Axf1ColumnId::pos,
                                                        format::Axf1ColumnId::cigar};
    if (filter.is_active()) {
        filter_columns.push_back(format::Axf1ColumnId::flag);
        filter_columns.push_back(format::Axf1ColumnId::mapq);
    }

    std::string output;
    for (const format::Axf1ChunkIndexEntry* chunk_entry : *hits) {
        format::Axf1ChunkReadProfile selective_profile;
        const auto selective_decode_start = Clock::now();
        auto filter_chunk =
            reader->read_chunk_columns_selective(*chunk_entry, filter_columns, selective_profile);
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
            const auto& rec = filter_chunk->records.at(record_index);
            auto overlaps = record_overlaps_region(rec, *parsed_region);
            if (!overlaps) {
                return std::unexpected(overlaps.error());
            }
            if (!*overlaps) {
                continue;
            }
            if (filter.is_active() && !passes_filter(filter, rec.flag, rec.mapq)) {
                profile.records_filtered += 1;
                continue;
            }
            matching_records.push_back(record_index);
            profile.records_matched += 1;
        }
        profile.filter_time += Clock::now() - filter_start;
        if (matching_records.empty()) {
            continue;
        }
        profile.chunks_with_matches += 1;

        format::Axf1ChunkReadProfile output_profile;
        const auto output_decode_start = Clock::now();
        auto output_chunk = reader->read_chunk_columns_selective(
            *chunk_entry,
            {format::Axf1ColumnId::qname, format::Axf1ColumnId::flag, format::Axf1ColumnId::mapq,
             format::Axf1ColumnId::mate_reference, format::Axf1ColumnId::mate_pos,
             format::Axf1ColumnId::template_length, format::Axf1ColumnId::sequence,
             format::Axf1ColumnId::quality, format::Axf1ColumnId::tags},
            output_profile);
        profile.output_decode_time += Clock::now() - output_decode_start;
        if (!output_chunk) {
            return std::unexpected(output_chunk.error());
        }
        profile.output_bytes_read += output_profile.bytes_read;
        profile.output_payload_bytes += output_profile.selected_payload_bytes;
        merge_output_columns(*filter_chunk, std::move(*output_chunk));

        const auto format_start = Clock::now();
        for (std::size_t record_index : matching_records) {
            output.append(format::format_axf1_sam_record(filter_chunk->records.at(record_index),
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
