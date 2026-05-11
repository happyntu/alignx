#include "index/bam_index_projection.hpp"

#include <limits>

namespace alignx::index {
namespace {

constexpr std::int32_t kBaiMinShift = 14;
constexpr std::int32_t kBaiDepth = 5;

std::expected<std::uint64_t, std::string> bin_level_offset(std::int32_t level) {
    if (level < 0) {
        return std::unexpected("negative BAM index bin level");
    }

    const auto shift = static_cast<unsigned int>(level) * 3U;
    if (shift >= std::numeric_limits<std::uint64_t>::digits) {
        return std::unexpected("BAM index bin hierarchy exceeds supported range");
    }
    return ((std::uint64_t{1} << shift) - 1U) / 7U;
}

std::expected<std::uint64_t, std::string> metadata_bin_id(std::int32_t depth) {
    auto terminal_offset = bin_level_offset(depth + 1);
    if (!terminal_offset) {
        return std::unexpected(terminal_offset.error());
    }
    return *terminal_offset + 1U;
}

std::expected<std::uint32_t, std::string> checked_reference_count(std::size_t reference_count) {
    if (reference_count > std::numeric_limits<std::uint32_t>::max()) {
        return std::unexpected("too many references for AXFIndex v1");
    }
    return static_cast<std::uint32_t>(reference_count);
}

std::expected<void, std::string> add_projected_chunk(AxfIndex& axf, std::uint32_t ref_id,
                                                     std::uint32_t bin_id,
                                                     std::uint64_t chunk_begin,
                                                     std::uint64_t chunk_end,
                                                     std::int32_t min_shift, std::int32_t depth) {
    if (chunk_begin >= chunk_end) {
        return std::unexpected("BAM index chunk requires begin < end");
    }

    auto interval = bin_to_genomic_interval(bin_id, min_shift, depth);
    if (!interval) {
        return std::unexpected(interval.error());
    }

    axf.add_interval(ref_id, AxfInterval{.start = interval->start,
                                         .end = interval->end,
                                         .chunk_offset = chunk_begin,
                                         .column_index_offset = chunk_end});
    return {};
}

} // namespace

std::expected<BinGenomicInterval, std::string>
bin_to_genomic_interval(std::uint32_t bin_id, std::int32_t min_shift, std::int32_t depth) {
    if (min_shift < 0) {
        return std::unexpected("negative BAM index min_shift");
    }
    if (depth < 0) {
        return std::unexpected("negative BAM index depth");
    }
    if (min_shift >= static_cast<std::int32_t>(std::numeric_limits<std::uint64_t>::digits)) {
        return std::unexpected("BAM index bin span exceeds supported range");
    }

    const auto terminal_offset = bin_level_offset(depth + 1);
    if (!terminal_offset) {
        return std::unexpected(terminal_offset.error());
    }
    if (bin_id >= *terminal_offset) {
        return std::unexpected("BAM index bin id is outside the regular bin hierarchy");
    }

    for (std::int32_t level = 0; level <= depth; ++level) {
        auto level_offset = bin_level_offset(level);
        auto next_level_offset = bin_level_offset(level + 1);
        if (!level_offset || !next_level_offset) {
            return std::unexpected("BAM index bin hierarchy exceeds supported range");
        }

        if (bin_id < *level_offset || bin_id >= *next_level_offset) {
            continue;
        }

        const auto span_shift = static_cast<unsigned int>(min_shift + ((depth - level) * 3));
        if (span_shift >= std::numeric_limits<std::uint64_t>::digits) {
            return std::unexpected("BAM index bin span exceeds supported range");
        }

        const std::uint64_t span = std::uint64_t{1} << span_shift;
        const std::uint64_t bin_index = static_cast<std::uint64_t>(bin_id) - *level_offset;
        const std::uint64_t start = bin_index * span;
        const std::uint64_t end = start + span;
        if (end > std::numeric_limits<std::uint32_t>::max()) {
            return std::unexpected("BAM index bin interval exceeds AXFIndex v1 coordinate range");
        }

        return BinGenomicInterval{.start = static_cast<std::uint32_t>(start),
                                  .end = static_cast<std::uint32_t>(end)};
    }

    return std::unexpected("BAM index bin id is outside the regular bin hierarchy");
}

std::expected<AxfIndex, std::string> project_bai_to_axf_index(const BaiIndex& bai) {
    auto reference_count = checked_reference_count(bai.reference_count());
    if (!reference_count) {
        return std::unexpected(reference_count.error());
    }

    auto metadata_bin = metadata_bin_id(kBaiDepth);
    if (!metadata_bin) {
        return std::unexpected(metadata_bin.error());
    }

    AxfIndex axf(*reference_count);
    for (std::uint32_t ref_id = 0; ref_id < *reference_count; ++ref_id) {
        const auto& reference = bai.references.at(ref_id);
        for (const BaiBin& bin : reference.bins) {
            if (bin.id == *metadata_bin) {
                continue;
            }
            for (const BaiChunk& chunk : bin.chunks) {
                auto add = add_projected_chunk(axf, ref_id, bin.id, chunk.begin, chunk.end,
                                               kBaiMinShift, kBaiDepth);
                if (!add) {
                    return std::unexpected(add.error());
                }
            }
        }
    }

    auto validation = axf.sort_and_validate();
    if (!validation) {
        return std::unexpected(validation.error());
    }
    return axf;
}

std::expected<AxfIndex, std::string> project_csi_to_axf_index(const CsiIndex& csi) {
    auto reference_count = checked_reference_count(csi.reference_count());
    if (!reference_count) {
        return std::unexpected(reference_count.error());
    }

    auto metadata_bin = metadata_bin_id(csi.depth);
    if (!metadata_bin) {
        return std::unexpected(metadata_bin.error());
    }

    AxfIndex axf(*reference_count);
    for (std::uint32_t ref_id = 0; ref_id < *reference_count; ++ref_id) {
        const auto& reference = csi.references.at(ref_id);
        for (const CsiBin& bin : reference.bins) {
            if (bin.id == *metadata_bin) {
                continue;
            }
            for (const CsiChunk& chunk : bin.chunks) {
                auto add = add_projected_chunk(axf, ref_id, bin.id, chunk.begin, chunk.end,
                                               csi.min_shift, csi.depth);
                if (!add) {
                    return std::unexpected(add.error());
                }
            }
        }
    }

    auto validation = axf.sort_and_validate();
    if (!validation) {
        return std::unexpected(validation.error());
    }
    return axf;
}

} // namespace alignx::index
