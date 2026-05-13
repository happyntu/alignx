#include <gtest/gtest.h>

#include <limits>
#include <string>

#include "query/region.hpp"

TEST(Region, ParsesSamRegionToZeroBasedHalfOpen) {
    auto region = alignx::query::parse_sam_region("chrToy:1-250");

    ASSERT_TRUE(region) << region.error();
    EXPECT_EQ(region->reference, "chrToy");
    EXPECT_EQ(region->start, 0);
    EXPECT_EQ(region->end, 250);
}

TEST(Region, AllowsSingleBaseRegion) {
    auto region = alignx::query::parse_sam_region("chrToy:151-151");

    ASSERT_TRUE(region) << region.error();
    EXPECT_EQ(region->start, 150);
    EXPECT_EQ(region->end, 151);
}

TEST(Region, RejectsMalformedShape) {
    auto region = alignx::query::parse_sam_region("chrToy");

    ASSERT_FALSE(region);
    EXPECT_NE(region.error().find("region must use ref:start-end"), std::string::npos);
}

TEST(Region, RejectsNonNumericCoordinates) {
    auto start = alignx::query::parse_sam_region("chrToy:a-10");
    auto end = alignx::query::parse_sam_region("chrToy:1-b");

    ASSERT_FALSE(start);
    ASSERT_FALSE(end);
    EXPECT_NE(start.error().find("invalid start"), std::string::npos);
    EXPECT_NE(end.error().find("invalid end"), std::string::npos);
}

TEST(Region, RejectsZeroCoordinates) {
    auto start = alignx::query::parse_sam_region("chrToy:0-10");
    auto end = alignx::query::parse_sam_region("chrToy:1-0");

    ASSERT_FALSE(start);
    ASSERT_FALSE(end);
    EXPECT_NE(start.error().find("start must be positive"), std::string::npos);
    EXPECT_NE(end.error().find("end must be positive"), std::string::npos);
}

TEST(Region, RejectsStartAfterEnd) {
    auto region = alignx::query::parse_sam_region("chrToy:250-1");

    ASSERT_FALSE(region);
    EXPECT_NE(region.error().find("region start must be <= end"), std::string::npos);
}

TEST(Region, RejectsCoordinatesLargerThanInt32) {
    const std::string too_large =
        "chrToy:1-" + std::to_string(static_cast<long long>(std::numeric_limits<int>::max()) + 1);

    auto region = alignx::query::parse_sam_region(too_large);

    ASSERT_FALSE(region);
    EXPECT_NE(region.error().find("end is too large"), std::string::npos);
}

TEST(Region, DoesNotSupportReferenceNamesContainingColon) {
    auto region = alignx::query::parse_sam_region("ref:with:colon:1-10");

    ASSERT_FALSE(region);
    EXPECT_NE(region.error().find("invalid start"), std::string::npos);
}
