#include <gtest/gtest.h>

#include <filesystem>
#include <sstream>
#include <string>
#include <vector>

#include "cli/runner.hpp"

namespace {

std::filesystem::path toy_bam_path() {
    return std::filesystem::path(TEST_DATA_DIR) / "toy_alignment.sorted.bam";
}

int run_cli(const std::vector<std::string>& args, std::ostream& out, std::ostream& err) {
    std::vector<char*> argv;
    argv.reserve(args.size());
    for (const auto& arg : args) {
        argv.push_back(const_cast<char*>(arg.c_str()));
    }
    return alignx::cli::run(static_cast<int>(argv.size()), argv.data(), out, err);
}

TEST(Cli, ViewRequiresArguments) {
    std::ostringstream out;
    std::ostringstream err;

    const int code = run_cli({"alignx", "view"}, out, err);

    EXPECT_NE(code, 0);
    EXPECT_TRUE(err.str().find("required") != std::string::npos ||
                out.str().find("required") != std::string::npos);
}

#ifdef ALIGNX_HAVE_HTSLIB

TEST(Cli, ViewOutputsToyRegionRecords) {
    std::ostringstream out;
    std::ostringstream err;

    const int code = run_cli({"alignx", "view", toy_bam_path().string(), "chrToy:1-250"}, out, err);

    EXPECT_EQ(code, 0) << err.str();
    EXPECT_EQ(err.str(), "");
    EXPECT_EQ(out.str(),
              "read001\t0\tchrToy\t101\t60\t10M\t*\t0\t0\tACGTACGTAA\tFFFFFFFFFF\tNM:i:0\n"
              "read002\t16\tchrToy\t151\t50\t5M1I4M\t*\t0\t0\tTTTTACGGGA\tFFFFFFFFFF\tNM:i:1\n");
}

TEST(Cli, StatsOutputsToyBamSummary) {
    std::ostringstream out;
    std::ostringstream err;

    const int code = run_cli({"alignx", "stats", toy_bam_path().string()}, out, err);

    EXPECT_EQ(code, 0) << err.str();
    EXPECT_EQ(err.str(), "");
    EXPECT_EQ(out.str(), "metric\tvalue\n"
                         "records.total\t3\n"
                         "records.mapped\t2\n"
                         "records.unmapped\t1\n"
                         "mapq.0\t1\n"
                         "mapq.50\t1\n"
                         "mapq.60\t1\n"
                         "flag.0\t1\n"
                         "flag.4\t1\n"
                         "flag.16\t1\n");
}

#else

TEST(Cli, ViewReportsMissingHtslib) {
    std::ostringstream out;
    std::ostringstream err;

    const int code = run_cli({"alignx", "view", toy_bam_path().string(), "chrToy:1-250"}, out, err);

    EXPECT_NE(code, 0);
    EXPECT_EQ(out.str(), "");
    EXPECT_NE(err.str().find("without HTSlib"), std::string::npos);
}

TEST(Cli, StatsReportsMissingHtslib) {
    std::ostringstream out;
    std::ostringstream err;

    const int code = run_cli({"alignx", "stats", toy_bam_path().string()}, out, err);

    EXPECT_NE(code, 0);
    EXPECT_EQ(out.str(), "");
    EXPECT_NE(err.str().find("without HTSlib"), std::string::npos);
}

#endif

} // namespace
