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
// An ff enable (a vendor extension; standard Liberty puts a clock enable in
// next_state) becomes the DFF's enable net, a hold while low. A latch enable is
// its transparency control and follows the same polarity normalization. An ff
// or latch clear/preset becomes async_clear/async_preset. Each control is also
// appended to the gate's fanins, so reachability and levelization see it, and
// its polarity is normalized to active high with a Not.
//
// v0 limits, all reported as errors rather than silently dropped: pins are
// scalar, clocked_on must name one pin, and an enable, clear or preset must name
// one pin or its complement.
db::Status decompose_cell(const Cell& cell, db::Module& module);

// Decomposes every cell and appends the modules to `design`.
db::Status decompose_library(const Library& library, db::Design& design);

}  // namespace gmax::frontend
