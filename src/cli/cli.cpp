#include <CLI/CLI.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "analysis/stats.hpp"
#include "cli/runner.hpp"
#include "convert/bam_to_axf.hpp"
#include "format/axf_file.hpp"
#include "index/axf_index.hpp"
#include "index/bai_reader.hpp"
#include "index/bam_index_projection.hpp"
#include "index/csi_reader.hpp"
#include "io/bam_reader.hpp"

namespace alignx::cli {
namespace {

using Clock = std::chrono::steady_clock;

std::string lower_extension(const std::filesystem::path& path);

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

struct ParsedRegion {
    std::string reference;
    std::int32_t start = 0;
    std::int32_t end = 0;
};

bool view_profile_enabled() {
#ifdef _WIN32
    std::size_t required_size = 0;
    getenv_s(&required_size, nullptr, 0, "ALIGNX_PROFILE_VIEW");
    if (required_size == 0) {
        return false;
    }

    std::string value(required_size, '\0');
    getenv_s(&required_size, value.data(), value.size(), "ALIGNX_PROFILE_VIEW");
    if (!value.empty() && value.back() == '\0') {
        value.pop_back();
    }
    return !value.empty() && value != "0";
#else
    const char* value = std::getenv("ALIGNX_PROFILE_VIEW");
    return value != nullptr && value[0] != '\0' && std::string_view(value) != "0";
#endif
}

std::expected<std::int32_t, std::string> parse_positive_i32(std::string_view text,
                                                            std::string_view label) {
    if (text.empty()) {
        return std::unexpected("missing " + std::string(label));
    }

    std::int64_t value = 0;
    for (char ch : text) {
        if (!std::isdigit(static_cast<unsigned char>(ch))) {
            return std::unexpected("invalid " + std::string(label));
        }
        value = value * 10 + (ch - '0');
        if (value > std::numeric_limits<std::int32_t>::max()) {
            return std::unexpected(std::string(label) + " is too large");
        }
    }
    if (value <= 0) {
        return std::unexpected(std::string(label) + " must be positive");
    }
    return static_cast<std::int32_t>(value);
}

std::expected<ParsedRegion, std::string> parse_region(std::string_view region) {
    const std::size_t colon = region.find(':');
    const std::size_t dash = region.find('-', colon == std::string_view::npos ? 0 : colon + 1);
    if (colon == std::string_view::npos || dash == std::string_view::npos || colon == 0 ||
        dash <= colon + 1 || dash + 1 >= region.size()) {
        return std::unexpected("region must use ref:start-end");
    }

    auto one_based_start = parse_positive_i32(region.substr(colon + 1, dash - colon - 1), "start");
    auto one_based_end = parse_positive_i32(region.substr(dash + 1), "end");
    if (!one_based_start) {
        return std::unexpected(one_based_start.error());
    }
    if (!one_based_end) {
        return std::unexpected(one_based_end.error());
    }
    if (*one_based_start > *one_based_end) {
        return std::unexpected("region start must be <= end");
    }

    return ParsedRegion{.reference = std::string(region.substr(0, colon)),
                        .start = *one_based_start - 1,
                        .end = *one_based_end};
}

double milliseconds(Clock::duration duration) {
    return std::chrono::duration<double, std::milli>(duration).count();
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

bool is_axf_path(const std::filesystem::path& path) {
    return lower_extension(path) == ".axf";
}

std::vector<std::string_view> split_tab_fields(std::string_view line, std::size_t max_fields) {
    std::vector<std::string_view> fields;
    fields.reserve(max_fields);

    std::size_t begin = 0;
    while (fields.size() + 1 < max_fields) {
        const std::size_t tab = line.find('\t', begin);
        if (tab == std::string_view::npos) {
            break;
        }
        fields.push_back(line.substr(begin, tab - begin));
        begin = tab + 1;
    }
    fields.push_back(line.substr(begin));
    return fields;
}

std::expected<std::int32_t, std::string> cigar_reference_span(std::string_view cigar) {
    if (cigar.empty() || cigar == "*") {
        return 1;
    }

    std::int64_t span = 0;
    std::int64_t length = 0;
    bool saw_op = false;
    for (char ch : cigar) {
        if (std::isdigit(static_cast<unsigned char>(ch))) {
            length = length * 10 + (ch - '0');
            if (length > std::numeric_limits<std::int32_t>::max()) {
                return std::unexpected("CIGAR operation length is too large");
            }
            continue;
        }

        if (length == 0) {
            return std::unexpected("invalid CIGAR string");
        }
        switch (ch) {
        case 'M':
        case 'D':
        case 'N':
        case '=':
        case 'X':
            span += length;
            if (span > std::numeric_limits<std::int32_t>::max()) {
                return std::unexpected("CIGAR reference span is too large");
            }
            break;
        case 'I':
        case 'S':
        case 'H':
        case 'P':
            break;
        default:
            return std::unexpected("invalid CIGAR operation");
        }
        length = 0;
        saw_op = true;
    }
    if (!saw_op || length != 0) {
        return std::unexpected("invalid CIGAR string");
    }
    return static_cast<std::int32_t>(std::max<std::int64_t>(span, 1));
}

std::expected<bool, std::string> sam_line_overlaps_region(std::string_view line,
                                                          const ParsedRegion& region) {
    if (!line.empty() && line.back() == '\n') {
        line.remove_suffix(1);
    }

    const auto fields = split_tab_fields(line, 7);
    if (fields.size() < 6) {
        return std::unexpected("AXF payload contains malformed SAM line");
    }
    if (fields[2] != region.reference) {
        return false;
    }

    auto one_based_pos = parse_positive_i32(fields[3], "SAM POS");
    if (!one_based_pos) {
        return std::unexpected(one_based_pos.error());
    }
    auto span = cigar_reference_span(fields[5]);
    if (!span) {
        return std::unexpected(span.error());
    }

    const std::int32_t start = *one_based_pos - 1;
    const std::int32_t end = start + *span;
    return start < region.end && region.start < end;
}

std::expected<std::uint32_t, std::string> find_reference_id(const format::AxfFile& axf,
                                                            std::string_view reference) {
    for (std::size_t index = 0; index < axf.references.size(); ++index) {
        if (axf.references[index].name == reference) {
            return static_cast<std::uint32_t>(index);
        }
    }
    return std::unexpected("reference not found in AXF: " + std::string(reference));
}

int run_axf_view(const std::filesystem::path& input, const std::string& region, std::ostream& out,
                 std::ostream& err) {
    auto parsed_region = parse_region(region);
    if (!parsed_region) {
        err << "alignx view: " << parsed_region.error() << '\n';
        return 1;
    }

    auto axf = format::read_axf_file(input);
    if (!axf) {
        err << "alignx view: " << axf.error() << '\n';
        return 1;
    }

    auto ref_id = find_reference_id(*axf, parsed_region->reference);
    if (!ref_id) {
        err << "alignx view: " << ref_id.error() << '\n';
        return 1;
    }

    auto blocks = axf->query_blocks(*ref_id, parsed_region->start, parsed_region->end);
    if (!blocks) {
        err << "alignx view: " << blocks.error() << '\n';
        return 1;
    }

    for (const format::AxfBlock* block : *blocks) {
        std::string_view payload(reinterpret_cast<const char*>(block->payload.data()),
                                 block->payload.size());
        while (!payload.empty()) {
            const std::size_t newline = payload.find('\n');
            const std::string_view line =
                newline == std::string_view::npos ? payload : payload.substr(0, newline + 1);
            payload = newline == std::string_view::npos ? std::string_view{}
                                                        : payload.substr(newline + 1);
            if (line.empty()) {
                continue;
            }

            auto overlaps = sam_line_overlaps_region(line, *parsed_region);
            if (!overlaps) {
                err << "alignx view: " << overlaps.error() << '\n';
                return 1;
            }
            if (*overlaps) {
                out.write(line.data(), static_cast<std::streamsize>(line.size()));
                if (line.back() != '\n') {
                    out.put('\n');
                }
            }
        }
    }

    return 0;
}

int run_view(const std::filesystem::path& input, const std::string& region,
             std::optional<int> hts_threads, std::ostream& out, std::ostream& err) {
    if (is_axf_path(input)) {
        return run_axf_view(input, region, out, err);
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

    if (!profile_enabled) {
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
        io::SamLineProfile line_profile;
        auto line = reader->next_sam_line_view_profiled(line_profile);
        profile.read_time += line_profile.read_time;
        profile.format_time += line_profile.format_time;

        if (!line) {
            err << "alignx view: " << line.error() << '\n';
            return 1;
        }
        if (!line->has_value()) {
            break;
        }

        const auto write_start = Clock::now();
        out.write(line->value().data(), static_cast<std::streamsize>(line->value().size()));
        if (line->value().empty() || line->value().back() != '\n') {
            out.put('\n');
        }
        const auto write_end = Clock::now();

        profile.write_time += write_end - write_start;
        profile.records += 1;
        profile.stdout_bytes += line->value().size();
        if (line->value().empty() || line->value().back() != '\n') {
            profile.stdout_bytes += 1;
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
                std::ostream& out, std::ostream& err) {
    auto conversion = convert::convert_bam_to_axf_mvp(input, output);
    if (!conversion) {
        err << "alignx convert: " << conversion.error() << '\n';
        return 1;
    }

    out << "input\t" << input.string() << '\n';
    out << "output\t" << output.string() << '\n';
    out << "format\tAXF0\n";
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

} // namespace

int run(int argc, char** argv, std::ostream& out, std::ostream& err) {
    ::CLI::App app{"alignx alignment storage and query engine"};
    app.require_subcommand(1);

    std::filesystem::path view_input;
    std::string view_region;
    int view_hts_threads = -1;

    auto* view = app.add_subcommand("view", "Output SAM records for a BAM or AXF region");
    view->add_option("input", view_input, "Input BAM or AXF file")
        ->required()
        ->check(::CLI::ExistingFile);
    view->add_option("region", view_region, "Genomic region, for example chr1:1-1000")->required();
    view->add_option("--hts-threads", view_hts_threads,
                     "HTSlib worker threads for BAM/CRAM I/O; overrides ALIGNX_HTS_THREADS");

    std::filesystem::path stats_input;
    auto* stats = app.add_subcommand("stats", "Output basic BAM statistics as TSV");
    stats->add_option("input", stats_input, "Input BAM file")
        ->required()
        ->check(::CLI::ExistingFile);

    std::filesystem::path convert_input;
    std::filesystem::path convert_output;
    auto* convert_cmd = app.add_subcommand("convert", "Convert BAM to AXF MVP format");
    convert_cmd->add_option("input", convert_input, "Input BAM file")
        ->required()
        ->check(::CLI::ExistingFile);
    convert_cmd->add_option("-o,--output", convert_output, "Output AXF file")->required();

    std::filesystem::path index_input;
    std::filesystem::path index_output;
    auto* index_cmd = app.add_subcommand("index", "Build an AXF index from a BAM index");
    index_cmd->add_option("input", index_input, "Input BAM file")
        ->required()
        ->check(::CLI::ExistingFile);
    index_cmd->add_option("-o,--output", index_output, "Output .axf.idx path");

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
        return run_view(view_input, view_region, hts_threads, out, err);
    }
    if (*stats) {
        return run_stats(stats_input, out, err);
    }
    if (*convert_cmd) {
        return run_convert(convert_input, convert_output, out, err);
    }
    if (*index_cmd) {
        return run_index(index_input, index_output, out, err);
    }

    err << "alignx: no command selected\n";
    return 1;
}

} // namespace alignx::cli
