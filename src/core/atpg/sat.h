#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "core/db/constraints.h"
#include "core/db/flat.h"
#include "core/sim/fault.h"

namespace gmax::atpg {

// The SAT fallback is deliberately separate from PODEM: it is a complete
// Boolean check over a two-copy miter, so an UNSAT result is evidence that the
// fault is redundant, not merely that a structural search gave up.
enum class SatFaultStatus {
    Detected,
    Untestable,
    Unsupported,
    Unknown,
};

// The clock is injectable so tests can force a deterministic timeout without
// sleeping or depending on machine speed. Production callers leave it null.
using SatNow = std::chrono::steady_clock::time_point (*)();

// The SAT boundary deliberately owns only variables, clauses, solve, model
// access and proof metrics. The two-copy miter and fault policy remain in
// atpg/sat.cpp, so a CaDiCaL or MiniSat adapter can be dropped in without
// changing the encoder or the ATPG caller.
class ISatSolver {
  public:
    enum class Result { Sat, Unsat, Unknown };

    virtual ~ISatSolver() = default;

    virtual int new_variable() = 0;
    virtual bool add_clause(std::vector<int> literals) = 0;
    virtual Result solve(std::size_t max_conflicts, std::uint64_t max_time_ms,
                         std::chrono::steady_clock::time_point start, SatNow now) = 0;
    virtual bool model_value(int variable) const = 0;
    virtual bool model_is_consistent() const = 0;
    virtual std::size_t conflicts() const = 0;
    virtual std::size_t variables() const = 0;
    virtual std::size_t clauses() const = 0;
    virtual bool timed_out() const = 0;
};

struct SatFaultResult {
    SatFaultStatus status = SatFaultStatus::Unknown;
    std::size_t conflicts = 0;
    std::size_t variables = 0;
    std::size_t clauses = 0;
    // True when the solver itself stopped at the wall-clock deadline. This is
    // distinct from a build or post-solve timeout and keeps the timeout test
    // from being satisfied by a deadline check outside the search loop.
    bool solver_timed_out = false;
};

// Checks one stuck-at fault against the observables. The netlist must be
// combinational: state-holding and black-box gates make the query unsupported,
// because their values are not implied by the primary inputs. On Detected, the
// model is replayed through the scalar fault simulator before being reported, so
// an encoding mistake cannot silently inflate coverage. `max_time_ms` is the
// caller's remaining per-fault wall-clock budget; zero disables it. A timeout
// returns Unknown rather than an unproven Detected or Untestable result.
// `constraints` are resolved case-analysis values, asserted as unit clauses on
// both miter copies (ADR-012 section 4), so a value that contradicts the defect
// makes the miter UNSAT, which is reported as Untestable.
SatFaultResult check_fault(const db::FlatNetlist& netlist, const sim::Fault& fault,
                           std::span<const db::NetId> observables,
                           std::size_t max_conflicts = 1000000, std::uint64_t max_time_ms = 0,
                           SatNow now = nullptr,
                           std::span<const db::CaseAssignment> constraints = {});

// The deterministic in-tree CDCL implementation used by the P0 smoke path.
// Production adapters for CaDiCaL and MiniSat implement the same interface.
std::unique_ptr<ISatSolver> make_internal_sat_solver();

}  // namespace gmax::atpg
