#pragma once

#include <chrono>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <iosfwd>
#include <string>

namespace alignx::query {

struct Axf1ViewProfile {
    std::uint64_t chunks_selected = 0;
    std::uint64_t chunks_with_matches = 0;
    std::uint64_t records_scanned = 0;
    std::uint64_t records_matched = 0;
    std::uint64_t records_output = 0;
    std::uint64_t stdout_bytes = 0;
    std::uint64_t selective_bytes_read = 0;
    std::uint64_t full_chunk_bytes_read = 0;
    std::uint64_t selective_payload_bytes = 0;
    std::uint64_t full_payload_bytes = 0;
    std::chrono::steady_clock::duration open_time{};
    std::chrono::steady_clock::duration reference_lookup_time{};
    std::chrono::steady_clock::duration chunk_query_time{};
    std::chrono::steady_clock::duration selective_decode_time{};
    std::chrono::steady_clock::duration filter_time{};
    std::chrono::steady_clock::duration full_decode_time{};
    std::chrono::steady_clock::duration format_time{};
    std::chrono::steady_clock::duration write_time{};
};

[[nodiscard]] std::expected<void, std::string>
write_axf1_region_sam(const std::filesystem::path& input, const std::string& region,
                      std::ostream& out);

[[nodiscard]] std::expected<void, std::string>
write_axf1_region_sam_profiled(const std::filesystem::path& input, const std::string& region,
                               std::ostream& out, Axf1ViewProfile& profile);

} // namespace alignx::query
