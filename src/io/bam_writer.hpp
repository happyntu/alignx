#pragma once

#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "format/axf1_file.hpp"

namespace alignx::io {

class BamWriter {
public:
    BamWriter();
    ~BamWriter();

    BamWriter(const BamWriter&) = delete;
    BamWriter& operator=(const BamWriter&) = delete;
    BamWriter(BamWriter&&) noexcept;
    BamWriter& operator=(BamWriter&&) noexcept;

    [[nodiscard]] static std::expected<BamWriter, std::string>
    open(const std::filesystem::path& path,
         const std::vector<format::Axf1Reference>& references,
         std::optional<int> hts_threads = std::nullopt);

    [[nodiscard]] std::expected<void, std::string> write_sam_line(std::string_view sam_line);

    [[nodiscard]] std::expected<void, std::string> close();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace alignx::io
