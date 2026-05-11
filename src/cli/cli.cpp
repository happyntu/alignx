#include <CLI/CLI.hpp>

#include <filesystem>
#include <iostream>
#include <string>

#include "analysis/stats.hpp"
#include "cli/runner.hpp"
#include "io/bam_reader.hpp"

namespace alignx::cli {
namespace {

int run_view(const std::filesystem::path& input, const std::string& region, std::ostream& out,
             std::ostream& err) {
    auto reader = io::BamReader::open(input);
    if (!reader) {
        err << "alignx view: " << reader.error() << '\n';
        return 1;
    }

    auto fetch = reader->fetch(region);
    if (!fetch) {
        err << "alignx view: " << fetch.error() << '\n';
        return 1;
    }

    for (;;) {
        auto line = reader->next_sam_line();
        if (!line) {
            err << "alignx view: " << line.error() << '\n';
            return 1;
        }
        if (!line->has_value()) {
            break;
        }

        out << **line;
        if (line->value().empty() || line->value().back() != '\n') {
            out << '\n';
        }
    }

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

} // namespace

int run(int argc, char** argv, std::ostream& out, std::ostream& err) {
    ::CLI::App app{"alignx alignment storage and query engine"};
    app.require_subcommand(1);

    std::filesystem::path view_input;
    std::string view_region;

    auto* view = app.add_subcommand("view", "Output SAM records for a BAM region");
    view->add_option("input", view_input, "Input BAM file")->required()->check(::CLI::ExistingFile);
    view->add_option("region", view_region, "Genomic region, for example chr1:1-1000")->required();

    std::filesystem::path stats_input;
    auto* stats = app.add_subcommand("stats", "Output basic BAM statistics as TSV");
    stats->add_option("input", stats_input, "Input BAM file")
        ->required()
        ->check(::CLI::ExistingFile);

    try {
        app.parse(argc, argv);
    } catch (const ::CLI::ParseError& error) {
        return app.exit(error, out, err);
    }

    if (*view) {
        return run_view(view_input, view_region, out, err);
    }
    if (*stats) {
        return run_stats(stats_input, out, err);
    }

    err << "alignx: no command selected\n";
    return 1;
}

} // namespace alignx::cli
