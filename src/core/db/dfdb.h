#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "core/db/design.h"
#include "core/db/flat.h"
#include "core/db/status.h"
#include "core/db/string_table.h"

namespace gmax::db::dfdb {

// Schema versions of the container (ADR-004, architecture 19.6). A tool release
// declares the versions it reads and writes; this one writes 1.3. The additive
// minor sections are async (DFF/DLAT clear and preset, 1.1), enable (DFF hold,
// 1.2) and clock edge (DFF capture edge, 1.3), so older files still load: an
// absent section means the default (no control, or a rising edge).
inline constexpr std::uint16_t kSchemaMajor = 1;
inline constexpr std::uint16_t kSchemaMinor = 3;

// Container layout (architecture 6.6): header (magic, major/minor version,
// endianness marker, section count), a section table of offset/size/CRC32, then
// the sections, written in ascending section id. Every integer is little-endian
// and written byte by byte, so neither host endianness nor struct packing can
// leak into the file.
std::vector<std::uint8_t> serialize(const Design& design, const FlatNetlist& flat,
                                    const StringTable& strings);

// Schema policy (ADR-004): the major version must match exactly; a file written
// by a newer minor is refused rather than read as if it were this one; an older
// minor is accepted, because minor changes are additive.
Status parse(std::span<const std::uint8_t> bytes, Design& design, FlatNetlist& flat,
             StringTable& strings);

}  // namespace gmax::db::dfdb
