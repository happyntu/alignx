#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <string>

#include "query/sam_utils.hpp"

TEST(SamUtils, ComputesSimpleCigarReferenceSpan) {
    auto span = alignx::query::cigar_reference_span("10M");

    ASSERT_TRUE(span) << span.error();
    EXPECT_EQ(*span, 10);
}

TEST(SamUtils, ComputesReferenceConsumingCigarOperations) {
    auto span = alignx::query::cigar_reference_span("2S5M1I3D4N2=1X1H1P");

    ASSERT_TRUE(span) << span.error();
    EXPECT_EQ(*span, 15);
}

TEST(SamUtils, TreatsEmptyAndStarCigarAsOneBaseSpan) {
    auto empty_span = alignx::query::cigar_reference_span("");
    auto star_span = alignx::query::cigar_reference_span("*");

    ASSERT_TRUE(empty_span) << empty_span.error();
    ASSERT_TRUE(star_span) << star_span.error();
    EXPECT_EQ(*empty_span, 1);
    EXPECT_EQ(*star_span, 1);
}

TEST(SamUtils, RejectsInvalidCigarOperation) {
    auto span = alignx::query::cigar_reference_span("10Z");

    ASSERT_FALSE(span);
    EXPECT_NE(span.error().find("invalid CIGAR operation"), std::string::npos);
}

TEST(SamUtils, RejectsMissingCigarOperationLength) {
    auto span = alignx::query::cigar_reference_span("M");

    ASSERT_FALSE(span);
    EXPECT_NE(span.error().find("invalid CIGAR string"), std::string::npos);
}

TEST(SamUtils, RejectsTrailingCigarLength) {
    auto span = alignx::query::cigar_reference_span("10M5");

    ASSERT_FALSE(span);
    EXPECT_NE(span.error().find("invalid CIGAR string"), std::string::npos);
}

TEST(SamUtils, RejectsCigarOperationLengthOverflow) {
    const std::string cigar =
        std::to_string(static_cast<long long>(std::numeric_limits<std::int32_t>::max()) + 1) + "M";

    auto span = alignx::query::cigar_reference_span(cigar);

    ASSERT_FALSE(span);
    EXPECT_NE(span.error().find("CIGAR operation length is too large"), std::string::npos);
}

TEST(SamUtils, RejectsCigarReferenceSpanOverflow) {
    const std::string cigar = std::to_string(std::numeric_limits<std::int32_t>::max()) + "M1M";

    auto span = alignx::query::cigar_reference_span(cigar);

    ASSERT_FALSE(span);
    EXPECT_NE(span.error().find("CIGAR reference span is too large"), std::string::npos);
}

TEST(SamUtils, ComputesHalfOpenIntervalOverlap) {
    EXPECT_TRUE(alignx::query::half_open_intervals_overlap(100, 110, 109, 120));
    EXPECT_TRUE(alignx::query::half_open_intervals_overlap(100, 110, 100, 110));
    EXPECT_FALSE(alignx::query::half_open_intervals_overlap(100, 110, 110, 120));
    EXPECT_FALSE(alignx::query::half_open_intervals_overlap(100, 110, 90, 100));
}
