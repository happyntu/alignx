#include "io/bam_reader.hpp"

#include <charconv>
#include <cstdlib>
#include <limits>
#include <string>
#include <utility>

#ifdef ALIGNX_HAVE_HTSLIB
#include <htslib/sam.h>
#endif

namespace alignx::io {

bool BamRecord::is_unmapped() const noexcept {
    return (flag & 0x4U) != 0;
}

#ifdef ALIGNX_HAVE_HTSLIB
namespace {

void destroy_iter(hts_itr_t* iter) noexcept {
    if (iter != nullptr) {
        hts_itr_destroy(iter);
    }
}

BamRecord to_record(const bam_hdr_t* header, const bam1_t* record) {
    BamRecord out;
    out.qname = bam_get_qname(record);
    out.position = record->core.pos;
    out.template_length = record->core.isize;
    out.flag = static_cast<std::uint16_t>(record->core.flag);
    out.mapq = static_cast<std::uint8_t>(record->core.qual);

    if (record->core.tid >= 0) {
        if (const char* name = sam_hdr_tid2name(header, record->core.tid); name != nullptr) {
            out.reference_name = name;
        }
        out.end_position = bam_endpos(record);
    } else {
        out.reference_name = "*";
        out.position = -1;
        out.end_position = -1;
    }

    return out;
}

std::expected<bool, std::string> read_next_raw_record(samFile* file, bam_hdr_t* header,
                                                      hts_itr_t* iter, bam1_t* record) {
    const int result =
        iter != nullptr ? sam_itr_next(file, iter, record) : sam_read1(file, header, record);

    if (result >= 0) {
        return true;
    }
    if (result == -1) {
        return false;
    }
    return std::unexpected("failed while reading BAM record");
}

std::expected<int, std::string> configured_hts_threads(std::optional<int> override_threads) {
    if (override_threads.has_value()) {
        if (*override_threads < 0) {
            return std::unexpected("HTSlib thread count must be a non-negative integer");
        }
        return *override_threads;
    }

#ifdef _WIN32
    std::size_t required_size = 0;
    getenv_s(&required_size, nullptr, 0, "ALIGNX_HTS_THREADS");
    if (required_size == 0) {
        return 0;
    }

    std::string value(required_size, '\0');
    getenv_s(&required_size, value.data(), value.size(), "ALIGNX_HTS_THREADS");
    if (!value.empty() && value.back() == '\0') {
        value.pop_back();
    }
#else
    const char* raw_value = std::getenv("ALIGNX_HTS_THREADS");
    if (raw_value == nullptr) {
        return 0;
    }
    std::string value(raw_value);
#endif

    if (value.empty() || value == "0") {
        return 0;
    }

    int threads = 0;
    const auto* first = value.data();
    const auto* last = value.data() + value.size();
    const auto parse = std::from_chars(first, last, threads);
    if (parse.ec != std::errc{} || parse.ptr != last || threads < 0) {
        return std::unexpected("ALIGNX_HTS_THREADS must be a non-negative integer");
    }
    if (threads > std::numeric_limits<int>::max()) {
        return std::unexpected("ALIGNX_HTS_THREADS is too large");
    }
    return threads;
}

} // namespace

struct BamReader::Impl {
    std::filesystem::path path;
    samFile* file = nullptr;
    bam_hdr_t* header = nullptr;
    hts_idx_t* index = nullptr;
    hts_itr_t* iter = nullptr;
    bam1_t* record = nullptr;
    kstring_t sam_line{0, 0, nullptr};

