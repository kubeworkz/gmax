#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace gmax::db {

// CRC-32/ISO-HDLC, the zlib and ethernet polynomial. Used per section in .dfdb
// (architecture 6.6) to catch truncation and bit rot before a section is parsed.
std::uint32_t crc32(const void* data, std::size_t size);
std::uint32_t crc32(std::string_view text);

}  // namespace gmax::db
