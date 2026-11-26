#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "core/db/constraints.h"
#include "core/dft/capture.h"
#include "core/frontend/diagnostics.h"

namespace gmax::dft {

// ADR-013 section 1: the profile declares the procedure, and the SDC decides
// which clocks exist, their groups and their waveforms. TBD-06 has not frozen
// the profile file, so this reads a small JSON declaration of the `capture`
// object instead of the eventual YAML profile; the model it fills is the model a
// frozen profile will fill, so only the reader changes when TBD-06 lands.
//
//   {
//     "capture": {
//       "kind": "grouped",
//       "cycles": [
//         {"groups": [
//           {"name": "fast", "clocks": ["clk_a"], "edge": 0},
//           {"clocks": ["clk_b"], "edge": 20}
//         ]}
//       ]
//     }
//   }
//
// `kind` is `simultaneous`, `grouped` or `sequential`. `cycles` is ordered and
// each cycle's `groups` is ordered, because order is procedure. `name` is an
// optional diagnostic label and is not part of the procedure identity. `edge` is
// optional per group, but
// a cycle gives it for every group or for none: when it gives none, the
// deterministic default layout of section 3 applies. Unknown keys are ignored so
// that a newer profile does not fail an older reader.
//
// A clock the constraints do not declare is a parse error rather than a silent
// zeroed timing set, because the profile may only name clocks the SDC creates.
// A clock repeated across the procedure is accepted here and refused later:
// section 4 condition 4 is a run-level refusal over a well-formed procedure.

struct CaptureProfileResult {
    // False when any error diagnostic was raised. `procedure` is unspecified
    // then and must not be used.
    bool ok = false;
    // False when the document carries no capture object, or carries null: the
    // engine derives the procedure instead (section 1).
    bool declared = false;
    CaptureProcedure procedure;
    std::vector<frontend::Diagnostic> diagnostics;
};

// Parses one profile document against the constraints the SDC produced. `source`
// names the file in diagnostics and defaults to a placeholder for tests.
CaptureProfileResult read_capture_profile(std::string_view text, const db::Constraints& constraints,
                                          std::string_view source = "<capture-profile>");

}  // namespace gmax::dft
