#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "core/db/flat.h"
#include "core/db/status.h"

namespace gmax::sim {

// Four-state logic value. Z is preserved for a pin that is high-impedance; a
// gate input treats it as X, and no primitive produces Z (architecture 6.4).
enum class Logic4 : std::uint8_t { Zero = 0, One = 1, X = 2, Z = 3 };

inline constexpr bool logic4_known(Logic4 value) {
    return value == Logic4::Zero || value == Logic4::One;
}

inline constexpr Logic4 logic4_normalized(Logic4 value) {
    return value == Logic4::Z ? Logic4::X : value;
}

std::string_view logic4_name(Logic4 value);

// 64 lanes in the two-plane encoding from architecture 6.4. `value` is only
// meaningful where `known` is set; every operation returns that canonical form.
struct LogicWord {
    static constexpr std::size_t kLanes = 64;

    std::uint64_t known = 0;
    std::uint64_t value = 0;

    static LogicWord splat(Logic4 value);
    Logic4 lane(std::size_t index) const;
    void set_lane(std::size_t index, Logic4 value);

    friend bool operator==(const LogicWord&, const LogicWord&) = default;
};

// Evaluates one primitive from its fanins in FlatNetlist order. MUX2 uses
// {then, else, select}; an unknown select yields X even when both data inputs
// agree. A malformed fanin count returns X. DFF, DLAT, SCAN_CELL, PI and
// BLACKBOX are not combinational, so they also return X here.
Logic4 eval_gate(db::GateType type, std::span<const Logic4> fanins);
LogicWord eval_gate(db::GateType type, std::span<const LogicWord> fanins);

// Evaluates every combinational gate in level order. `values` is indexed by
// NetId and must have net_count() entries. The caller seeds primary inputs and
// the outputs of state-holding gates; evaluate() leaves those nets untouched,
// forces BLACKBOX outputs to X, and writes every other gate output.
//
// BadPayload reports an inconsistent netlist or value array. CycleDetected
// reports a combinational fanin whose level is not before its consumer.
db::Status evaluate(const db::FlatNetlist& netlist, std::span<Logic4> values);
db::Status evaluate(const db::FlatNetlist& netlist, std::span<LogicWord> values);

}  // namespace gmax::sim
