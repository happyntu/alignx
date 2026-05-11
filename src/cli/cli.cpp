#include <CLI/CLI.hpp>

#include <filesystem>
#include <iostream>
#include <string>

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

} // namespace

int run(int argc, char** argv, std::ostream& out, std::ostream& err) {
    ::CLI::App app{"alignx alignment storage and query engine"};
    app.require_subcommand(1);

    std::filesystem::path view_input;
    std::string view_region;

    auto* view = app.add_subcommand("view", "Output SAM records for a BAM region");
    view->add_option("input", view_input, "Input BAM file")->required()->check(::CLI::ExistingFile);
    view->add_option("region", view_region, "Genomic region, for example chr1:1-1000")->required();

    try {
        app.parse(argc, argv);
    } catch (const ::CLI::ParseError& error) {
        return app.exit(error, out, err);
    }

    if (*view) {
        return run_view(view_input, view_region, out, err);
    }

    err << "alignx: no command selected\n";
    return 1;
}

} // namespace alignx::cli
