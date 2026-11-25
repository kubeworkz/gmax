#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include "core/atpg/logic5.h"
#include "core/db/constraints.h"
#include "core/db/flat.h"
#include "core/sim/fault.h"

namespace gmax::atpg {

enum class PodemStatus {
    Detected,
    Untestable,
    Aborted,
};

std::string_view podem_status_name(PodemStatus status);

struct PodemBudget {
    // A budget is per fault. A failed branch is a backtrack; max_backtracks is
    // the number of failed branches allowed before the search reports ABORTED.
    std::size_t max_backtracks = 1000;
    // Zero disables the wall-clock limit; otherwise the budget is in
    // milliseconds.
    std::uint64_t max_time_ms = 1000;
};

struct PodemResult {
    PodemStatus status = PodemStatus::Aborted;
    // On Detected, one five-valued entry per net in the final successful
    // assignment. The fault site is D or DBar; unassigned PIs remain X.
    std::vector<Logic5> values;
    std::size_t backtracks = 0;
    std::size_t decisions = 0;
};

// Runs PODEM for one stuck-at fault. `observables` are the primary outputs or
// scan-cell data inputs at which a defect may be detected. The fault site is
// activated with force_fault(); the search then justifies the fault and finds
// an X-path to an observable. `constraints` are resolved case-analysis values,
// applied as hard assignments before activation (ADR-012 section 4); the CLI
// layer is intentionally not involved.
PodemResult run_podem(const db::FlatNetlist& netlist, const sim::Fault& fault,
                      std::span<const db::NetId> observables, PodemBudget budget = {},
                      std::span<const db::CaseAssignment> constraints = {});

}  // namespace gmax::atpg
