#include "query/bam_coverage.hpp"

#include <chrono>
#include <cstdint>
#include <string>

#include "io/bam_reader.hpp"
#include "query/region.hpp"
#include "query/sam_utils.hpp"

namespace alignx::query {
namespace {

using Clock = std::chrono::steady_clock;

std::string extract_cigar_from_sam(std::string_view sam_line) {
    std::size_t tab_count = 0;
    std::size_t field_start = 0;
    for (std::size_t i = 0; i < sam_line.size(); ++i) {
        if (sam_line[i] == '\t') {
            ++tab_count;
            if (tab_count == 5) {
                field_start = i + 1;
            }
            if (tab_count == 6) {
                return std::string(sam_line.substr(field_start, i - field_start));
            }
        }
    }
    return "*";
}

} // namespace

std::expected<analysis::CoverageResult, std::string>
compute_bam_coverage(const std::filesystem::path& input, const std::string& region,
                     std::optional<int> hts_threads) {
    BamCoverageProfile unused;
    return compute_bam_coverage_profiled(input, region, unused, hts_threads);
}

std::expected<analysis::CoverageResult, std::string>
compute_bam_coverage_profiled(const std::filesystem::path& input, const std::string& region,
                              BamCoverageProfile& profile, std::optional<int> hts_threads) {
    auto parsed_region = parse_sam_region(region);
    if (!parsed_region) {
        return std::unexpected(parsed_region.error());
    }

    analysis::CoverageResult result;
    result.reference = parsed_region->reference;
    result.start = parsed_region->start;
    result.end = parsed_region->end;
    result.depth.resize(static_cast<std::size_t>(parsed_region->end - parsed_region->start), 0);

    const auto open_start = Clock::now();
    auto reader = io::BamReader::open(input, hts_threads);
    profile.open_time += Clock::now() - open_start;
    if (!reader) {
        return std::unexpected(reader.error());
    }

    const auto fetch_start = Clock::now();
    auto fetch = reader->fetch(region);
    profile.fetch_time += Clock::now() - fetch_start;
    if (!fetch) {
        return std::unexpected(fetch.error());
    }

    for (;;) {
        const auto read_start = Clock::now();
        auto record_view = reader->next_record_view();
        profile.read_time += Clock::now() - read_start;
        if (!record_view) {
            return std::unexpected(record_view.error());
        }
        if (!record_view->has_value()) {
            break;
        }
        profile.records_scanned += 1;

        const auto& rec = record_view->value();
        if (rec.record.is_unmapped()) {
            continue;
        }
        if (!half_open_intervals_overlap(rec.record.position, rec.record.end_position,
                                         parsed_region->start, parsed_region->end)) {
            continue;
        }
        profile.records_matched += 1;

        const auto coverage_start = Clock::now();
        std::string cigar = extract_cigar_from_sam(rec.sam_line);
        auto add = analysis::add_coverage(result, rec.record.position, cigar);
        profile.coverage_time += Clock::now() - coverage_start;
        if (!add) {
            return std::unexpected(add.error());
        }
        result.records_counted += 1;
    }

    return result;
}

} // namespace alignx::query
