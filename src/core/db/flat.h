#pragma once

#include <cstdint>
#include <vector>

#include "core/db/ids.h"
#include "core/db/string_table.h"

namespace gmax::db {

// The flattened view ATPG traverses (architecture 6.3). Arrays are parallel:
// index g is a gate, index n is a net, and the *begin arrays are G + 1 offsets
// so a gate's slice is [begin[g], begin[g + 1]).
struct FlatNetlist {
    // Per net's source location; kept here so a snapshot carries its own file
    // and line table (architecture 6.6).
    struct SourceLocation {
        StringId file = kInvalidId;
        std::uint32_t line = 0;

        friend bool operator==(const SourceLocation&, const SourceLocation&) = default;
    };

    // Per gate.
    std::vector<GateType> type;
    std::vector<std::uint32_t> fanin_begin;
    std::vector<NetId> fanins;
    std::vector<std::uint32_t> fanout_begin;
    std::vector<NetId> fanouts;
    std::vector<NetId> out_net;
    std::vector<std::uint8_t> flags;
    std::vector<std::uint32_t> level;
    std::vector<std::uint32_t> lib_cell;

    // Per gate: the net that asynchronously clears (forces 0) or presets (forces
    // 1) the output, or kInvalidId when the gate has no such control. The net is
    // also present in the gate's fanin slice; this array is what tells ATPG the
    // control's role. Both controls are active high by the time they reach here.
    std::vector<NetId> async_clear;
    std::vector<NetId> async_preset;

    // Per net.
    std::vector<std::uint32_t> driver;  // gate id, kPrimaryInput or kInvalidId
    std::vector<std::uint32_t> name_id;
    std::vector<SourceId> source_loc;

    std::vector<SourceLocation> source_table;

    std::uint32_t gate_count() const { return static_cast<std::uint32_t>(type.size()); }
    std::uint32_t net_count() const { return static_cast<std::uint32_t>(driver.size()); }

    friend bool operator==(const FlatNetlist&, const FlatNetlist&) = default;
};

}  // namespace gmax::db
