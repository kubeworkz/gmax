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
// deterministic default layout of section 3 applies. `intermediate` is optional
// on every cycle but the last, where it is a refusal: it is either one state for
// the whole cycle (`"intermediate": "masked"`) or the exact mapping from captured
// clock to state (`{"clk_a": "masked"}`), which names every clock the cycle
// captures and no other. `scanned` and `masked` are the only states (section 2),
// and a cycle without the key keeps the `scanned` default, so a profile that does
// not mention intermediate state reads as fully scanned. Unknown keys are
// ignored so that a newer profile does not fail an older reader.
//
// A clock the constraints do not declare is a parse error rather than a silent
// zeroed timing set, because the profile may only name clocks the SDC creates.
// A clock repeated across the procedure is accepted here and refused later:
// section 4 condition 4 is a run-level refusal over a well-formed procedure.
//
// The same split applies to a group that holds clocks the SDC keeps apart, which
// JSON can express and this reader therefore accepts: the document is well formed,
// and whether the capture it describes is legal is condition 1's question, answered
// over every group of the procedure whatever its shape. Reading it as a syntax
// error would put one verdict in two places and would refuse at load a document
// whose only problem section 4 already names precisely.

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
