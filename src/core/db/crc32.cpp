#include "core/db/crc32.h"

#include <array>

namespace gmax::db {
namespace {

constexpr std::uint32_t kPolynomial = 0xEDB88320u;  // reflected 0x04C11DB7

constexpr std::array<std::uint32_t, 256> make_table() {
    std::array<std::uint32_t, 256> table{};
    for (std::uint32_t i = 0; i < 256; ++i) {
        std::uint32_t value = i;
        for (int bit = 0; bit < 8; ++bit) {
            value = (value & 1u) != 0 ? (kPolynomial ^ (value >> 1)) : (value >> 1);
        }
        table[i] = value;
    }
    return table;
}

constexpr std::array<std::uint32_t, 256> kTable = make_table();

}  // namespace

std::uint32_t crc32(const void* data, std::size_t size) {
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    std::uint32_t crc = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < size; ++i) {
        crc = kTable[(crc ^ bytes[i]) & 0xFFu] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFu;
}

std::uint32_t crc32(std::string_view text) { return crc32(text.data(), text.size()); }

}  // namespace gmax::db
