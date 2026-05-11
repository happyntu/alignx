#pragma once

#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace alignx::io {

struct BamRecord {
    std::string qname;
    std::string reference_name;
    std::int32_t position = -1;     // 0-based, -1 for unmapped records.
    std::int32_t end_position = -1; // 0-based half-open, -1 for unmapped records.
    std::uint16_t flag = 0;
    std::uint8_t mapq = 0;

    [[nodiscard]] bool is_unmapped() const noexcept;
};

class BamReader {
public:
    BamReader();
    ~BamReader();

    BamReader(const BamReader&) = delete;
    BamReader& operator=(const BamReader&) = delete;
    BamReader(BamReader&&) noexcept;
    BamReader& operator=(BamReader&&) noexcept;

    [[nodiscard]] static std::expected<BamReader, std::string>
    open(const std::filesystem::path& path);

    [[nodiscard]] std::expected<void, std::string> fetch(std::string_view region);
    [[nodiscard]] std::expected<std::optional<BamRecord>, std::string> next_record();
    [[nodiscard]] std::expected<std::optional<std::string>, std::string> next_sam_line();

    [[nodiscard]] std::int32_t reference_count() const noexcept;
    [[nodiscard]] bool has_index() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace alignx::io
