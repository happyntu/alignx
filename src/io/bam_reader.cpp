#include "io/bam_reader.hpp"

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

} // namespace

struct BamReader::Impl {
    std::filesystem::path path;
    samFile* file = nullptr;
    bam_hdr_t* header = nullptr;
    hts_idx_t* index = nullptr;
    hts_itr_t* iter = nullptr;
    bam1_t* record = nullptr;

    ~Impl() {
        destroy_iter(iter);
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

std::expected<BamReader, std::string> BamReader::open(const std::filesystem::path& path) {
#ifdef ALIGNX_HAVE_HTSLIB
    BamReader reader;
    reader.impl_->path = path;

    const auto path_string = path.string();
    reader.impl_->file = sam_open(path_string.c_str(), "r");
    if (reader.impl_->file == nullptr) {
        return std::unexpected("failed to open BAM file: " + path_string);
    }

    reader.impl_->header = sam_hdr_read(reader.impl_->file);
    if (reader.impl_->header == nullptr) {
        return std::unexpected("failed to read BAM header: " + path_string);
    }

    reader.impl_->record = bam_init1();
    if (reader.impl_->record == nullptr) {
        return std::unexpected("failed to allocate BAM record");
    }

    reader.impl_->index = sam_index_load(reader.impl_->file, path_string.c_str());
    return reader;
#else
    (void)path;
    return std::unexpected("alignx was built without HTSlib support");
#endif
}

std::expected<void, std::string> BamReader::fetch(std::string_view region) {
#ifdef ALIGNX_HAVE_HTSLIB
    if (impl_->index == nullptr) {
        return std::unexpected("BAM index is not available for: " + impl_->path.string());
    }

    destroy_iter(impl_->iter);
    impl_->iter = nullptr;

    const std::string region_string(region);
    impl_->iter = sam_itr_querys(impl_->index, impl_->header, region_string.c_str());
    if (impl_->iter == nullptr) {
        return std::unexpected("failed to query BAM region: " + region_string);
    }

    return {};
#else
    (void)region;
    return std::unexpected("alignx was built without HTSlib support");
#endif
}

std::expected<std::optional<BamRecord>, std::string> BamReader::next_record() {
#ifdef ALIGNX_HAVE_HTSLIB
    const int result = impl_->iter != nullptr
                           ? sam_itr_next(impl_->file, impl_->iter, impl_->record)
                           : sam_read1(impl_->file, impl_->header, impl_->record);

    if (result >= 0) {
        return to_record(impl_->header, impl_->record);
    }
    if (result == -1) {
        return std::optional<BamRecord>{};
    }
    return std::unexpected("failed while reading BAM record");
#else
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
