#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
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
// net id, then SA0 before SA1 (architecture 9.1).
std::vector<sim::Fault> stuck_at_faults(const db::FlatNetlist& netlist);

struct CollapsedFaultList {
    // One representative per structural equivalence class, ordered by the first
    // raw fault in that class.
    std::vector<sim::Fault> faults;
    // raw_to_collapsed[i] indexes faults for the i-th entry of stuck_at_faults().
    std::vector<std::uint32_t> raw_to_collapsed;

    std::size_t raw_count() const { return raw_to_collapsed.size(); }
    std::size_t collapsed_count() const { return faults.size(); }
};

// Structural equivalence collapsing over stuck_at_faults(netlist). An input of
// a single-input BUF or NOT is equivalent to its output when the input is not
// observable and has exactly one load: every path from the input passes through
// the output, and forcing either side produces the same value there. Inverter
// edges swap the stuck value, and chains compose transitively because the
// classes are joined, not just adjacent pairs.
//
// `observables` are the nets a tester can see (primary outputs today). A net in
// that set is never merged away: forcing it and forcing a buffered copy are
// distinguishable at the observation point itself.
CollapsedFaultList collapse_stuck_at_faults(const db::FlatNetlist& netlist,
                                            std::span<const db::NetId> observables);

}  // namespace gmax::atpg
