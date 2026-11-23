#pragma once

#include <vector>

#include "core/db/flat.h"
#include "core/sim/fault.h"

namespace gmax::atpg {

// WP-006 checkpoint sites (architecture 9.1): every driven net except a tie
// output. A primary input and a non-tied gate output each contribute one site;
// a primary output is the net that drives it, so it adds no second site.
// Undriven nets have no controllable fault and are skipped. The result is
// ordered by net id.
std::vector<db::NetId> stuck_at_fault_sites(const db::FlatNetlist& netlist);

// Expands every site to SA0 then SA1. The order is deterministic: ascending
// net id, then SA0 before SA1 (architecture 9.1). Structural equivalence
// collapsing and the .faults writer build on this list.
std::vector<sim::Fault> stuck_at_faults(const db::FlatNetlist& netlist);

}  // namespace gmax::atpg
