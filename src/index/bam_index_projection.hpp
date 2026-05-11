#pragma once

#include <cstdint>
#include <expected>
#include <string>

#include "index/axf_index.hpp"
#include "index/bai_reader.hpp"
#include "index/csi_reader.hpp"

namespace alignx::index {

struct BinGenomicInterval {
    std::uint32_t start = 0;
    std::uint32_t end = 0;
};

[[nodiscard]] std::expected<BinGenomicInterval, std::string>
bin_to_genomic_interval(std::uint32_t bin_id, std::int32_t min_shift, std::int32_t depth);

[[nodiscard]] std::expected<AxfIndex, std::string> project_bai_to_axf_index(const BaiIndex& bai);

[[nodiscard]] std::expected<AxfIndex, std::string> project_csi_to_axf_index(const CsiIndex& csi);

} // namespace alignx::index
