#pragma once

#include <cstdint>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

#include "core/db/constraints.h"
#include "core/dft/capture.h"

namespace gmax::dft {

// ADR-013 section 4's refusal conditions, in the order the ADR numbers them. The
// code is the contract: a test asserts the kebab token, never the prose after it,
// and the prose may improve without a test change (ADR-012 section 2's rule).
enum class CaptureRefusalCode : std::uint8_t {
    SimultaneousAcrossGroups,
    IntermediateState,
    GroupTimingDisagrees,
    ClockRepeated,
    NoCaptureClock,
    CrossingWithoutLockup,
};

std::string_view capture_refusal_code_name(CaptureRefusalCode code);

// One finding: the condition that fired and the prose naming the clock, group or
// cycle an operator has to fix. `detail` never ends with a period and keeps
// objects in single quotes, like the rest of the CLI.
struct CaptureRefusal {
    CaptureRefusalCode code = CaptureRefusalCode::SimultaneousAcrossGroups;
    std::string detail;
    // Optional fact printed one level under the detail, for the numbers that make
    // the finding actionable (condition 2's scan counts). Empty when there are none.
    std::string fact;

    friend bool operator==(const CaptureRefusal&, const CaptureRefusal&) = default;
};

// Evaluates the section 4 conditions this tree can decide, over a procedure and
// the constraints that produced it. `clock_gates` is the declared-clock view of
// the design (db::clock_gate_counts): every declared clock and how many
// state-holding gates sample it.
//
// Findings come back in contract order: by condition, then by the line, so two
// runs over one input print byte-identical output (ADR-011). An empty result
// means the procedure may be applied.
//
// Condition 2 (`intermediate-state`) is decided here from the per-clock scan
// counts: a domain a non-final cycle captures is usable only when every element
// that captures it is a scan cell, or when the cycle declares it masked. Condition
// 6 (`crossing-without-lockup`) needs the netlist, so it lives in
// capture_crossing.h, which covers both the same-cycle group pairs and the
// sequential pairs section 5 names, with the lockup-latch and false-path excuses.
// Neither pass guesses: condition 2 stays silent without a design view, and
// condition 6 without a crossing.
std::vector<CaptureRefusal>
find_capture_refusals(const CaptureProcedure& procedure, const db::Constraints& constraints,
                      const std::vector<db::ClockGateCount>& clock_gates);

// Sorts findings into contract order (condition, then the line) and drops duplicates.
// A caller that merges findings from more than one pass normalizes them again here,
// so the printed block stays deterministic.
void sort_capture_refusals(std::vector<CaptureRefusal>& refusals);

// Writes the refusal block: the header once, then one `gmax:   <code>: <detail>`
// line per finding. Writes nothing at all when there is nothing to refuse, so a
// caller may invoke it unconditionally.
void write_capture_refusals(std::ostream& out, const std::vector<CaptureRefusal>& refusals);

}  // namespace gmax::dft
