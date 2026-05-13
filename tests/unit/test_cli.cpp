#include <gtest/gtest.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "cli/runner.hpp"
#include "format/axf_file.hpp"
#include "index/axf_index.hpp"

namespace {

std::filesystem::path toy_bam_path() {
    return std::filesystem::path(TEST_DATA_DIR) / "toy_alignment.sorted.bam";
}

std::filesystem::path toy_bai_path() {
    return std::filesystem::path(TEST_DATA_DIR) / "toy_alignment.sorted.bam.bai";
}

std::filesystem::path toy_csi_path() {
    return std::filesystem::path(TEST_DATA_DIR) / "toy_alignment.sorted.bam.csi";
}

std::filesystem::path make_temp_dir(std::string_view label) {
    const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    auto path = std::filesystem::temp_directory_path() /
                (std::string(label) + "_" + std::to_string(suffix));
    std::filesystem::create_directories(path);
    return path;
}

int run_cli(const std::vector<std::string>& args, std::ostream& out, std::ostream& err) {
    std::vector<char*> argv;
    argv.reserve(args.size());
    for (const auto& arg : args) {
        argv.push_back(const_cast<char*>(arg.c_str()));
    }
    return alignx::cli::run(static_cast<int>(argv.size()), argv.data(), out, err);
}

void set_env_var(const char* name, const char* value) {
#ifdef _WIN32
    _putenv_s(name, value);
#else
    setenv(name, value, 1);
#endif
}

void unset_env_var(const char* name) {
#ifdef _WIN32
    _putenv_s(name, "");
#else
    unsetenv(name);
#endif
}

TEST(Cli, ViewRequiresArguments) {
    std::ostringstream out;
    std::ostringstream err;

    const int code = run_cli({"alignx", "view"}, out, err);

    EXPECT_NE(code, 0);
    EXPECT_TRUE(err.str().find("required") != std::string::npos ||
                out.str().find("required") != std::string::npos);
}

TEST(Cli, IndexWritesDefaultAxfIndexFromToyCsi) {
    const auto temp_dir = make_temp_dir("alignx_cli_index_csi");
    const auto input = temp_dir / "toy.bam";
    const auto csi = temp_dir / "toy.bam.csi";
    const auto output = temp_dir / "toy.bam.axf.idx";
    std::filesystem::copy_file(toy_bam_path(), input);
    std::filesystem::copy_file(toy_csi_path(), csi);

    std::ostringstream out;
    std::ostringstream err;
    const int code = run_cli({"alignx", "index", input.string()}, out, err);

    EXPECT_EQ(code, 0) << err.str();
    EXPECT_EQ(err.str(), "");
    EXPECT_NE(out.str().find("references\t1"), std::string::npos);
    EXPECT_NE(out.str().find("intervals\t1"), std::string::npos);
    ASSERT_TRUE(std::filesystem::is_regular_file(output));

    auto axf = alignx::index::read_axf_index(output);
    ASSERT_TRUE(axf) << axf.error();
    auto hits = axf->query(0, 100, 110);
    ASSERT_TRUE(hits) << hits.error();
    EXPECT_EQ(hits->size(), 1);

    std::filesystem::remove_all(temp_dir);
}

TEST(Cli, IndexWritesRequestedAxfIndexFromToyBai) {
    const auto temp_dir = make_temp_dir("alignx_cli_index_bai");
    const auto input = temp_dir / "toy.bam";
    const auto bai = temp_dir / "toy.bam.bai";
    const auto output = temp_dir / "requested.axf.idx";
    std::filesystem::copy_file(toy_bam_path(), input);
    std::filesystem::copy_file(toy_bai_path(), bai);

    std::ostringstream out;
    std::ostringstream err;
    const int code = run_cli({"alignx", "index", input.string(), "-o", output.string()}, out, err);

    EXPECT_EQ(code, 0) << err.str();
    EXPECT_EQ(err.str(), "");
    EXPECT_NE(out.str().find("output\t"), std::string::npos);
    ASSERT_TRUE(std::filesystem::is_regular_file(output));

    auto axf = alignx::index::read_axf_index(output);
    ASSERT_TRUE(axf) << axf.error();
    EXPECT_EQ(axf->reference_count(), 1);
    EXPECT_EQ(axf->intervals(0).size(), 1);

    std::filesystem::remove_all(temp_dir);
}

TEST(Cli, IndexReportsMissingBamIndex) {
    const auto temp_dir = make_temp_dir("alignx_cli_index_missing");
    const auto input = temp_dir / "missing_index.bam";
    {
        std::ofstream output(input, std::ios::binary);
        output << "not a real BAM";
    }

    std::ostringstream out;
    std::ostringstream err;
    const int code = run_cli({"alignx", "index", input.string()}, out, err);

    EXPECT_NE(code, 0);
    EXPECT_EQ(out.str(), "");
    EXPECT_NE(err.str().find("no BAI/CSI index found"), std::string::npos);

    std::filesystem::remove_all(temp_dir);
}

#ifdef ALIGNX_HAVE_HTSLIB

TEST(Cli, ConvertWritesToyAxfMvp) {
    const auto temp_dir = make_temp_dir("alignx_cli_convert");
    const auto output = temp_dir / "toy.axf";

    std::ostringstream out;
    std::ostringstream err;
    const int code =
        run_cli({"alignx", "convert", toy_bam_path().string(), "-o", output.string()}, out, err);

    EXPECT_EQ(code, 0) << err.str();
    EXPECT_EQ(err.str(), "");
    EXPECT_NE(out.str().find("input\t"), std::string::npos);
    EXPECT_NE(out.str().find("output\t"), std::string::npos);
    EXPECT_NE(out.str().find("format\tAXF0"), std::string::npos);
    ASSERT_TRUE(std::filesystem::is_regular_file(output));

    auto axf = alignx::format::read_axf_file(output);
    ASSERT_TRUE(axf) << axf.error();
    ASSERT_EQ(axf->references.size(), 1);
    EXPECT_EQ(axf->references[0].name, "chrToy");
    ASSERT_EQ(axf->blocks.size(), 1);
    EXPECT_EQ(axf->blocks[0].record_count, 2);

    std::filesystem::remove_all(temp_dir);
}

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

TEST(Cli, ViewProfileWritesOnlyToStderr) {
    set_env_var("ALIGNX_PROFILE_VIEW", "1");

    std::ostringstream out;
    std::ostringstream err;
    const int code = run_cli({"alignx", "view", toy_bam_path().string(), "chrToy:1-250"}, out, err);

    unset_env_var("ALIGNX_PROFILE_VIEW");

    EXPECT_EQ(code, 0) << err.str();
    EXPECT_EQ(out.str(),
              "read001\t0\tchrToy\t101\t60\t10M\t*\t0\t0\tACGTACGTAA\tFFFFFFFFFF\tNM:i:0\n"
              "read002\t16\tchrToy\t151\t50\t5M1I4M\t*\t0\t0\tTTTTACGGGA\tFFFFFFFFFF\tNM:i:1\n");
    EXPECT_NE(
        err.str().find(
            "profile\trecords\topen_ms\theader_ms\tindex_ms\tfetch_ms\tread_ms\tformat_ms\twrite_ms"
            "\ttotal_ms\tstdout_bytes"),
        std::string::npos);
    EXPECT_NE(err.str().find("view\t2\t"), std::string::npos);
    EXPECT_NE(err.str().find("\t130\n"), std::string::npos);
}

TEST(Cli, ViewAcceptsHtsThreadsOption) {
    std::ostringstream out;
    std::ostringstream err;
    const int code =
        run_cli({"alignx", "view", "--hts-threads", "1", toy_bam_path().string(), "chrToy:1-250"},
                out, err);

    EXPECT_EQ(code, 0) << err.str();
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

TEST(Cli, ConvertReportsMissingHtslib) {
    const auto temp_dir = make_temp_dir("alignx_cli_convert_missing_htslib");
    const auto output = temp_dir / "toy.axf";

    std::ostringstream out;
    std::ostringstream err;
    const int code =
        run_cli({"alignx", "convert", toy_bam_path().string(), "-o", output.string()}, out, err);

    EXPECT_NE(code, 0);
    EXPECT_EQ(out.str(), "");
    EXPECT_NE(err.str().find("without HTSlib"), std::string::npos);

    std::filesystem::remove_all(temp_dir);
}

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
