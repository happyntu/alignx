#pragma once

#include <array>
#include <cstdint>
#include <iosfwd>
#include <map>

#include "io/bam_reader.hpp"

namespace alignx::analysis {

struct BamStats {
    std::uint64_t total_records = 0;
    std::uint64_t mapped_records = 0;
    std::uint64_t unmapped_records = 0;
    std::array<std::uint64_t, 256> mapq_histogram{};
    std::map<std::uint16_t, std::uint64_t> flag_histogram;
    std::map<std::int32_t, std::uint64_t> insert_size_histogram;
};

class StatsCollector {
public:
    void add(const io::BamRecord& record);

    [[nodiscard]] const BamStats& stats() const noexcept;

private:
    BamStats stats_;
};

void write_stats_tsv(const BamStats& stats, std::ostream& out);

} // namespace alignx::analysis
