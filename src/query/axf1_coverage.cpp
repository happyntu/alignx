#include "query/axf1_coverage.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <thread>
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

std::expected<void, std::string>
add_coverage_to_depth(std::vector<std::uint32_t>& depth, std::int32_t region_start,
                      std::int32_t region_end, std::int32_t record_pos,
                      std::string_view cigar) {
    auto span = cigar_reference_span(cigar);
    if (!span) return std::unexpected(span.error());

    std::int32_t ref_pos = record_pos;
    std::size_t ci = 0;
    while (ci < cigar.size()) {
        std::uint32_t op_len = 0;
        while (ci < cigar.size() && cigar[ci] >= '0' && cigar[ci] <= '9') {
            op_len = op_len * 10 + static_cast<std::uint32_t>(cigar[ci] - '0');
            ++ci;
        }
        if (ci >= cigar.size()) break;
        const char op = cigar[ci++];

        switch (op) {
        case 'M': case '=': case 'X': {
            std::int32_t start = std::max(ref_pos, region_start);
            std::int32_t end = std::min(ref_pos + static_cast<std::int32_t>(op_len), region_end);
            if (start < end) {
                for (std::int32_t p = start; p < end; ++p) {
                    depth[static_cast<std::size_t>(p - region_start)] += 1;
                }
            }
            ref_pos += static_cast<std::int32_t>(op_len);
            break;
        }
        case 'D': case 'N':
            ref_pos += static_cast<std::int32_t>(op_len);
            break;
        case 'I': case 'S': case 'H': case 'P':
            break;
        default:
            return std::unexpected("unknown CIGAR op in coverage");
        }
    }
    return {};
}

} // namespace

std::expected<analysis::CoverageResult, std::string>
compute_axf1_coverage(const std::filesystem::path& input, const std::string& region,
                      const RecordFilter& filter) {
    Axf1CoverageProfile unused;
    return compute_axf1_coverage_profiled(input, region, unused, filter);
}

std::expected<analysis::CoverageResult, std::string>
compute_axf1_coverage_profiled(const std::filesystem::path& input, const std::string& region,
                               Axf1CoverageProfile& profile, const RecordFilter& filter) {
    auto parsed_region = parse_sam_region(region);
    if (!parsed_region) {
        return std::unexpected(parsed_region.error());
    }

    analysis::CoverageResult result;
    result.reference = parsed_region->reference;
    result.start = parsed_region->start;
    result.end = parsed_region->end;
    const auto depth_size = static_cast<std::size_t>(parsed_region->end - parsed_region->start);
    result.depth.resize(depth_size, 0);

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

    constexpr std::size_t kParallelChunkThreshold = 500;
    const bool use_mmap = reader->mapped_data() != nullptr;
    const bool use_parallel = use_mmap && hits->size() >= kParallelChunkThreshold;

    if (use_parallel) {
        const std::size_t num_workers =
            std::min<std::size_t>(std::thread::hardware_concurrency(), 8);

        struct WorkerResult {
            std::vector<std::uint32_t> local_depth;
            std::uint64_t records_scanned = 0;
            std::uint64_t records_matched = 0;
            std::uint64_t records_filtered = 0;
            std::uint64_t bytes_read = 0;
            std::string error;
        };

        std::vector<WorkerResult> worker_results(num_workers);
        for (auto& wr : worker_results) {
            wr.local_depth.resize(depth_size, 0);
        }

        std::atomic<std::size_t> next_chunk{0};
        const std::size_t total_chunks = hits->size();
        const bool filter_active = filter.is_active();
        const auto region_start = parsed_region->start;
        const auto region_end = parsed_region->end;

        const auto decode_start = Clock::now();

        auto worker_fn = [&](std::size_t my_id) {
            auto& wr = worker_results[my_id];
            while (true) {
                const std::size_t idx = next_chunk.fetch_add(1, std::memory_order_relaxed);
                if (idx >= total_chunks) break;

                const format::Axf1ChunkIndexEntry* chunk_entry = (*hits)[idx];
                const unsigned char* chunk_data =
                    reader->mapped_data() + chunk_entry->chunk_offset;

                auto chunk = format::Axf1FileReader::decode_chunk_mapped(
                    chunk_data, chunk_entry->chunk_length, *chunk_entry, filter_columns);
                if (!chunk) {
                    wr.error = chunk.error();
                    return;
                }
                wr.bytes_read += chunk_entry->chunk_length;

                const bool is_interior =
                    chunk_entry->start_pos >= region_start &&
                    chunk_entry->end_pos <= region_end;

                for (std::size_t i = 0; i < chunk->records.size(); ++i) {
                    wr.records_scanned += 1;
                    const auto& rec = chunk->records[i];

                    if (!is_interior) {
                        auto overlaps = record_overlaps_region(rec, *parsed_region);
                        if (!overlaps) {
                            wr.error = overlaps.error();
                            return;
                        }
                        if (!*overlaps) continue;
                    }
                    if (filter_active) {
                        if ((rec.flag & filter.flag_exclude) != 0 ||
                            rec.mapq < filter.min_mapq) {
                            wr.records_filtered += 1;
                            continue;
                        }
                    }
                    wr.records_matched += 1;
                    auto cov = add_coverage_to_depth(
                        wr.local_depth, region_start, region_end, rec.pos, rec.cigar);
                    if (!cov) {
                        wr.error = cov.error();
                        return;
                    }
                }
            }
        };

        std::vector<std::thread> workers;
        workers.reserve(num_workers);
        for (std::size_t i = 0; i < num_workers; ++i) {
            workers.emplace_back(worker_fn, i);
        }
        for (auto& w : workers) {
            w.join();
        }

        profile.selective_decode_time += Clock::now() - decode_start;

        // Merge worker results
        for (auto& wr : worker_results) {
            if (!wr.error.empty()) {
                return std::unexpected(wr.error);
            }
            profile.records_scanned += wr.records_scanned;
            profile.records_matched += wr.records_matched;
            profile.records_filtered += wr.records_filtered;
            profile.selective_bytes_read += wr.bytes_read;
            result.records_counted += wr.records_matched;
            for (std::size_t i = 0; i < depth_size; ++i) {
                result.depth[i] += wr.local_depth[i];
            }
        }
        profile.chunks_with_matches = hits->size();

    } else {
        // Sequential path
        for (const format::Axf1ChunkIndexEntry* chunk_entry : *hits) {
            format::Axf1ChunkReadProfile selective_profile;
            const auto selective_decode_start = Clock::now();
            auto chunk =
                reader->read_chunk_columns_selective(*chunk_entry, filter_columns, selective_profile);
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
                const auto& rec = chunk->records[i];
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
                matching_indices.push_back(i);
                profile.records_matched += 1;
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
    }

    return result;
}

} // namespace alignx::query
