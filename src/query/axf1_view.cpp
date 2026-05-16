#include "query/axf1_view.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <future>
#include <limits>
#include <ostream>
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

    static const std::vector<format::Axf1ColumnId> kAllViewColumns = {
        format::Axf1ColumnId::qname,          format::Axf1ColumnId::flag,
        format::Axf1ColumnId::pos,            format::Axf1ColumnId::mapq,
        format::Axf1ColumnId::cigar,          format::Axf1ColumnId::mate_reference,
        format::Axf1ColumnId::mate_pos,       format::Axf1ColumnId::template_length,
        format::Axf1ColumnId::sequence,       format::Axf1ColumnId::quality,
        format::Axf1ColumnId::tags,
    };

    std::vector<format::Axf1ColumnId> filter_columns = {format::Axf1ColumnId::pos,
                                                        format::Axf1ColumnId::cigar};
    if (filter.is_active()) {
        filter_columns.push_back(format::Axf1ColumnId::flag);
        filter_columns.push_back(format::Axf1ColumnId::mapq);
    }

    const auto& ref_name = reader->index().references.at(*ref_id).name;

    constexpr std::size_t kParallelThreshold = 16;
    const bool use_parallel =
        !filter.is_active() && hits->size() >= kParallelThreshold;

    if (use_parallel) {
        const std::size_t num_workers =
            std::min<std::size_t>(std::thread::hardware_concurrency(), 8);

        struct ChunkResult {
            std::string output;
            std::size_t records_scanned = 0;
            std::size_t records_matched = 0;
            std::uint64_t bytes_read = 0;
            std::uint64_t payload_bytes = 0;
        };

        const auto decode_start = Clock::now();
        const bool use_mmap = reader->mapped_data() != nullptr;

        // Pre-allocate result slots for all chunks
        std::vector<std::expected<ChunkResult, std::string>> results(hits->size());

        // Thread pool with work-stealing via atomic index
        std::atomic<std::size_t> next_chunk{0};
        const std::size_t total_chunks = hits->size();
        const auto& columns_ref = kAllViewColumns;

        auto worker_fn = [&]() {
            while (true) {
                const std::size_t idx = next_chunk.fetch_add(1, std::memory_order_relaxed);
                if (idx >= total_chunks) {
                    break;
                }
                const format::Axf1ChunkIndexEntry* chunk_entry = (*hits)[idx];

                std::expected<ChunkResult, std::string> result;
                if (use_mmap) {
                    const unsigned char* chunk_data =
                        reader->mapped_data() + chunk_entry->chunk_offset;
                    auto chunk = format::Axf1FileReader::decode_chunk_mapped(
                        chunk_data, chunk_entry->chunk_length, *chunk_entry, columns_ref);
                    if (!chunk) {
                        results[idx] = std::unexpected(chunk.error());
                        continue;
                    }
                    ChunkResult cr;
                    cr.bytes_read = chunk_entry->chunk_length;
                    cr.payload_bytes = chunk_entry->chunk_length;

                    const bool is_interior_chunk =
                        chunk_entry->start_pos >= parsed_region->start &&
                        chunk_entry->end_pos <= parsed_region->end;

                    if (is_interior_chunk) {
                        cr.records_scanned = chunk->records.size();
                        cr.records_matched = chunk->records.size();
                        for (std::size_t i = 0; i < chunk->records.size(); ++i) {
                            format::append_axf1_sam_record(cr.output, chunk->records[i], ref_name);
                        }
                    } else {
                        for (std::size_t i = 0; i < chunk->records.size(); ++i) {
                            cr.records_scanned += 1;
                            auto overlaps =
                                record_overlaps_region(chunk->records[i], *parsed_region);
                            if (!overlaps) {
                                results[idx] = std::unexpected(overlaps.error());
                                return;
                            }
                            if (!*overlaps) {
                                continue;
                            }
                            cr.records_matched += 1;
                            format::append_axf1_sam_record(cr.output, chunk->records[i], ref_name);
                        }
                    }
                    results[idx] = std::move(cr);
                } else {
                    // Fallback: read_chunk_raw requires ifstream (not thread-safe)
                    // This path shouldn't be reached in parallel mode on non-mmap systems
                    // but kept for correctness on Windows
                    results[idx] = std::unexpected(
                        std::string("parallel decode requires mmap"));
                }
            }
        };

        if (use_mmap) {
            std::vector<std::thread> workers;
            workers.reserve(num_workers);
            for (std::size_t i = 0; i < num_workers; ++i) {
                workers.emplace_back(worker_fn);
            }
            for (auto& w : workers) {
                w.join();
            }
        } else {
            // Non-mmap fallback: sliding-window async with sequential main-thread reads
            std::deque<std::pair<std::size_t, std::future<std::expected<ChunkResult, std::string>>>>
                in_flight;

            for (std::size_t idx = 0; idx < total_chunks; ++idx) {
                const format::Axf1ChunkIndexEntry* chunk_entry = (*hits)[idx];
                auto chunk_bytes = reader->read_chunk_raw(*chunk_entry);
                if (!chunk_bytes) {
                    results[idx] = std::unexpected(chunk_bytes.error());
                    break;
                }

                auto fut = std::async(
                    std::launch::async,
                    [bytes = std::move(*chunk_bytes), entry = *chunk_entry, &columns_ref,
                     &parsed_region, &ref_name]() -> std::expected<ChunkResult, std::string> {
                        auto chunk = format::Axf1FileReader::decode_chunk_raw(
                            bytes, entry, columns_ref);
                        if (!chunk) {
                            return std::unexpected(chunk.error());
                        }
                        ChunkResult cr;
                        cr.bytes_read = bytes.size();
                        cr.payload_bytes = bytes.size();

                        const bool is_interior =
                            entry.start_pos >= parsed_region->start &&
                            entry.end_pos <= parsed_region->end;

                        if (is_interior) {
                            cr.records_scanned = chunk->records.size();
                            cr.records_matched = chunk->records.size();
                            for (std::size_t i = 0; i < chunk->records.size(); ++i) {
                                format::append_axf1_sam_record(
                                    cr.output, chunk->records[i], ref_name);
                            }
                        } else {
                            for (std::size_t i = 0; i < chunk->records.size(); ++i) {
                                cr.records_scanned += 1;
                                auto overlaps =
                                    record_overlaps_region(chunk->records[i], *parsed_region);
                                if (!overlaps) {
                                    return std::unexpected(overlaps.error());
                                }
                                if (!*overlaps) {
                                    continue;
                                }
                                cr.records_matched += 1;
                                format::append_axf1_sam_record(
                                    cr.output, chunk->records[i], ref_name);
                            }
                        }
                        return cr;
                    });

                in_flight.emplace_back(idx, std::move(fut));

                while (in_flight.size() >= num_workers) {
                    auto& [result_idx, front_fut] = in_flight.front();
                    results[result_idx] = front_fut.get();
                    in_flight.pop_front();
                }
            }
            while (!in_flight.empty()) {
                auto& [result_idx, front_fut] = in_flight.front();
                results[result_idx] = front_fut.get();
                in_flight.pop_front();
            }
        }

        // Drain results in order
        for (std::size_t idx = 0; idx < total_chunks; ++idx) {
            auto& result = results[idx];
            if (!result) {
                return std::unexpected(result.error());
            }
            profile.records_scanned += result->records_scanned;
            profile.records_matched += result->records_matched;
            profile.records_output += result->records_matched;
            profile.output_bytes_read += result->bytes_read;
            profile.output_payload_bytes += result->payload_bytes;
            if (!result->output.empty()) {
                profile.chunks_with_matches += 1;
                const auto write_start = Clock::now();
                out.write(result->output.data(),
                          static_cast<std::streamsize>(result->output.size()));
                profile.write_time += Clock::now() - write_start;
                profile.stdout_bytes += result->output.size();
            }
        }
        profile.output_decode_time += Clock::now() - decode_start;

        return {};
    }

    // Sequential path (small chunk count or filter active)
    std::string output;
    for (const format::Axf1ChunkIndexEntry* chunk_entry : *hits) {
        if (!filter.is_active()) {
            format::Axf1ChunkReadProfile chunk_profile;
            const auto decode_start = Clock::now();
            auto chunk =
                reader->read_chunk_columns_selective(*chunk_entry, kAllViewColumns, chunk_profile);
            profile.output_decode_time += Clock::now() - decode_start;
            if (!chunk) {
                return std::unexpected(chunk.error());
            }
            profile.output_bytes_read += chunk_profile.bytes_read;
            profile.output_payload_bytes += chunk_profile.selected_payload_bytes;

            const auto filter_start = Clock::now();
            std::vector<std::size_t> matching_records;
            for (std::size_t record_index = 0; record_index < chunk->records.size();
                 ++record_index) {
                profile.records_scanned += 1;
                const auto& rec = chunk->records[record_index];
                auto overlaps = record_overlaps_region(rec, *parsed_region);
                if (!overlaps) {
                    return std::unexpected(overlaps.error());
                }
                if (!*overlaps) {
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

            const auto format_start = Clock::now();
            for (std::size_t record_index : matching_records) {
                format::append_axf1_sam_record(output, chunk->records[record_index], ref_name);
                profile.records_output += 1;
            }
            profile.format_time += Clock::now() - format_start;
        } else {
            format::Axf1ChunkReadProfile selective_profile;
            const auto selective_decode_start = Clock::now();
            auto filter_chunk = reader->read_chunk_columns_selective(*chunk_entry, filter_columns,
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
                const auto& rec = filter_chunk->records[record_index];
                auto overlaps = record_overlaps_region(rec, *parsed_region);
                if (!overlaps) {
                    return std::unexpected(overlaps.error());
                }
                if (!*overlaps) {
                    continue;
                }
                if (!passes_filter(filter, rec.flag, rec.mapq)) {
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
                {format::Axf1ColumnId::qname, format::Axf1ColumnId::flag,
                 format::Axf1ColumnId::mapq, format::Axf1ColumnId::mate_reference,
                 format::Axf1ColumnId::mate_pos, format::Axf1ColumnId::template_length,
                 format::Axf1ColumnId::sequence, format::Axf1ColumnId::quality,
                 format::Axf1ColumnId::tags},
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
                format::append_axf1_sam_record(output, filter_chunk->records[record_index],
                                               ref_name);
                profile.records_output += 1;
            }
            profile.format_time += Clock::now() - format_start;
        }

        const auto write_start = Clock::now();
        out.write(output.data(), static_cast<std::streamsize>(output.size()));
        profile.write_time += Clock::now() - write_start;
        profile.stdout_bytes += output.size();
        output.clear();
    }

    return {};
}

} // namespace alignx::query
