#pragma once

#include <cstdint>
#include <span>
#include <string_view>

#include "core/db/ids.h"

namespace gmax::atpg {

// PODEM's five values (architecture 9.3). D is 1 in the good circuit and 0 in
// the faulty circuit; DBar is the opposite. X is unknown in both circuits.
enum class Logic5 : std::uint8_t {
    Zero = 0,
    One,
    X,
    D,
    DBar,
};

// D and DBar are known defect values, not unknowns. This predicate is about
// information, not whether the good and faulty circuits agree.
inline constexpr bool logic5_known(const Logic5 value) { return value != Logic5::X; }

// True only for the ordinary Boolean values. Controlling-value counts use this
// classification; D and DBar require direct gate evaluation.
inline constexpr bool logic5_binary(const Logic5 value) {
    return value == Logic5::Zero || value == Logic5::One;
}

inline constexpr bool logic5_defect(const Logic5 value) {
    return value == Logic5::D || value == Logic5::DBar;
}

// The good-circuit value: 0 for 0/DBar, 1 for 1/D, and -1 for X.
int logic5_good(Logic5 value);

Logic5 logic5_invert(Logic5 value);
std::string_view logic5_name(Logic5 value);

// Evaluates one primitive from its fanins. Invalid arity returns X. Sequential
// and source gates return X here; the implication engine seeds their outputs.
Logic5 eval_gate(db::GateType type, std::span<const Logic5> fanins);

}  // namespace gmax::atpg
