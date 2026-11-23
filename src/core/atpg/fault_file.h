#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "core/atpg/fault_list.h"
#include "core/db/db.h"
#include "core/db/status.h"
#include "core/sim/fault.h"

namespace gmax::atpg {

struct FaultListHeader {
    // CRC32 of the canonical .dfdb serialization of the design database.
    std::uint32_t netlist_hash = 0;
    // CRC32 of the canonical count and fault-entry payload.
    std::uint32_t fault_list_hash = 0;
    std::uint32_t raw_count = 0;
    std::uint32_t collapsed_count = 0;

    friend bool operator==(const FaultListHeader&, const FaultListHeader&) = default;
};

struct FaultListFile {
    FaultListHeader header;
    std::vector<sim::Fault> raw;
    std::vector<sim::Fault> collapsed;
    std::vector<std::uint32_t> raw_to_collapsed;

    friend bool operator==(const FaultListFile&, const FaultListFile&) = default;
};

// Netlist identity used by the .faults header. Hashing the canonical .dfdb
// bytes keeps the hash independent of insertion order and host layout, and
// makes the fault list usable only with the exact netlist it was built for.
std::uint32_t netlist_hash(const db::DesignDb& database);

// Writes the ordered raw fault list and its collapsed representatives:
//
//   gmax-faults 1
//   netlist <8 hex>
//   raw-count <N>
//   collapsed-count <M>
//   fault-list <8 hex>
//   R <net> <0|1> <collapsed-id>     (N lines)
//   C <net> <0|1>                    (M lines)
//
// The fault-list hash covers the two count lines and every R/C line, so a
// changed count or entry is detected before the body is trusted.
db::Status write_faults(const std::string& path, const db::DesignDb& database,
                        std::span<const sim::Fault> raw, const CollapsedFaultList& collapsed);

// Reads and validates a .faults file. A file written for another netlist is
// rejected with BadPayload; a payload that does not match its header hash is
// rejected with CrcMismatch.
db::Status read_faults(const std::string& path, const db::DesignDb& database, FaultListFile& out);

}  // namespace gmax::atpg
