#include <gtest/gtest.h>

#include <sstream>

#include "analysis/stats.hpp"

namespace {

TEST(StatsCollector, CountsToyLikeRecords) {
    alignx::analysis::StatsCollector collector;

    collector.add(alignx::io::BamRecord{.qname = "read001", .mapq = 60});
    collector.add(alignx::io::BamRecord{.qname = "read002", .flag = 16, .mapq = 50});
    collector.add(alignx::io::BamRecord{.qname = "read003", .flag = 4, .mapq = 0});

    const auto& stats = collector.stats();
    EXPECT_EQ(stats.total_records, 3);
    EXPECT_EQ(stats.mapped_records, 2);
    EXPECT_EQ(stats.unmapped_records, 1);
    EXPECT_EQ(stats.mapq_histogram.at(0), 1);
    EXPECT_EQ(stats.mapq_histogram.at(50), 1);
    EXPECT_EQ(stats.mapq_histogram.at(60), 1);
    EXPECT_EQ(stats.flag_histogram.at(0), 1);
    EXPECT_EQ(stats.flag_histogram.at(4), 1);
    EXPECT_EQ(stats.flag_histogram.at(16), 1);
}

TEST(StatsCollector, WritesStableTsv) {
    alignx::analysis::StatsCollector collector;
    collector.add(alignx::io::BamRecord{.qname = "read001", .mapq = 60});

    std::ostringstream out;
    alignx::analysis::write_stats_tsv(collector.stats(), out);

    EXPECT_EQ(out.str(), "metric\tvalue\n"
                         "records.total\t1\n"
                         "records.mapped\t1\n"
                         "records.unmapped\t0\n"
                         "mapq.60\t1\n"
                         "flag.0\t1\n");
}

} // namespace
