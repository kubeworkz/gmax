#pragma once

#include <string>

#include "core/db/design.h"
#include "core/frontend/liberty.h"

namespace gmax::frontend {

// Decomposes one library cell into a module of the architecture 6.2 primitives:
// combinational pins become And2/Or2/Xor2/Not/Mux2/Buf/Tie0/Tie1 cones, ff and
// latch pins become Dff/Dlat with their next-state cone, statetable rows become
// a sum-of-products next-state cone over the primitive set, and an output with
// no function becomes a Blackbox. Every gate carries the cell's index in
// lib_cell, so reporting can still name the original library cell.
//
// An ff or latch clear/preset becomes the gate's async_clear/async_preset net
// and is also appended to its fanins, so reachability and levelization see the
// control. Control polarity is normalized to active high with a Not.
//
// v0 limits, all reported as errors rather than silently dropped: pins are
// scalar, clocked_on/enable must name one pin, and a clear or preset must name
// one pin or its complement.
db::Status decompose_cell(const Cell& cell, db::Module& module);

// Decomposes every cell and appends the modules to `design`.
db::Status decompose_library(const Library& library, db::Design& design);

}  // namespace gmax::frontend
