#include <CLI/CLI.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <expected>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "analysis/stats.hpp"
#include "cli/runner.hpp"
#include "convert/axf_to_bam.hpp"
#include "convert/bam_to_axf.hpp"
#include "format/axf1_file.hpp"
#include "index/axf_index.hpp"
#include "index/bai_reader.hpp"
#include "index/bam_index_projection.hpp"
#include "index/csi_reader.hpp"
#include "io/bam_reader.hpp"
#include "query/axf1_coverage.hpp"
#include "query/axf1_view.hpp"
#include "query/axf_view.hpp"
#include "query/bam_coverage.hpp"
#include "query/record_filter.hpp"

namespace alignx::cli {
namespace {

using Clock = std::chrono::steady_clock;

std::string lower_extension(const std::filesystem::path& path);

enum class ViewInputFormat {
    bam_or_unknown,
    axf0,
    axf1,
    unsupported_axf,
};

struct ViewProfile {
    std::uint64_t records = 0;
    std::uint64_t stdout_bytes = 0;
    Clock::duration open_time{};
    Clock::duration header_time{};
    Clock::duration index_time{};
    Clock::duration fetch_time{};
    Clock::duration read_time{};
    Clock::duration format_time{};
    Clock::duration write_time{};
};

std::optional<std::string> get_env_value(const char* name);

bool view_profile_enabled() {
    const auto value = get_env_value("ALIGNX_PROFILE_VIEW");
    return value.has_value() && !value->empty() && *value != "0";
}

bool axf1_view_profile_enabled() {
    const auto value = get_env_value("ALIGNX_PROFILE_AXF1");
    return value.has_value() && !value->empty() && *value != "0";
}

double milliseconds(Clock::duration duration) {
    return std::chrono::duration<double, std::milli>(duration).count();
}

std::optional<std::string> get_env_value(const char* name) {
#ifdef _WIN32
    std::size_t required_size = 0;
    getenv_s(&required_size, nullptr, 0, name);
    if (required_size == 0) {
        return std::nullopt;
    }

    std::string value(required_size, '\0');
    getenv_s(&required_size, value.data(), value.size(), name);
    if (!value.empty() && value.back() == '\0') {
        value.pop_back();
    }
    if (value.empty()) {
        return std::nullopt;
    }
    return value;
#else
    const char* value = std::getenv(name);
    if (value == nullptr || value[0] == '\0') {
        return std::nullopt;
    }
    return std::string(value);
#endif
}

template <typename Int>
std::expected<std::optional<Int>, std::string> read_positive_integer_env(const char* name) {
    const auto raw_value = get_env_value(name);
    if (!raw_value.has_value()) {
        return std::optional<Int>{};
    }

    const std::string_view text(*raw_value);
    std::uint64_t parsed = 0;
    const auto* first = text.data();
    const auto* last = text.data() + text.size();
    const auto result = std::from_chars(first, last, parsed);
    if (result.ec != std::errc{} || result.ptr != last || parsed == 0 ||
        parsed > static_cast<std::uint64_t>(std::numeric_limits<Int>::max())) {
        return std::unexpected(std::string("alignx convert: environment variable ") + name +
                               " must be a positive integer");
    }

    return static_cast<Int>(parsed);
}

std::expected<convert::Axf1ChunkPolicyOverride, std::string>
read_axf1_chunk_policy_override_from_env() {
    convert::Axf1ChunkPolicyOverride policy_override;

    auto target_bytes =
        read_positive_integer_env<std::size_t>("ALIGNX_AXF1_TARGET_UNCOMPRESSED_BYTES");
    if (!target_bytes) {
        return std::unexpected(target_bytes.error());
    }
    policy_override.target_uncompressed_bytes = *target_bytes;

    auto max_bytes =
        read_positive_integer_env<std::size_t>("ALIGNX_AXF1_MAX_UNCOMPRESSED_BYTES");
    if (!max_bytes) {
        return std::unexpected(max_bytes.error());
    }
    policy_override.max_uncompressed_bytes = *max_bytes;

    auto max_records =
        read_positive_integer_env<std::size_t>("ALIGNX_AXF1_MAX_RECORDS");
    if (!max_records) {
        return std::unexpected(max_records.error());
    }
    policy_override.max_records = *max_records;

    auto max_span =
        read_positive_integer_env<std::int32_t>("ALIGNX_AXF1_MAX_GENOMIC_SPAN");
    if (!max_span) {
        return std::unexpected(max_span.error());
    }
    policy_override.max_genomic_span = *max_span;

    return policy_override;
}

bool has_axf1_chunk_policy_env_override() {
    return get_env_value("ALIGNX_AXF1_TARGET_UNCOMPRESSED_BYTES").has_value() ||
           get_env_value("ALIGNX_AXF1_MAX_UNCOMPRESSED_BYTES").has_value() ||
           get_env_value("ALIGNX_AXF1_MAX_RECORDS").has_value() ||
           get_env_value("ALIGNX_AXF1_MAX_GENOMIC_SPAN").has_value();
}

void write_view_profile(const ViewProfile& profile, Clock::duration total_time, std::ostream& err) {
    err << "profile\trecords\topen_ms\theader_ms\tindex_ms\tfetch_ms\tread_ms\tformat_ms\twrite_ms"
           "\ttotal_ms\tstdout_bytes\n";
    err << "view\t" << profile.records << '\t' << milliseconds(profile.open_time) << '\t'
        << milliseconds(profile.header_time) << '\t' << milliseconds(profile.index_time) << '\t'
        << milliseconds(profile.fetch_time) << '\t' << milliseconds(profile.read_time) << '\t'
        << milliseconds(profile.format_time) << '\t' << milliseconds(profile.write_time) << '\t'
        << milliseconds(total_time) << '\t' << profile.stdout_bytes << '\n';
}

void write_axf1_view_profile(const query::Axf1ViewProfile& profile, Clock::duration total_time,
                             std::ostream& err) {
    err << "profile\tchunks_selected\tchunks_with_matches\trecords_scanned\trecords_matched"
           "\trecords_filtered\trecords_output\topen_ms\tref_lookup_ms\tchunk_query_ms"
           "\tselective_decode_ms\tfilter_ms\toutput_decode_ms\tformat_ms\twrite_ms\ttotal_ms"
           "\tselective_bytes_read\toutput_bytes_read\tselective_payload_bytes"
           "\toutput_payload_bytes\tstdout_bytes\n";
    err << "axf1_view\t" << profile.chunks_selected << '\t' << profile.chunks_with_matches << '\t'
        << profile.records_scanned << '\t' << profile.records_matched << '\t'
        << profile.records_filtered << '\t' << profile.records_output << '\t' << milliseconds(profile.open_time) << '\t'
        << milliseconds(profile.reference_lookup_time) << '\t'
        << milliseconds(profile.chunk_query_time) << '\t'
        << milliseconds(profile.selective_decode_time) << '\t'
        << milliseconds(profile.filter_time) << '\t' << milliseconds(profile.output_decode_time)
        << '\t' << milliseconds(profile.format_time) << '\t'
        << milliseconds(profile.write_time) << '\t' << milliseconds(total_time) << '\t'
        << profile.selective_bytes_read << '\t' << profile.output_bytes_read << '\t'
        << profile.selective_payload_bytes << '\t' << profile.output_payload_bytes << '\t'
        << profile.stdout_bytes << '\n';
}

bool is_axf_path(const std::filesystem::path& path) {
    return lower_extension(path) == ".axf";
}

bool is_axf1_path(const std::filesystem::path& path) {
    return lower_extension(path) == ".axf1";
}

ViewInputFormat detect_view_input_format(const std::filesystem::path& input) {
    std::ifstream stream(input, std::ios::binary);
    if (!stream) {
        return ViewInputFormat::bam_or_unknown;
    }

    std::array<char, 4> magic{};
    stream.read(magic.data(), static_cast<std::streamsize>(magic.size()));
    if (stream.gcount() == static_cast<std::streamsize>(magic.size())) {
        if (magic == std::array<char, 4>{'A', 'X', 'F', '0'}) {
            return ViewInputFormat::axf0;
        }
        if (magic == std::array<char, 4>{'A', 'X', 'F', '1'}) {
            return ViewInputFormat::axf1;
        }
    }

    if (is_axf_path(input) || is_axf1_path(input)) {
        return ViewInputFormat::unsupported_axf;
    }
    return ViewInputFormat::bam_or_unknown;
}

int run_axf_view(const std::filesystem::path& input, const std::string& region, std::ostream& out,
                 std::ostream& err) {
    auto result = query::write_axf_region_sam(input, region, out);
    if (!result) {
        err << "alignx view: " << result.error() << '\n';
        return 1;
    }
    return 0;
}

int run_axf1_view(const std::filesystem::path& input, const std::string& region,
                  const query::RecordFilter& filter, std::ostream& out, std::ostream& err) {
    const bool profile_enabled = axf1_view_profile_enabled();
    const auto total_start = profile_enabled ? Clock::now() : Clock::time_point{};

    query::Axf1ViewProfile profile;
    auto result =
        profile_enabled
            ? query::write_axf1_region_sam_profiled(input, region, out, profile, filter)
            : query::write_axf1_region_sam(input, region, out, filter);
    if (!result) {
        err << "alignx view: " << result.error() << '\n';
        return 1;
    }
    if (profile_enabled) {
        write_axf1_view_profile(profile, Clock::now() - total_start, err);
    }
    return 0;
}

int run_view(const std::filesystem::path& input, const std::string& region,
             std::optional<int> hts_threads, const query::RecordFilter& filter, std::ostream& out,
             std::ostream& err) {
    switch (detect_view_input_format(input)) {
    case ViewInputFormat::axf0:
        return run_axf_view(input, region, out, err);
    case ViewInputFormat::axf1:
        return run_axf1_view(input, region, filter, out, err);
    case ViewInputFormat::unsupported_axf:
        err << "alignx view: unsupported AXF file magic\n";
        return 1;
    case ViewInputFormat::bam_or_unknown:
        break;
    }

    const bool profile_enabled = view_profile_enabled();
    const auto total_start = profile_enabled ? Clock::now() : Clock::time_point{};

    ViewProfile profile;
    io::BamOpenProfile open_profile;
    auto reader = profile_enabled ? io::BamReader::open_profiled(input, open_profile, hts_threads)
                                  : io::BamReader::open(input, hts_threads);
    profile.open_time = open_profile.open_time;
    profile.header_time = open_profile.header_time;
    profile.index_time = open_profile.index_time;
    if (!reader) {
        err << "alignx view: " << reader.error() << '\n';
        return 1;
    }

    auto fetch = profile_enabled ? reader->fetch_profiled(region, profile.fetch_time)
                                 : reader->fetch(region);
    if (!fetch) {
        err << "alignx view: " << fetch.error() << '\n';
        return 1;
    }

    if (!profile_enabled && !filter.is_active()) {
        for (;;) {
            auto line = reader->next_sam_line_view();
            if (!line) {
                err << "alignx view: " << line.error() << '\n';
                return 1;
            }
            if (!line->has_value()) {
                break;
            }

            out.write(line->value().data(), static_cast<std::streamsize>(line->value().size()));
            if (line->value().empty() || line->value().back() != '\n') {
                out.put('\n');
            }
        }

        return 0;
    }

    for (;;) {
        const auto read_start = Clock::now();
        auto record_view = reader->next_record_view();
        if (profile_enabled) {
            profile.read_time += Clock::now() - read_start;
        }

        if (!record_view) {
            err << "alignx view: " << record_view.error() << '\n';
            return 1;
        }
        if (!record_view->has_value()) {
            break;
        }

        const auto& rec = record_view->value();
        if (filter.is_active() &&
            !passes_filter(filter, rec.record.flag, rec.record.mapq)) {
            continue;
        }

        const auto write_start = Clock::now();
        out.write(rec.sam_line.data(), static_cast<std::streamsize>(rec.sam_line.size()));
        if (rec.sam_line.empty() || rec.sam_line.back() != '\n') {
            out.put('\n');
        }
        const auto write_end = Clock::now();

        if (profile_enabled) {
            profile.write_time += write_end - write_start;
            profile.records += 1;
            profile.stdout_bytes += rec.sam_line.size();
            if (rec.sam_line.empty() || rec.sam_line.back() != '\n') {
                profile.stdout_bytes += 1;
            }
        }
    }

    write_view_profile(profile, Clock::now() - total_start, err);
    return 0;
}

int run_stats(const std::filesystem::path& input, std::ostream& out, std::ostream& err) {
    auto reader = io::BamReader::open(input);
    if (!reader) {
        err << "alignx stats: " << reader.error() << '\n';
        return 1;
    }

    analysis::StatsCollector collector;
    for (;;) {
        auto record = reader->next_record();
        if (!record) {
            err << "alignx stats: " << record.error() << '\n';
            return 1;
        }
        if (!record->has_value()) {
            break;
        }

        collector.add(**record);
    }

    analysis::write_stats_tsv(collector.stats(), out);
    return 0;
}

int run_convert(const std::filesystem::path& input, const std::filesystem::path& output,
                const std::optional<std::string>& region, std::string_view format,
                std::string_view axf1_quality_compression, std::ostream& out, std::ostream& err) {
    std::string normalized_format(format);
    std::transform(normalized_format.begin(), normalized_format.end(), normalized_format.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });
    if (normalized_format != "AXF0" && normalized_format != "AXF1") {
        err << "alignx convert: --format must be AXF0 or AXF1\n";
        return 1;
    }

