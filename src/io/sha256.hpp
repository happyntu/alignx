#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace alignx::io {

std::array<unsigned char, 32> sha256(const unsigned char* data, std::size_t size);

inline std::array<unsigned char, 32> sha256(std::string_view data) {
    return sha256(reinterpret_cast<const unsigned char*>(data.data()), data.size());
}

} // namespace alignx::io
