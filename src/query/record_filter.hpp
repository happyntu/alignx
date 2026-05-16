#pragma once

#include <cstdint>

namespace alignx::query {

struct RecordFilter {
    std::uint16_t flag_exclude = 0;
    std::uint8_t min_mapq = 0;

    [[nodiscard]] bool is_active() const noexcept {
        return flag_exclude != 0 || min_mapq != 0;
    }
};

[[nodiscard]] inline bool passes_filter(const RecordFilter& filter, std::uint16_t flag,
                                        std::uint8_t mapq) {
    if ((flag & filter.flag_exclude) != 0) {
        return false;
    }
    if (mapq < filter.min_mapq) {
        return false;
    }
    return true;
}

} // namespace alignx::query