    ~Impl() {
        destroy_iter(iter);
        std::free(sam_line.s);
        if (record != nullptr) {
            bam_destroy1(record);
        }
        if (index != nullptr) {
            hts_idx_destroy(index);
        }
        if (header != nullptr) {
            bam_hdr_destroy(header);
        }
        if (file != nullptr) {
            sam_close(file);
        }
    }
};

#else

struct BamReader::Impl {};

#endif

BamReader::BamReader() : impl_(std::make_unique<Impl>()) {}

BamReader::~BamReader() = default;

BamReader::BamReader(BamReader&&) noexcept = default;

BamReader& BamReader::operator=(BamReader&&) noexcept = default;

std::expected<BamReader, std::string> BamReader::open(const std::filesystem::path& path,
                                                      std::optional<int> hts_threads) {
    return open_impl(path, nullptr, hts_threads);
}

std::expected<BamReader, std::string> BamReader::open_profiled(const std::filesystem::path& path,
                                                               BamOpenProfile& profile,
                                                               std::optional<int> hts_threads) {
    return open_impl(path, &profile, hts_threads);
}

std::expected<BamReader, std::string> BamReader::open_impl(const std::filesystem::path& path,
                                                           BamOpenProfile* profile,
                                                           std::optional<int> hts_threads) {
#ifdef ALIGNX_HAVE_HTSLIB
    BamReader reader;
    reader.impl_->path = path;

    const auto path_string = path.string();
    const auto open_start = profile != nullptr ? std::chrono::steady_clock::now()
                                               : std::chrono::steady_clock::time_point{};
    reader.impl_->file = sam_open(path_string.c_str(), "r");
    if (profile != nullptr) {
        profile->open_time += std::chrono::steady_clock::now() - open_start;
    }
    if (reader.impl_->file == nullptr) {
        return std::unexpected("failed to open BAM file: " + path_string);
    }

    auto threads = configured_hts_threads(hts_threads);
    if (!threads) {
        return std::unexpected(threads.error());
    }
    if (*threads > 0 && hts_set_threads(reader.impl_->file, *threads) != 0) {
        return std::unexpected("failed to configure HTSlib threads");
    }

    const auto header_start = profile != nullptr ? std::chrono::steady_clock::now()
                                                 : std::chrono::steady_clock::time_point{};
    reader.impl_->header = sam_hdr_read(reader.impl_->file);
    if (profile != nullptr) {
        profile->header_time += std::chrono::steady_clock::now() - header_start;
    }
    if (reader.impl_->header == nullptr) {
        return std::unexpected("failed to read BAM header: " + path_string);
    }

    reader.impl_->record = bam_init1();
    if (reader.impl_->record == nullptr) {
        return std::unexpected("failed to allocate BAM record");
    }

    const auto index_start = profile != nullptr ? std::chrono::steady_clock::now()
                                                : std::chrono::steady_clock::time_point{};
    reader.impl_->index = sam_index_load(reader.impl_->file, path_string.c_str());
    if (profile != nullptr) {
        profile->index_time += std::chrono::steady_clock::now() - index_start;
    }
    return reader;
#else
    (void)path;
    (void)profile;
    return std::unexpected("alignx was built without HTSlib support");
#endif
}

std::expected<void, std::string> BamReader::fetch(std::string_view region) {
    return fetch_impl(region, nullptr);
}

std::expected<void, std::string>
BamReader::fetch_profiled(std::string_view region,
                          std::chrono::steady_clock::duration& fetch_time) {
    return fetch_impl(region, &fetch_time);
}

std::expected<void, std::string>
BamReader::fetch_impl(std::string_view region, std::chrono::steady_clock::duration* fetch_time) {
#ifdef ALIGNX_HAVE_HTSLIB
    if (impl_->index == nullptr) {
        return std::unexpected("BAM index is not available for: " + impl_->path.string());
    }

    destroy_iter(impl_->iter);
    impl_->iter = nullptr;

    const auto fetch_start = fetch_time != nullptr ? std::chrono::steady_clock::now()
                                                   : std::chrono::steady_clock::time_point{};
    const std::string region_string(region);
    impl_->iter = sam_itr_querys(impl_->index, impl_->header, region_string.c_str());
    if (fetch_time != nullptr) {
        *fetch_time += std::chrono::steady_clock::now() - fetch_start;
    }
    if (impl_->iter == nullptr) {
        return std::unexpected("failed to query BAM region: " + region_string);
    }

    return {};
#else
    (void)region;
    (void)fetch_time;
    return std::unexpected("alignx was built without HTSlib support");
#endif
}

std::expected<std::optional<BamRecord>, std::string> BamReader::next_record() {
#ifdef ALIGNX_HAVE_HTSLIB
    auto result = read_next_raw_record(impl_->file, impl_->header, impl_->iter, impl_->record);
    if (!result) {
        return std::unexpected(result.error());
    }
    if (*result) {
        return to_record(impl_->header, impl_->record);
    }
    return std::optional<BamRecord>{};
#else
    return std::unexpected("alignx was built without HTSlib support");
#endif
}

std::expected<std::optional<std::string>, std::string> BamReader::next_sam_line() {
    auto line = next_sam_line_view();
    if (!line) {
        return std::unexpected(line.error());
    }
    if (!line->has_value()) {
        return std::optional<std::string>{};
    }

    return std::string(line->value());
}

std::expected<std::optional<std::string_view>, std::string> BamReader::next_sam_line_view() {
    return next_sam_line_view_impl(nullptr);
}

std::expected<std::optional<std::string_view>, std::string>
BamReader::next_sam_line_view_profiled(SamLineProfile& profile) {
    return next_sam_line_view_impl(&profile);
}

std::expected<std::optional<std::string_view>, std::string>
BamReader::next_sam_line_view_impl(SamLineProfile* profile) {
#ifdef ALIGNX_HAVE_HTSLIB
    const auto read_start = profile != nullptr ? std::chrono::steady_clock::now()
                                               : std::chrono::steady_clock::time_point{};
    auto result = read_next_raw_record(impl_->file, impl_->header, impl_->iter, impl_->record);
    if (profile != nullptr) {
        profile->read_time += std::chrono::steady_clock::now() - read_start;
    }
    if (!result) {
        return std::unexpected(result.error());
    }
    if (!*result) {
        return std::optional<std::string_view>{};
    }

    impl_->sam_line.l = 0;
    const auto format_start = profile != nullptr ? std::chrono::steady_clock::now()
                                                 : std::chrono::steady_clock::time_point{};
    if (sam_format1(impl_->header, impl_->record, &impl_->sam_line) < 0) {
        return std::unexpected("failed to format BAM record as SAM");
    }
    if (profile != nullptr) {
        profile->format_time += std::chrono::steady_clock::now() - format_start;
    }

    return std::string_view(impl_->sam_line.s, impl_->sam_line.l);
#else
    (void)profile;
    return std::unexpected("alignx was built without HTSlib support");
#endif
}

std::int32_t BamReader::reference_count() const noexcept {
#ifdef ALIGNX_HAVE_HTSLIB
    return impl_->header != nullptr ? impl_->header->n_targets : 0;
#else
    return 0;
#endif
}

bool BamReader::has_index() const noexcept {
#ifdef ALIGNX_HAVE_HTSLIB
    return impl_->index != nullptr;
#else
    return false;
#endif
}

} // namespace alignx::io
