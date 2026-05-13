#pragma once

#include <chrono>
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
    std::int32_t template_length = 0;
    std::uint16_t flag = 0;
    std::uint8_t mapq = 0;

    [[nodiscard]] bool is_unmapped() const noexcept;
};

struct SamLineProfile {
    std::chrono::steady_clock::duration read_time{};
    std::chrono::steady_clock::duration format_time{};
};

struct BamOpenProfile {
    std::chrono::steady_clock::duration open_time{};
    std::chrono::steady_clock::duration header_time{};
    std::chrono::steady_clock::duration index_time{};
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
    [[nodiscard]] static std::expected<BamReader, std::string>
    open_profiled(const std::filesystem::path& path, BamOpenProfile& profile);

    [[nodiscard]] std::expected<void, std::string> fetch(std::string_view region);
    [[nodiscard]] std::expected<void, std::string>
    fetch_profiled(std::string_view region, std::chrono::steady_clock::duration& fetch_time);
    [[nodiscard]] std::expected<std::optional<BamRecord>, std::string> next_record();
    [[nodiscard]] std::expected<std::optional<std::string>, std::string> next_sam_line();
    [[nodiscard]] std::expected<std::optional<std::string_view>, std::string> next_sam_line_view();
    [[nodiscard]] std::expected<std::optional<std::string_view>, std::string>
    next_sam_line_view_profiled(SamLineProfile& profile);

    [[nodiscard]] std::int32_t reference_count() const noexcept;
    [[nodiscard]] bool has_index() const noexcept;

private:
    [[nodiscard]] static std::expected<BamReader, std::string>
    open_impl(const std::filesystem::path& path, BamOpenProfile* profile);

    [[nodiscard]] std::expected<void, std::string>
    fetch_impl(std::string_view region, std::chrono::steady_clock::duration* fetch_time);

    [[nodiscard]] std::expected<std::optional<std::string_view>, std::string>
    next_sam_line_view_impl(SamLineProfile* profile);

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace alignx::io
