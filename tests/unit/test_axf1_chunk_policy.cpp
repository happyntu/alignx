#include <gtest/gtest.h>

#include <optional>
#include <string>

#include "convert/axf1_chunk_policy.hpp"

namespace {

alignx::format::Axf1Record make_record(std::string qname = "read001", std::string sequence = "ACGT",
                                       std::string tags = "NM:i:0") {
    return alignx::format::Axf1Record{.qname = std::move(qname),
                                      .flag = 0,
                                      .pos = 100,
                                      .mapq = 60,
                                      .cigar = "4M",
                                      .mate_reference = "*",
                                      .mate_pos = -1,
                                      .template_length = 0,
                                      .sequence = std::move(sequence),
                                      .quality = "FFFF",
                                      .tags = std::move(tags)};
}

alignx::convert::Axf1RecordChunkMetrics metrics(std::int32_t start, std::int32_t end,
                                                std::size_t bytes = 10) {
    return {.start_pos = start, .end_pos = end, .estimated_uncompressed_bytes = bytes};
}

} // namespace

TEST(Axf1ChunkPolicy, EstimatesRawRecordBytesFromColumns) {
    const auto small = alignx::convert::estimate_axf1_record_uncompressed_bytes(make_record());
    const auto large = alignx::convert::estimate_axf1_record_uncompressed_bytes(
        make_record("read-with-longer-name", "ACGTACGTACGT", "NM:i:0\tMD:Z:12"));

    EXPECT_GT(small, 0);
    EXPECT_GT(large, small);
}

TEST(Axf1ChunkPolicy, FlushesBeforeAppendWhenRecordLimitWouldBeExceeded) {
    const alignx::convert::Axf1ChunkPolicy policy{.target_uncompressed_bytes = 1000,
                                                  .max_uncompressed_bytes = 1000,
                                                  .max_records = 2,
                                                  .max_genomic_span = 1000};
    alignx::convert::Axf1ChunkState chunk;
    alignx::convert::append_axf1_chunk_metrics(chunk, metrics(100, 110));
    alignx::convert::append_axf1_chunk_metrics(chunk, metrics(120, 130));

    EXPECT_TRUE(
        alignx::convert::should_flush_axf1_chunk_before_append(policy, chunk, metrics(140, 150)));
}

TEST(Axf1ChunkPolicy, FlushesBeforeAppendWhenByteLimitWouldBeExceeded) {
    const alignx::convert::Axf1ChunkPolicy policy{.target_uncompressed_bytes = 1000,
                                                  .max_uncompressed_bytes = 25,
                                                  .max_records = 100,
                                                  .max_genomic_span = 1000};
    alignx::convert::Axf1ChunkState chunk;
    alignx::convert::append_axf1_chunk_metrics(chunk, metrics(100, 110, 20));

    EXPECT_TRUE(alignx::convert::should_flush_axf1_chunk_before_append(policy, chunk,
                                                                       metrics(120, 130, 6)));
}

TEST(Axf1ChunkPolicy, FlushesBeforeAppendWhenGenomicSpanWouldBeExceeded) {
    const alignx::convert::Axf1ChunkPolicy policy{.target_uncompressed_bytes = 1000,
                                                  .max_uncompressed_bytes = 1000,
                                                  .max_records = 100,
                                                  .max_genomic_span = 50};
    alignx::convert::Axf1ChunkState chunk;
    alignx::convert::append_axf1_chunk_metrics(chunk, metrics(100, 110));

    EXPECT_TRUE(
        alignx::convert::should_flush_axf1_chunk_before_append(policy, chunk, metrics(160, 170)));
}

TEST(Axf1ChunkPolicy, DoesNotFlushEmptyChunkBeforeOversizedSingleRecord) {
    const alignx::convert::Axf1ChunkPolicy policy{.target_uncompressed_bytes = 100,
                                                  .max_uncompressed_bytes = 100,
                                                  .max_records = 1,
                                                  .max_genomic_span = 100};
    const alignx::convert::Axf1ChunkState chunk;

    EXPECT_FALSE(alignx::convert::should_flush_axf1_chunk_before_append(policy, chunk,
                                                                        metrics(100, 110, 200)));
}

TEST(Axf1ChunkPolicy, FlushesOnReferenceChange) {
    EXPECT_FALSE(alignx::convert::should_flush_axf1_chunk_on_reference_change(std::nullopt, 0));
    EXPECT_FALSE(alignx::convert::should_flush_axf1_chunk_on_reference_change(0, 0));
    EXPECT_TRUE(alignx::convert::should_flush_axf1_chunk_on_reference_change(0, 1));
}

TEST(Axf1ChunkPolicy, FlushesAfterAppendAtTargetByteBudget) {
    const alignx::convert::Axf1ChunkPolicy policy{.target_uncompressed_bytes = 100,
                                                  .max_uncompressed_bytes = 200,
                                                  .max_records = 100,
                                                  .max_genomic_span = 1000};
    alignx::convert::Axf1ChunkState chunk;
    alignx::convert::append_axf1_chunk_metrics(chunk, metrics(100, 110, 100));

    EXPECT_TRUE(alignx::convert::should_flush_axf1_chunk_after_append(policy, chunk));
}

TEST(Axf1ChunkPolicy, AppliesOverrideToBasePolicy) {
    const alignx::convert::Axf1ChunkPolicy base{};
    const alignx::convert::Axf1ChunkPolicyOverride policy_override{
        .target_uncompressed_bytes = 128 * 1024,
        .max_uncompressed_bytes = 256 * 1024,
        .max_records = 2048,
        .max_genomic_span = 500'000};

    const auto merged = alignx::convert::apply_axf1_chunk_policy_override(base, policy_override);
    ASSERT_TRUE(merged.has_value());
    EXPECT_EQ(merged->target_uncompressed_bytes, 128 * 1024);
    EXPECT_EQ(merged->max_uncompressed_bytes, 256 * 1024);
    EXPECT_EQ(merged->max_records, 2048);
    EXPECT_EQ(merged->max_genomic_span, 500'000);
}

TEST(Axf1ChunkPolicy, RejectsInvalidOverrideOrdering) {
    const alignx::convert::Axf1ChunkPolicyOverride policy_override{
        .target_uncompressed_bytes = 512 * 1024,
        .max_uncompressed_bytes = 256 * 1024,
        .max_records = 2048,
        .max_genomic_span = 500'000};

    const auto merged = alignx::convert::apply_axf1_chunk_policy_override(
        alignx::convert::Axf1ChunkPolicy{}, policy_override);
    ASSERT_FALSE(merged.has_value());
    EXPECT_NE(merged.error().find("target_uncompressed_bytes must not exceed max_uncompressed_bytes"),
              std::string::npos);
}
