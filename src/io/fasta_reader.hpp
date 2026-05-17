#pragma once

#include <array>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace alignx::io {

struct FastaContig {
    std::string name;
    std::uint32_t length = 0;
};

class FastaReader {
public:
    ~FastaReader();

    FastaReader(const FastaReader&) = delete;
    FastaReader& operator=(const FastaReader&) = delete;
    FastaReader(FastaReader&&) noexcept;
    FastaReader& operator=(FastaReader&&) noexcept;

    [[nodiscard]] static std::expected<FastaReader, std::string>
    open(const std::filesystem::path& path);

    [[nodiscard]] std::expected<std::vector<FastaContig>, std::string> contigs() const;

    [[nodiscard]] std::expected<std::string, std::string>
    fetch_sequence(const std::string& contig, std::int32_t start, std::int32_t end) const;

    [[nodiscard]] std::expected<std::string, std::string>
    fetch_contig(const std::string& contig) const;

    [[nodiscard]] std::expected<std::array<unsigned char, 32>, std::string>
    compute_contig_sha256(const std::string& contig) const;

private:
    FastaReader() = default;
    struct Impl;
    Impl* impl_ = nullptr;
};

} // namespace alignx::io
