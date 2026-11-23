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
// v0 limits, all reported as errors rather than silently dropped: pins are
// scalar, clocked_on/enable must name one pin, and an ff clear or preset is
// rejected because the flat graph has no per-fanin roles to say which input is
// an asynchronous control.
db::Status decompose_cell(const Cell& cell, db::Module& module);

// Decomposes every cell and appends the modules to `design`.
db::Status decompose_library(const Library& library, db::Design& design);

}  // namespace gmax::frontend