    std::string normalized_quality_compression(axf1_quality_compression);
    std::transform(normalized_quality_compression.begin(), normalized_quality_compression.end(),
                   normalized_quality_compression.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    if (normalized_quality_compression != "none" && normalized_quality_compression != "zstd") {
        err << "alignx convert: --axf1-quality-compression must be none or zstd\n";
        return 1;
    }
    if (normalized_format != "AXF1" && normalized_quality_compression != "none") {
        err << "alignx convert: --axf1-quality-compression requires --format AXF1\n";
        return 1;
    }

    convert::Axf1ChunkPolicy chunk_policy;
    if (has_axf1_chunk_policy_env_override()) {
        auto override_or_error = read_axf1_chunk_policy_override_from_env();
        if (!override_or_error) {
            err << override_or_error.error() << '\n';
            return 1;
        }
        auto merged = convert::apply_axf1_chunk_policy_override(chunk_policy, *override_or_error);
        if (!merged) {
            err << "alignx convert: " << merged.error() << '\n';
            return 1;
        }
        chunk_policy = *merged;
    }

    format::Axf1WriteOptions axf1_options;
    if (normalized_quality_compression == "zstd") {
        axf1_options.quality_compression = format::Axf1Compression::zstd;
    }

    auto conversion = normalized_format == "AXF1"
                          ? convert::convert_bam_to_axf1_mvp(input, output, region, axf1_options,
                                                             chunk_policy)
                          : convert::convert_bam_to_axf_mvp(input, output, region);
    if (!conversion) {
        err << "alignx convert: " << conversion.error() << '\n';
        return 1;
    }

    out << "input\t" << input.string() << '\n';
    out << "output\t" << output.string() << '\n';
    if (region.has_value()) {
        out << "region\t" << *region << '\n';
    }
    out << "format\t" << normalized_format << '\n';
    if (normalized_format == "AXF1") {
        out << "axf1_quality_compression\t" << normalized_quality_compression << '\n';
    }
    return 0;
}

void add_unique_path(std::vector<std::filesystem::path>& paths, std::filesystem::path path) {
    if (std::find(paths.begin(), paths.end(), path) == paths.end()) {
        paths.push_back(std::move(path));
    }
}

std::vector<std::filesystem::path> bam_index_candidates(const std::filesystem::path& input) {
    std::vector<std::filesystem::path> candidates;

    std::filesystem::path csi_sidecar = input;
    csi_sidecar += ".csi";
    add_unique_path(candidates, csi_sidecar);

    std::filesystem::path bai_sidecar = input;
    bai_sidecar += ".bai";
    add_unique_path(candidates, bai_sidecar);

    if (input.has_extension()) {
        std::filesystem::path csi_replaced = input;
        csi_replaced.replace_extension(".csi");
        add_unique_path(candidates, csi_replaced);

        std::filesystem::path bai_replaced = input;
        bai_replaced.replace_extension(".bai");
        add_unique_path(candidates, bai_replaced);
    }

    return candidates;
}

std::optional<std::filesystem::path> find_bam_index(const std::filesystem::path& input) {
    for (const auto& candidate : bam_index_candidates(input)) {
        if (std::filesystem::is_regular_file(candidate)) {
            return candidate;
        }
    }
    return std::nullopt;
}

std::string lower_extension(const std::filesystem::path& path) {
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return extension;
}

std::expected<index::AxfIndex, std::string>
read_projected_bam_index(const std::filesystem::path& bam_index_path) {
    const auto extension = lower_extension(bam_index_path);
    if (extension == ".csi") {
        auto csi = index::read_csi_index(bam_index_path);
        if (!csi) {
            return std::unexpected(csi.error());
        }
        return index::project_csi_to_axf_index(*csi);
    }
    if (extension == ".bai") {
        auto bai = index::read_bai_index(bam_index_path);
        if (!bai) {
            return std::unexpected(bai.error());
        }
        return index::project_bai_to_axf_index(*bai);
    }
    return std::unexpected("unsupported BAM index extension: " + bam_index_path.string());
}

std::filesystem::path default_axf_index_path(const std::filesystem::path& input) {
    std::filesystem::path output = input;
    output += ".axf.idx";
    return output;
}

std::uint64_t interval_count(const index::AxfIndex& axf) {
    std::uint64_t count = 0;
    for (std::uint32_t ref_id = 0; ref_id < axf.reference_count(); ++ref_id) {
        count += axf.intervals(ref_id).size();
    }
    return count;
}

int run_index(const std::filesystem::path& input, const std::filesystem::path& requested_output,
              std::ostream& out, std::ostream& err) {
    const auto bam_index_path = find_bam_index(input);
    if (!bam_index_path.has_value()) {
        err << "alignx index: no BAI/CSI index found for: " << input.string() << '\n';
        return 1;
    }

    auto axf = read_projected_bam_index(*bam_index_path);
    if (!axf) {
        err << "alignx index: " << axf.error() << '\n';
        return 1;
    }

    const auto output = requested_output.empty() ? default_axf_index_path(input) : requested_output;
    auto write = index::write_axf_index(*axf, output);
    if (!write) {
        err << "alignx index: " << write.error() << '\n';
        return 1;
    }

    out << "source\t" << bam_index_path->string() << '\n';
    out << "output\t" << output.string() << '\n';
    out << "references\t" << axf->reference_count() << '\n';
    out << "intervals\t" << interval_count(*axf) << '\n';
    return 0;
}

bool coverage_profile_enabled() {
    const auto value = get_env_value("ALIGNX_PROFILE_COVERAGE");
    return value.has_value() && !value->empty() && *value != "0";
}

void write_axf1_coverage_profile(const query::Axf1CoverageProfile& profile,
                                 Clock::duration total_time, std::ostream& err) {
    err << "profile\tchunks_selected\tchunks_with_matches\trecords_scanned\trecords_matched"
           "\trecords_filtered\topen_ms\tref_lookup_ms\tchunk_query_ms\tselective_decode_ms"
           "\tfilter_ms\tcoverage_ms\ttotal_ms\tselective_bytes_read"
           "\tselective_payload_bytes\n";
    err << "axf1_coverage\t" << profile.chunks_selected << '\t' << profile.chunks_with_matches
        << '\t' << profile.records_scanned << '\t' << profile.records_matched << '\t'
        << profile.records_filtered << '\t'
        << milliseconds(profile.open_time) << '\t' << milliseconds(profile.reference_lookup_time)
        << '\t' << milliseconds(profile.chunk_query_time) << '\t'
        << milliseconds(profile.selective_decode_time) << '\t' << milliseconds(profile.filter_time)
        << '\t' << milliseconds(profile.coverage_time) << '\t' << milliseconds(total_time) << '\t'
        << profile.selective_bytes_read << '\t' << profile.selective_payload_bytes << '\n';
}

void write_bam_coverage_profile(const query::BamCoverageProfile& profile,
                                Clock::duration total_time, std::ostream& err) {
    err << "profile\trecords_scanned\trecords_matched\trecords_filtered\topen_ms\tfetch_ms"
           "\tread_ms\tcoverage_ms\ttotal_ms\n";
    err << "bam_coverage\t" << profile.records_scanned << '\t' << profile.records_matched << '\t'
        << profile.records_filtered << '\t'
        << milliseconds(profile.open_time) << '\t' << milliseconds(profile.fetch_time) << '\t'
        << milliseconds(profile.read_time) << '\t' << milliseconds(profile.coverage_time) << '\t'
        << milliseconds(total_time) << '\n';
}

void write_coverage_tsv(const analysis::CoverageResult& result, std::ostream& out) {
    for (std::size_t i = 0; i < result.depth.size(); ++i) {
        const auto pos_1based = result.start + static_cast<std::int32_t>(i) + 1;
        out << result.reference << '\t' << pos_1based << '\t' << result.depth[i] << '\n';
    }
}

void write_coverage_summary(const analysis::CoverageResult& result, std::ostream& out) {
    out << "reference\t" << result.reference << '\n';
    out << "start\t" << (result.start + 1) << '\n';
    out << "end\t" << result.end << '\n';
    out << "bases\t" << result.depth.size() << '\n';
    out << "records\t" << result.records_counted << '\n';
    std::uint64_t covered = 0;
    std::uint64_t total_depth = 0;
    for (auto d : result.depth) {
        if (d > 0) {
            ++covered;
        }
        total_depth += d;
    }
    out << "covered_bases\t" << covered << '\n';
    out << "total_depth\t" << total_depth << '\n';
    if (!result.depth.empty()) {
        const double mean =
            static_cast<double>(total_depth) / static_cast<double>(result.depth.size());
        out << "mean_depth\t" << mean << '\n';
    }
}

int run_coverage(const std::filesystem::path& input, const std::string& region,
                 std::string_view output_mode, std::optional<int> hts_threads,
                 const query::RecordFilter& filter, std::ostream& out, std::ostream& err) {
    const bool profile_enabled = coverage_profile_enabled();
    const auto total_start = profile_enabled ? Clock::now() : Clock::time_point{};

    const auto format = detect_view_input_format(input);
    if (format == ViewInputFormat::axf1) {
        query::Axf1CoverageProfile profile;
        auto result = profile_enabled
                          ? query::compute_axf1_coverage_profiled(input, region, profile, filter)
                          : query::compute_axf1_coverage(input, region, filter);
        if (!result) {
            err << "alignx coverage: " << result.error() << '\n';
            return 1;
        }
        if (output_mode == "tsv") {
            write_coverage_tsv(*result, out);
        } else {
            write_coverage_summary(*result, out);
        }
        if (profile_enabled) {
            write_axf1_coverage_profile(profile, Clock::now() - total_start, err);
        }
        return 0;
    }

    if (format == ViewInputFormat::axf0 || format == ViewInputFormat::unsupported_axf) {
        err << "alignx coverage: AXF0 coverage is not supported; use AXF1 or BAM input\n";
        return 1;
    }

    query::BamCoverageProfile profile;
    auto result =
        profile_enabled
            ? query::compute_bam_coverage_profiled(input, region, profile, filter, hts_threads)
            : query::compute_bam_coverage(input, region, filter, hts_threads);
    if (!result) {
        err << "alignx coverage: " << result.error() << '\n';
        return 1;
    }
    if (output_mode == "tsv") {
        write_coverage_tsv(*result, out);
    } else {
        write_coverage_summary(*result, out);
    }
    if (profile_enabled) {
        write_bam_coverage_profile(profile, Clock::now() - total_start, err);
    }
    return 0;
}

} // namespace

int run(int argc, char** argv, std::ostream& out, std::ostream& err) {
    ::CLI::App app{"alignx alignment storage and query engine"};
    app.require_subcommand(1);

    std::filesystem::path view_input;
    std::string view_region;
    int view_hts_threads = -1;
    std::uint16_t view_flag_exclude = 0;
    std::uint8_t view_min_mapq = 0;

    auto* view = app.add_subcommand("view", "Output SAM records for a BAM or AXF region");
    view->add_option("input", view_input, "Input BAM or AXF file")
        ->required()
        ->check(::CLI::ExistingFile);
    view->add_option("region", view_region, "Genomic region, for example chr1:1-1000")->required();
    view->add_option("--hts-threads", view_hts_threads,
                     "HTSlib worker threads for BAM/CRAM I/O; overrides ALIGNX_HTS_THREADS");
    view->add_option("--flag-exclude", view_flag_exclude,
                     "Exclude records with any of these FLAG bits set");
    view->add_option("--min-mapq", view_min_mapq, "Exclude records below this MAPQ value");

    std::filesystem::path stats_input;
    auto* stats = app.add_subcommand("stats", "Output basic BAM statistics as TSV");
    stats->add_option("input", stats_input, "Input BAM file")
        ->required()
        ->check(::CLI::ExistingFile);

    std::filesystem::path convert_input;
    std::filesystem::path convert_output;
    std::optional<std::string> convert_region;
    std::string convert_format = "AXF0";
    std::string convert_axf1_quality_compression = "none";
    auto* convert_cmd = app.add_subcommand("convert", "Convert BAM to AXF MVP format");
    convert_cmd->add_option("input", convert_input, "Input BAM file")
        ->required()
        ->check(::CLI::ExistingFile);
    convert_cmd->add_option("-o,--output", convert_output, "Output AXF file")->required();
    convert_cmd->add_option("--region", convert_region,
                            "Optional BAM region to convert, for example chr1:1-1000");
    convert_cmd->add_option("--format", convert_format, "Output AXF format: AXF0 or AXF1");
    convert_cmd->add_option("--axf1-quality-compression", convert_axf1_quality_compression,
                            "AXF1 quality payload compression: none or zstd");

    std::filesystem::path coverage_input;
    std::string coverage_region;
    std::string coverage_output_mode = "summary";
    int coverage_hts_threads = -1;
    std::uint16_t coverage_flag_exclude = 0;
    std::uint8_t coverage_min_mapq = 0;
    auto* coverage_cmd =
        app.add_subcommand("coverage", "Compute per-base coverage for a BAM or AXF1 region");
    coverage_cmd->add_option("input", coverage_input, "Input BAM or AXF1 file")
        ->required()
        ->check(::CLI::ExistingFile);
    coverage_cmd->add_option("region", coverage_region, "Genomic region, for example chr1:1-1000")
        ->required();
    coverage_cmd->add_option("--output-mode", coverage_output_mode,
                             "Output format: summary or tsv");
    coverage_cmd->add_option("--hts-threads", coverage_hts_threads,
                             "HTSlib worker threads for BAM I/O");
    coverage_cmd->add_option("--flag-exclude", coverage_flag_exclude,
                             "Exclude records with any of these FLAG bits set");
    coverage_cmd->add_option("--min-mapq", coverage_min_mapq,
                             "Exclude records below this MAPQ value");

    std::filesystem::path pileup_input;
    std::string pileup_region;
    int pileup_hts_threads = -1;
    std::uint16_t pileup_flag_exclude = 0;
    std::uint8_t pileup_min_mapq = 0;
    auto* pileup_cmd =
        app.add_subcommand("pileup", "Compute per-base pileup depth for a BAM or AXF1 region");
    pileup_cmd->add_option("input", pileup_input, "Input BAM or AXF1 file")
        ->required()
        ->check(::CLI::ExistingFile);
    pileup_cmd->add_option("region", pileup_region, "Genomic region, for example chr1:1-1000")
        ->required();
    pileup_cmd->add_option("--hts-threads", pileup_hts_threads,
                           "HTSlib worker threads for BAM I/O");
    pileup_cmd->add_option("--flag-exclude", pileup_flag_exclude,
                           "Exclude records with any of these FLAG bits set");
    pileup_cmd->add_option("--min-mapq", pileup_min_mapq,
                           "Exclude records below this MAPQ value");

    std::filesystem::path index_input;
    std::filesystem::path index_output;
    auto* index_cmd = app.add_subcommand("index", "Build an AXF index from a BAM index");
    index_cmd->add_option("input", index_input, "Input BAM file")
        ->required()
        ->check(::CLI::ExistingFile);
    index_cmd->add_option("-o,--output", index_output, "Output .axf.idx path");

    std::filesystem::path export_input;
    std::filesystem::path export_output;
    int export_hts_threads = -1;
    auto* export_cmd = app.add_subcommand("export", "Export AXF1 to BAM");
    export_cmd->add_option("input", export_input, "Input AXF1 file")
        ->required()
        ->check(::CLI::ExistingFile);
    export_cmd->add_option("-o,--output", export_output, "Output BAM file")->required();
    export_cmd->add_option("--hts-threads", export_hts_threads, "HTSlib worker threads");

    try {
        app.parse(argc, argv);
    } catch (const ::CLI::ParseError& error) {
        return app.exit(error, out, err);
    }

    if (*view) {
        if (view_hts_threads < -1) {
            err << "alignx view: --hts-threads must be a non-negative integer\n";
            return 1;
        }
        const auto hts_threads =
            view_hts_threads >= 0 ? std::optional<int>{view_hts_threads} : std::nullopt;
        const query::RecordFilter view_filter{.flag_exclude = view_flag_exclude,
                                              .min_mapq = view_min_mapq};
        return run_view(view_input, view_region, hts_threads, view_filter, out, err);
    }
    if (*stats) {
        return run_stats(stats_input, out, err);
    }
    if (*convert_cmd) {
        return run_convert(convert_input, convert_output, convert_region, convert_format,
                           convert_axf1_quality_compression, out, err);
    }
    if (*coverage_cmd) {
        if (coverage_output_mode != "summary" && coverage_output_mode != "tsv") {
            err << "alignx coverage: --output-mode must be summary or tsv\n";
            return 1;
        }
        if (coverage_hts_threads < -1) {
            err << "alignx coverage: --hts-threads must be a non-negative integer\n";
            return 1;
        }
        const auto hts_threads =
            coverage_hts_threads >= 0 ? std::optional<int>{coverage_hts_threads} : std::nullopt;
        const query::RecordFilter coverage_filter{.flag_exclude = coverage_flag_exclude,
                                                  .min_mapq = coverage_min_mapq};
        return run_coverage(coverage_input, coverage_region, coverage_output_mode, hts_threads,
                            coverage_filter, out, err);
    }
    if (*pileup_cmd) {
        if (pileup_hts_threads < -1) {
            err << "alignx pileup: --hts-threads must be a non-negative integer\n";
            return 1;
        }
        const auto hts_threads =
            pileup_hts_threads >= 0 ? std::optional<int>{pileup_hts_threads} : std::nullopt;
        const query::RecordFilter pileup_filter{.flag_exclude = pileup_flag_exclude,
                                                .min_mapq = pileup_min_mapq};
        return run_coverage(pileup_input, pileup_region, "tsv", hts_threads, pileup_filter, out,
                            err);
    }
    if (*index_cmd) {
        return run_index(index_input, index_output, out, err);
    }
    if (*export_cmd) {
        const auto format = detect_view_input_format(export_input);
        if (format != ViewInputFormat::axf1) {
            err << "alignx export: input must be an AXF1 file\n";
            return 1;
        }
        if (export_hts_threads < -1) {
            err << "alignx export: --hts-threads must be a non-negative integer\n";
            return 1;
        }
        const auto hts_threads =
            export_hts_threads >= 0 ? std::optional<int>{export_hts_threads} : std::nullopt;
        auto result = convert::convert_axf1_to_bam(export_input, export_output, hts_threads);
        if (!result) {
            err << "alignx export: " << result.error() << '\n';
            return 1;
        }
        out << "input\t" << export_input.string() << '\n';
        out << "output\t" << export_output.string() << '\n';
        return 0;
    }

    err << "alignx: no command selected\n";
    return 1;
}

} // namespace alignx::cli
