#pragma once

#include <cstdint>
#include <string_view>

namespace gmax::db {

// Every gate and net has a stable 32-bit id (architecture 6.1). Ids are assigned
// by the flattened traversal, never derived from addresses or hash order, so
// fault lists and checkpoints stay comparable across runs.
using GateId = std::uint32_t;
using NetId = std::uint32_t;
using StringId = std::uint32_t;
using SourceId = std::uint32_t;

inline constexpr std::uint32_t kInvalidId = 0xFFFFFFFFu;

// Architecture 6.3 gives a net one of two drivers: a gate id, or this sentinel
// when a primary input feeds the net directly. v0's flattener uses it for the top
// module's unbound inputs, which is why PIs do not consume gate ids.
inline constexpr std::uint32_t kPrimaryInput = 0xFFFFFFFEu;

// The primitive set ATPG reasons over (architecture 6.2). Liberty cells are
// decomposed into these by WP-003 and later.
enum class GateType : std::uint8_t {
    And2 = 0,
    Or2,
    Nand2,
    Nor2,
    Xor2,
    Xnor2,
    Not,
    Buf,
    Mux2,
    Tie0,
    Tie1,
    Dff,
    Dlat,
    Pi,
    Po,
    Blackbox,
    ScanCell,
};

// A DFF samples on one clock edge. The clock net names the pin; this says which
// transition captures, so a falling-edge cell needs no inverter gate.
enum class ClockEdge : std::uint8_t { Rising = 0, Falling = 1 };

inline std::string_view clock_edge_name(ClockEdge edge) {
    switch (edge) {
    case ClockEdge::Rising:
        return "rising";
    case ClockEdge::Falling:
        return "falling";
    }
    return "unknown";
}

// Per-gate flags, one bit each, as laid out in architecture 6.3.
inline constexpr std::uint8_t kFlagScan = 1u << 0;
inline constexpr std::uint8_t kFlagBlackbox = 1u << 1;
inline constexpr std::uint8_t kFlagXSource = 1u << 2;
inline constexpr std::uint8_t kFlagInserted = 1u << 3;

inline std::string_view gate_type_name(GateType type) {
    switch (type) {
    case GateType::And2:
        return "AND2";
    case GateType::Or2:
        return "OR2";
    case GateType::Nand2:
        return "NAND2";
    case GateType::Nor2:
        return "NOR2";
    case GateType::Xor2:
        return "XOR2";
    case GateType::Xnor2:
        return "XNOR2";
    case GateType::Not:
        return "NOT";
    case GateType::Buf:
        return "BUF";
    case GateType::Mux2:
        return "MUX2";
    case GateType::Tie0:
        return "TIE0";
    case GateType::Tie1:
        return "TIE1";
    case GateType::Dff:
        return "DFF";
    case GateType::Dlat:
        return "DLAT";
    case GateType::Pi:
        return "PI";
    case GateType::Po:
        return "PO";
    case GateType::Blackbox:
        return "BLACKBOX";
    case GateType::ScanCell:
        return "SCAN_CELL";
    }
    return "UNKNOWN";
}

}  // namespace gmax::db
