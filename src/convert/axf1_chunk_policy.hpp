#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

#include "format/axf1_file.hpp"

namespace alignx::convert {

struct Axf1ChunkPolicy {
    std::size_t target_uncompressed_bytes = 256 * 1024;
    std::size_t max_uncompressed_bytes = 512 * 1024;
    std::size_t max_records = 4096;
    std::int32_t max_genomic_span = 1'000'000;
};

struct Axf1ChunkPolicyOverride {
    std::optional<std::size_t> target_uncompressed_bytes;
    std::optional<std::size_t> max_uncompressed_bytes;
    std::optional<std::size_t> max_records;
    std::optional<std::int32_t> max_genomic_span;
};

struct Axf1RecordChunkMetrics {
    std::int32_t start_pos = -1;
    std::int32_t end_pos = -1;
    std::size_t estimated_uncompressed_bytes = 0;
};

struct Axf1ChunkState {
    bool has_records = false;
    std::int32_t start_pos = -1;
    std::int32_t end_pos = -1;
    std::size_t record_count = 0;
    std::size_t estimated_uncompressed_bytes = 0;
};

[[nodiscard]] std::size_t
estimate_axf1_record_uncompressed_bytes(const format::Axf1Record& record) noexcept;

[[nodiscard]] std::expected<Axf1ChunkPolicy, std::string>
apply_axf1_chunk_policy_override(const Axf1ChunkPolicy& base,
                                 const Axf1ChunkPolicyOverride& policy_override) noexcept;

[[nodiscard]] bool
should_flush_axf1_chunk_before_append(const Axf1ChunkPolicy& policy, const Axf1ChunkState& chunk,
                                      const Axf1RecordChunkMetrics& next_record) noexcept;

[[nodiscard]] bool
should_flush_axf1_chunk_on_reference_change(const std::optional<std::uint32_t>& pending_ref_id,
                                            std::uint32_t next_ref_id) noexcept;

[[nodiscard]] bool should_flush_axf1_chunk_after_append(const Axf1ChunkPolicy& policy,
                                                        const Axf1ChunkState& chunk) noexcept;

void append_axf1_chunk_metrics(Axf1ChunkState& chunk,
                               const Axf1RecordChunkMetrics& record) noexcept;

} // namespace alignx::convert
