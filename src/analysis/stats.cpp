#include "analysis/stats.hpp"

#include <ostream>

namespace alignx::analysis {

void StatsCollector::add(const io::BamRecord& record) {
    ++stats_.total_records;

    if (record.is_unmapped()) {
        ++stats_.unmapped_records;
    } else {
        ++stats_.mapped_records;
    }

    ++stats_.mapq_histogram.at(record.mapq);
    ++stats_.flag_histogram[record.flag];

    if (record.template_length != 0) {
        ++stats_.insert_size_histogram[record.template_length];
    }
}

const BamStats& StatsCollector::stats() const noexcept {
    return stats_;
}

void write_stats_tsv(const BamStats& stats, std::ostream& out) {
    out << "metric\tvalue\n";
    out << "records.total\t" << stats.total_records << '\n';
    out << "records.mapped\t" << stats.mapped_records << '\n';
    out << "records.unmapped\t" << stats.unmapped_records << '\n';

    for (std::size_t mapq = 0; mapq < stats.mapq_histogram.size(); ++mapq) {
        const std::uint64_t count = stats.mapq_histogram.at(mapq);
        if (count != 0) {
            out << "mapq." << mapq << '\t' << count << '\n';
        }
    }

    for (const auto& [flag, count] : stats.flag_histogram) {
        out << "flag." << flag << '\t' << count << '\n';
    }

    for (const auto& [insert_size, count] : stats.insert_size_histogram) {
        out << "insert_size." << insert_size << '\t' << count << '\n';
    }
}

} // namespace alignx::analysis
