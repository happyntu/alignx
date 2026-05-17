#include "io/fasta_reader.hpp"

#include "io/sha256.hpp"

#include <cstdlib>
#include <utility>

#ifdef ALIGNX_HAVE_HTSLIB
#include <htslib/faidx.h>
#endif

namespace alignx::io {

struct FastaReader::Impl {
#ifdef ALIGNX_HAVE_HTSLIB
    faidx_t* fai = nullptr;
#endif
};

FastaReader::~FastaReader() {
    if (impl_) {
#ifdef ALIGNX_HAVE_HTSLIB
        if (impl_->fai) {
            fai_destroy(impl_->fai);
        }
#endif
        delete impl_;
    }
}

FastaReader::FastaReader(FastaReader&& other) noexcept : impl_(other.impl_) {
    other.impl_ = nullptr;
}

FastaReader& FastaReader::operator=(FastaReader&& other) noexcept {
    if (this != &other) {
        if (impl_) {
#ifdef ALIGNX_HAVE_HTSLIB
            if (impl_->fai) {
                fai_destroy(impl_->fai);
            }
#endif
            delete impl_;
        }
        impl_ = other.impl_;
        other.impl_ = nullptr;
    }
    return *this;
}

std::expected<FastaReader, std::string>
FastaReader::open(const std::filesystem::path& path) {
#ifdef ALIGNX_HAVE_HTSLIB
    faidx_t* fai = fai_load(path.string().c_str());
    if (!fai) {
        return std::unexpected("failed to open FASTA index for: " + path.string() +
                               " (ensure .fai index exists)");
    }
    FastaReader reader;
    reader.impl_ = new Impl{.fai = fai};
    return reader;
#else
    (void)path;
    return std::unexpected("FASTA reading requires HTSlib support");
#endif
}

std::expected<std::vector<FastaContig>, std::string> FastaReader::contigs() const {
#ifdef ALIGNX_HAVE_HTSLIB
    const int n = faidx_nseq(impl_->fai);
    std::vector<FastaContig> result;
    result.reserve(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) {
        const char* name = faidx_iseq(impl_->fai, i);
        const int len = faidx_seq_len(impl_->fai, name);
        result.push_back({.name = name, .length = static_cast<std::uint32_t>(len)});
    }
    return result;
#else
    return std::unexpected("FASTA reading requires HTSlib support");
#endif
}

std::expected<std::string, std::string>
FastaReader::fetch_sequence(const std::string& contig, std::int32_t start,
                            std::int32_t end) const {
#ifdef ALIGNX_HAVE_HTSLIB
    int len = 0;
    // faidx_fetch_seq uses 0-based, inclusive [start, end]
    char* seq = faidx_fetch_seq(impl_->fai, contig.c_str(), start, end - 1, &len);
    if (!seq) {
        return std::unexpected("failed to fetch sequence for " + contig + ":" +
                               std::to_string(start) + "-" + std::to_string(end));
    }
    std::string result(seq, static_cast<std::size_t>(len));
    free(seq);
    return result;
#else
    (void)contig;
    (void)start;
    (void)end;
    return std::unexpected("FASTA reading requires HTSlib support");
#endif
}

std::expected<std::string, std::string>
FastaReader::fetch_contig(const std::string& contig) const {
#ifdef ALIGNX_HAVE_HTSLIB
    const int len = faidx_seq_len(impl_->fai, contig.c_str());
    if (len < 0) {
        return std::unexpected("contig not found in FASTA index: " + contig);
    }
    return fetch_sequence(contig, 0, len);
#else
    (void)contig;
    return std::unexpected("FASTA reading requires HTSlib support");
#endif
}

std::expected<std::array<unsigned char, 32>, std::string>
FastaReader::compute_contig_sha256(const std::string& contig) const {
    auto seq = fetch_contig(contig);
    if (!seq) {
        return std::unexpected(seq.error());
    }
    // Uppercase for deterministic hashing (HTSlib may return lowercase)
    for (char& c : *seq) {
        if (c >= 'a' && c <= 'z') {
            c = static_cast<char>(c - 32);
        }
    }
    return sha256(*seq);
}

} // namespace alignx::io
