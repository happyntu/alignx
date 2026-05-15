#include "convert/axf1_chunk_policy.hpp"

#include <algorithm>
#include <string>

namespace alignx::convert {
namespace {

constexpr std::size_t kFixedRawColumnBytes = sizeof(std::uint16_t) + sizeof(std::int32_t) +
                                             sizeof(std::uint8_t) + sizeof(std::int32_t) +
                                             sizeof(std::int32_t);

std::size_t string_column_bytes(const std::string& value) noexcept {
    return sizeof(std::uint32_t) + value.size();
}

} // namespace

std::size_t estimate_axf1_record_uncompressed_bytes(const format::Axf1Record& record) noexcept {
    return kFixedRawColumnBytes + string_column_bytes(record.qname) +
           string_column_bytes(record.cigar) + string_column_bytes(record.mate_reference) +
           string_column_bytes(record.sequence) + string_column_bytes(record.quality) +
           string_column_bytes(record.tags);
}

std::expected<Axf1ChunkPolicy, std::string> apply_axf1_chunk_policy_override(
    const Axf1ChunkPolicy& base, const Axf1ChunkPolicyOverride& policy_override) noexcept {
    Axf1ChunkPolicy policy = base;
    if (policy_override.target_uncompressed_bytes.has_value()) {
        policy.target_uncompressed_bytes = *policy_override.target_uncompressed_bytes;
    }
    if (policy_override.max_uncompressed_bytes.has_value()) {
        policy.max_uncompressed_bytes = *policy_override.max_uncompressed_bytes;
    }
    if (policy_override.max_records.has_value()) {
        policy.max_records = *policy_override.max_records;
    }
    if (policy_override.max_genomic_span.has_value()) {
        policy.max_genomic_span = *policy_override.max_genomic_span;
    }

    if (policy.target_uncompressed_bytes == 0) {
        return std::unexpected("AXF1 chunk policy target_uncompressed_bytes must be positive");
    }
    if (policy.max_uncompressed_bytes == 0) {
        return std::unexpected("AXF1 chunk policy max_uncompressed_bytes must be positive");
    }
    if (policy.max_records == 0) {
        return std::unexpected("AXF1 chunk policy max_records must be positive");
    }
    if (policy.max_genomic_span <= 0) {
        return std::unexpected("AXF1 chunk policy max_genomic_span must be positive");
    }
    if (policy.target_uncompressed_bytes > policy.max_uncompressed_bytes) {
        return std::unexpected(
            "AXF1 chunk policy target_uncompressed_bytes must not exceed max_uncompressed_bytes");
    }

    return policy;
}

bool should_flush_axf1_chunk_before_append(const Axf1ChunkPolicy& policy,
                                           const Axf1ChunkState& chunk,
                                           const Axf1RecordChunkMetrics& next_record) noexcept {
    if (!chunk.has_records) {
        return false;
    }

    const std::int32_t merged_start = std::min(chunk.start_pos, next_record.start_pos);
    const std::int32_t merged_end = std::max(chunk.end_pos, next_record.end_pos);
    const std::int64_t merged_span = static_cast<std::int64_t>(merged_end) - merged_start;

    return chunk.record_count + 1 > policy.max_records ||
           chunk.estimated_uncompressed_bytes + next_record.estimated_uncompressed_bytes >
               policy.max_uncompressed_bytes ||
           merged_span > policy.max_genomic_span;
}

bool should_flush_axf1_chunk_on_reference_change(const std::optional<std::uint32_t>& pending_ref_id,
                                                 std::uint32_t next_ref_id) noexcept {
    return pending_ref_id.has_value() && *pending_ref_id != next_ref_id;
}

bool should_flush_axf1_chunk_after_append(const Axf1ChunkPolicy& policy,
                                          const Axf1ChunkState& chunk) noexcept {
    return chunk.has_records &&
           chunk.estimated_uncompressed_bytes >= policy.target_uncompressed_bytes;
}

void append_axf1_chunk_metrics(Axf1ChunkState& chunk,
                               const Axf1RecordChunkMetrics& record) noexcept {
    if (!chunk.has_records) {
        chunk.has_records = true;
        chunk.start_pos = record.start_pos;
        chunk.end_pos = record.end_pos;
    } else {
        chunk.start_pos = std::min(chunk.start_pos, record.start_pos);
        chunk.end_pos = std::max(chunk.end_pos, record.end_pos);
    }

    chunk.record_count += 1;
    chunk.estimated_uncompressed_bytes += record.estimated_uncompressed_bytes;
}

} // namespace alignx::convert
