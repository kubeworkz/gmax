#pragma once

#include <string>
#include <vector>

#include "core/db/constraints.h"
#include "core/frontend/diagnostics.h"
#include "core/frontend/sdc.h"

namespace gmax::frontend {

// Names of the constructs the reader could not represent, as ADR-012 section 2
// defines them: the bare command keyword, the option token, or the object-query
// name carried on the diagnostic's `object` field. Diagnostic message text and
// line numbers are deliberately excluded, so rewording a message cannot change a
// constraints hash.
std::vector<std::string> unsupported_construct_names(const std::vector<Diagnostic>& diagnostics);

// Converts the parsed SDC model into the persisted one. Completeness starts from
// the warnings above; the caller downgrades it further when a supported case
// value targets an object this engine cannot enforce (ADR-012 section 4).
db::Constraints to_constraints(const SdcConstraints& sdc,
                               const std::vector<Diagnostic>& diagnostics);

}  // namespace gmax::frontend
