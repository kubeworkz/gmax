#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "core/db/constraints.h"
#include "core/db/design.h"
#include "core/db/flat.h"
#include "core/db/status.h"
#include "core/db/string_table.h"

namespace gmax::db::dfdb {

// Schema versions of the container (ADR-004, architecture 19.6). A tool release
// declares the versions it reads and writes; this one writes 1.6 when a design
// carries constraints and 1.3 when it does not, so an unconstrained database is
// byte-identical to what the previous release wrote.
//
// The additive minor sections are async (DFF/DLAT clear and preset, 1.1), enable
// (DFF hold, 1.2), clock edge (DFF capture edge, 1.3), constraints (the canonical
// SDC model, 1.4), false-path clock provenance (which endpoints the SDC wrote as
// [get_clocks], 1.5) and the false path's line (1.6), so older files still load:
// absent provenance means every endpoint was written as an object, and a 1.5 path
// has no line. Each addition gets its own minor even when it lands in the same
// release, because a minor that has been written by a release cannot grow a field
// afterwards: a reader built for the longer record would misread the shorter one.
inline constexpr std::uint16_t kSchemaMajor = 1;
inline constexpr std::uint16_t kSchemaMinor = 6;
// What a database without a constraints section is written as (ADR-012: "may
// continue to use the 1.3 container layout").
inline constexpr std::uint16_t kSchemaMinorWithoutConstraints = 3;

// Container layout (architecture 6.6): header (magic, major/minor version,
// endianness marker, section count), a section table of offset/size/CRC32, then
// the sections, written in ascending section id. Every integer is little-endian
// and written byte by byte, so neither host endianness nor struct packing can
// leak into the file.
//
// `constraints` is null for a database with no attached constraints; the file is
// then written as 1.3 and holds no constraints section. `minor` is the constraint
// layout to write and defaults to this build's; a test passes an older one to produce
// a file the reader must still accept.
std::vector<std::uint8_t> serialize(const Design& design, const FlatNetlist& flat,
                                    const StringTable& strings,
                                    const Constraints* constraints = nullptr,
                                    std::uint16_t minor = kSchemaMinor);

// Schema policy (ADR-004): the major version must match exactly; a file written
// by a newer minor is refused rather than read as if it were this one; an older
// minor is accepted, because minor changes are additive. When `constraints` is
// supplied it is always filled; `present` is false when the file has no
// constraints section.
Status parse(std::span<const std::uint8_t> bytes, Design& design, FlatNetlist& flat,
             StringTable& strings, Constraints* constraints = nullptr);

}  // namespace gmax::db::dfdb
