#include "io/bam_writer.hpp"

#include <string>
#include <utility>

#ifdef ALIGNX_HAVE_HTSLIB
#include <htslib/sam.h>
#endif

namespace alignx::io {

#ifdef ALIGNX_HAVE_HTSLIB

struct BamWriter::Impl {
    std::filesystem::path path;
    samFile* file = nullptr;
    bam_hdr_t* header = nullptr;
    bam1_t* record = nullptr;
    kstring_t parse_buf{0, 0, nullptr};

    ~Impl() {
        std::free(parse_buf.s);
        if (record != nullptr) {
            bam_destroy1(record);
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

struct BamWriter::Impl {};

#endif

BamWriter::BamWriter() : impl_(std::make_unique<Impl>()) {}

BamWriter::~BamWriter() = default;

BamWriter::BamWriter(BamWriter&&) noexcept = default;

BamWriter& BamWriter::operator=(BamWriter&&) noexcept = default;

std::expected<BamWriter, std::string>
BamWriter::open(const std::filesystem::path& path,
                const std::vector<format::Axf1Reference>& references,
                std::optional<int> hts_threads) {
#ifdef ALIGNX_HAVE_HTSLIB
    BamWriter writer;
    writer.impl_->path = path;

    writer.impl_->file = sam_open(path.string().c_str(), "wb");
    if (writer.impl_->file == nullptr) {
        return std::unexpected("failed to open BAM output: " + path.string());
    }

    if (hts_threads.has_value() && *hts_threads > 0) {
        hts_set_threads(writer.impl_->file, *hts_threads);
    }

    writer.impl_->header = sam_hdr_init();
    if (writer.impl_->header == nullptr) {
        return std::unexpected("failed to create BAM header");
    }

    if (sam_hdr_add_line(writer.impl_->header, "HD", "VN", "1.6", "SO", "coordinate",
                         nullptr) != 0) {
        return std::unexpected("failed to add HD header line");
    }

    for (const auto& ref : references) {
        const auto length_str = std::to_string(ref.length);
        if (sam_hdr_add_line(writer.impl_->header, "SQ", "SN", ref.name.c_str(), "LN",
                             length_str.c_str(), nullptr) != 0) {
            return std::unexpected("failed to add SQ line for " + ref.name);
        }
    }

    if (sam_hdr_write(writer.impl_->file, writer.impl_->header) != 0) {
        return std::unexpected("failed to write BAM header");
    }

    writer.impl_->record = bam_init1();
    if (writer.impl_->record == nullptr) {
        return std::unexpected("failed to allocate BAM record buffer");
    }

    return writer;
#else
    (void)path;
    (void)references;
    (void)hts_threads;
    return std::unexpected("alignx was built without HTSlib; BAM export is not available");
#endif
}

std::expected<void, std::string> BamWriter::write_sam_line(std::string_view sam_line) {
#ifdef ALIGNX_HAVE_HTSLIB
    auto& buf = impl_->parse_buf;
    buf.l = 0;
    if (kputsn(sam_line.data(), sam_line.size(), &buf) < 0) {
        return std::unexpected("failed to copy SAM line to parse buffer");
    }

    if (sam_parse1(&buf, impl_->header, impl_->record) < 0) {
        return std::unexpected("failed to parse SAM line");
    }

    if (sam_write1(impl_->file, impl_->header, impl_->record) < 0) {
        return std::unexpected("failed to write BAM record");
    }

    return {};
#else
    (void)sam_line;
    return std::unexpected("alignx was built without HTSlib; BAM export is not available");
#endif
}

std::expected<void, std::string> BamWriter::close() {
#ifdef ALIGNX_HAVE_HTSLIB
    if (impl_->file != nullptr) {
        const int result = sam_close(impl_->file);
        impl_->file = nullptr;
        if (result != 0) {
            return std::unexpected("failed to finalize BAM output: " + impl_->path.string());
        }
    }
    return {};
#else
    return std::unexpected("alignx was built without HTSlib; BAM export is not available");
#endif
}

} // namespace alignx::io
