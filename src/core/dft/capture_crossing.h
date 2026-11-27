
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/db/constraints.h"
#include "core/db/flat.h"
#include "core/db/string_table.h"
#include "core/dft/capture.h"
#include "core/dft/capture_refusal.h"

namespace gmax::dft {

// ADR-013 section 5's DFT003 for the pairs this tree can decide. A sequential
// procedure captures one clock domain before another, so a path that carries the
// earlier domain's flop output into a flop of the later domain is a clock-boundary
// crossing. It is legal only with a lockup latch on the path, or with a false path
// that is not itself a DFT019 finding: a false path between the two pulsed clocks
// removes exactly the capture edge the procedure creates, so it excuses nothing.
//
// Same-cycle group pairs are the other half of section 5 and are not evaluated here,
// because they need a grouped procedure and no entry point can produce one while the
// profile is unwired.
//
// Findings carry CrossingWithoutLockup, one per (earlier cycle, later cycle, launch
// clock, capture clock) that crosses unexcused, and come back in contract order.
std::vector<CaptureRefusal> find_crossing_refusals(const CaptureProcedure& procedure,
                                                   const db::Constraints& constraints,
                                                   const db::FlatNetlist& flat,
                                                   const db::StringTable& strings);

// ADR-013 section 5's DFT019, and the same finding from the other side: a false path
// whose two endpoints are clocks the procedure captures in different groups or cycles
// removes exactly the capture edge between them. It is a warning, so nothing here
// refuses; the run records the findings and grades. Only endpoints the SDC wrote as
// clocks count, because an object endpoint names no capture edge, and a path naming a
// pulsed clock on one side only is not evaluated.
struct FalsePathWarning {
    std::string from_clock;
    std::string to_clock;
    std::uint32_t line = 0;
};

std::vector<FalsePathWarning> find_false_path_warnings(const CaptureProcedure& procedure,
                                                       const db::Constraints& constraints);

}  // namespace gmax::dft
