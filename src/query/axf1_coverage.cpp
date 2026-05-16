#include "query/axf1_coverage.hpp"

#include <chrono>
#include <cstdint>
#include <limits>
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

} // namespace

std::expected<analysis::CoverageResult, std::string>
compute_axf1_coverage(const std::filesystem::path& input, const std::string& region) {
    Axf1CoverageProfile unused;
    return compute_axf1_coverage_profiled(input, region, unused);
}

std::expected<analysis::CoverageResult, std::string>
compute_axf1_coverage_profiled(const std::filesystem::path& input, const std::string& region,
                               Axf1CoverageProfile& profile) {
    auto parsed_region = parse_sam_region(region);
    if (!parsed_region) {
        return std::unexpected(parsed_region.error());
    }

    analysis::CoverageResult result;
    result.reference = parsed_region->reference;
    result.start = parsed_region->start;
    result.end = parsed_region->end;
    result.depth.resize(static_cast<std::size_t>(parsed_region->end - parsed_region->start), 0);

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

    for (const format::Axf1ChunkIndexEntry* chunk_entry : *hits) {
        format::Axf1ChunkReadProfile selective_profile;
        const auto selective_decode_start = Clock::now();
        auto chunk = reader->read_chunk_columns_profiled(
            *chunk_entry, {format::Axf1ColumnId::pos, format::Axf1ColumnId::cigar},
            selective_profile);
        profile.selective_decode_time += Clock::now() - selective_decode_start;
        if (!chunk) {
            return std::unexpected(chunk.error());
        }
        profile.selective_bytes_read += selective_profile.bytes_read;
        profile.selective_payload_bytes += selective_profile.selected_payload_bytes;

        const auto filter_start = Clock::now();
        std::vector<std::size_t> matching_indices;
        for (std::size_t i = 0; i < chunk->records.size(); ++i) {
            profile.records_scanned += 1;
            auto overlaps = record_overlaps_region(chunk->records[i], *parsed_region);
            if (!overlaps) {
                return std::unexpected(overlaps.error());
            }
            if (*overlaps) {
                matching_indices.push_back(i);
                profile.records_matched += 1;
            }
        }
        profile.filter_time += Clock::now() - filter_start;
        if (matching_indices.empty()) {
            continue;
        }
        profile.chunks_with_matches += 1;

        const auto coverage_start = Clock::now();
        for (std::size_t idx : matching_indices) {
            auto add = analysis::add_coverage(result, chunk->records[idx].pos,
                                              chunk->records[idx].cigar);
            if (!add) {
                return std::unexpected(add.error());
            }
            result.records_counted += 1;
        }
        profile.coverage_time += Clock::now() - coverage_start;
    }

    return result;
}

} // namespace alignx::query
