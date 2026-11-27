#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "core/db/constraints.h"
#include "core/db/flat.h"
#include "core/db/string_table.h"
#include "core/dft/capture.h"

namespace gmax::dft {

// ADR-013 section 2's masked state, applied to the observations a run may use.
//
// A masked domain's intermediate value becomes X at the earliest legal point, and
// the earliest point the grading model has is the observation: every observation
// the domain's state elements reach through combinational logic is forced X, so it
// can neither detect nor justify a fault. A state-holding element stops the walk,
// because the model reads each one as a free value -- PODEM blocks propagation
// through it and the SAT miter leaves it unconstrained -- so nothing beyond it
// depends on the masked value inside one frame.
struct CaptureMask {
    // The domains a non-final cycle captures and declares masked, bytewise sorted
    // and unique. Empty when no cycle masks anything.
    std::vector<std::string> domains;
    // `observables` without the nets the mask forces to X, in the caller's order.
    // Equal to `observables` when `domains` is empty.
    std::vector<db::NetId> observables;
};

// The mask the procedure applies to `observables`: its masked domains and the
// observation points that stay usable. A mask that reaches no observation leaves
// the list unchanged, so a caller can tell a mask that changed nothing from one
// that was never declared.
CaptureMask apply_capture_mask(const CaptureProcedure& procedure,
                               const db::Constraints& constraints, const db::FlatNetlist& flat,
                               const db::StringTable& strings,
                               std::span<const db::NetId> observables);

}  // namespace gmax::dft
