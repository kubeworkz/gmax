#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/db/constraints.h"

namespace gmax::dft {

// ADR-013: the capture procedure model. A procedure is an ordered list of
// cycles, a cycle pulses one or more groups, and each group contributes one
// timing set. This header is the model only. The profile that declares a
// procedure, the refusals of section 4 and the report fields of section 6 land
// in later slices and are not represented here.

enum class CaptureKind : std::uint8_t {
    Simultaneous,
    Grouped,
    Sequential,
    // Not a capture kind: the marker an empty procedure carries. A design whose
    // SDC declares no capture clock has no cycle to describe, and section 6 calls
    // that `absent` rather than naming a shape, so the model says so instead of
    // borrowing a kind whose definition the procedure does not meet. No profile may
    // declare it and no procedure with a cycle carries it.
    None,
};

std::string_view capture_kind_name(CaptureKind kind);

// ADR-013 section 2: in a sequential procedure a domain captured before the
// last cycle holds a value later cycles read, and that value is usable only if
// the domain is fully scanned or explicitly masked. The state belongs to the
// domain the cycle captures, so a non-final cycle states the domains it pulses;
// a domain it leaves unpulsed is not intermediate there. `Scanned` is the default
// and a claim the engine verifies; `Masked` makes every observation depending on
// the domain an X, so a masked domain has to be recorded explicitly.
enum class CaptureIntermediateState : std::uint8_t {
    Scanned,
    Masked,
};

std::string_view capture_intermediate_state_name(CaptureIntermediateState state);

// One group's timing set (ADR-013 section 3): the clocks it pulses, the period
// and waveform they share, and the group's edge time inside its cycle. `clocks`
// is sorted bytewise, so SDC order never reaches the hash.
struct CaptureTimingSet {
    std::vector<std::string> clocks;
    double period = 0.0;
    bool has_waveform = false;
    double rise = 0.0;
    double fall = 0.0;
    // Edge time inside the cycle, in the unit of `period`. Zero for the first
    // group; later groups follow the deterministic default of section 3 until a
    // profile supplies explicit edges.
    double edge = 0.0;

    friend bool operator==(const CaptureTimingSet&, const CaptureTimingSet&) = default;
};

struct CaptureGroup {
    // A diagnostic label for the group, empty when the profile gives none and
    // never set on an engine-derived group. It is provenance, not procedure:
    // section 6 hashes the clocks and their times, so two procedures that differ
    // only in this label share one capture hash.
    std::string name;
    CaptureTimingSet timing;

    friend bool operator==(const CaptureGroup&, const CaptureGroup&) = default;
};

struct CaptureCycle {
    std::vector<CaptureGroup> groups;
    // Per-domain intermediate state (section 2), meaningful only on a non-final
    // cycle and keyed by the domains that cycle captures, which are the domains a
    // later cycle reads. Empty means the default, `Scanned`; a masked domain must
    // be recorded. Entry order is not identity: the payload writes the domains
    // sorted by clock name.
    std::vector<std::pair<std::string, CaptureIntermediateState>> intermediate;

    friend bool operator==(const CaptureCycle&, const CaptureCycle&) = default;
};

struct CaptureProcedure {
    CaptureKind kind = CaptureKind::Sequential;
    // True when the engine derived the procedure instead of a profile declaring
    // it. Provenance, not procedure: excluded from the canonical payload, so two
    // otherwise identical procedures hash the same either way.
    bool derived = true;
    std::vector<CaptureCycle> cycles;

    friend bool operator==(const CaptureProcedure&, const CaptureProcedure&) = default;
};

// The canonical payload the capture hash is computed over (ADR-013 section 6):
// cycles in order, groups in edge order, clocks sorted bytewise within a group,
// each non-final cycle's captured domains and their intermediate state, and one
// decimal encoding. Source paths, comments, whitespace, line and column numbers,
// database IDs, the `derived` flag and a group's diagnostic label are excluded
// by construction.
std::string canonical_capture_payload(const CaptureProcedure& procedure);

// CRC32 of the canonical payload, printed as eight hex digits like the other
// hashes in the reports. `constraints_hash` is untouched by design: a procedure
// is how the constraints are used, not a constraint.
std::uint32_t capture_hash(const CaptureProcedure& procedure);

// The default derived procedure (ADR-013 section 1): one single-clock cycle per
// declared clock, ordered by clock name, with the kind read back off those cycles.
// Two or more cycles are sequential, so a design with a single clock comes back
// simultaneous rather than as a sequential procedure without a second cycle. A
// clock with no declared group is its own group, so undeclared clocks are never
// simultaneous. Empty when the constraints declare no clock, and an empty procedure
// carries `CaptureKind::None` because it makes no capture claim at all. Declared
// rows of set_clock_groups do not enter the derived order; they shape a declared
// procedure.
CaptureProcedure resolve_capture_procedure(const db::Constraints& constraints);

// The timing set for one group, taking period and waveform from the bytewise
// smallest declared clock and the edge time from `edge`. A group naming no
// declared clock keeps zeroed timing rather than inventing a period.
CaptureTimingSet capture_timing_set(const db::Constraints& constraints,
                                    const std::vector<std::string>& clocks, double edge);

// True when every clock in the group is declared and all of them share one
// period and waveform. ADR-013 section 4 condition 3 refuses a group where this
// is false; `capture_timing_set` still returns the representative values so the
// finding can name the group it refused. An empty group agrees with itself.
bool capture_group_timing_agrees(const db::Constraints& constraints,
                                 const std::vector<std::string>& clocks);

// ADR-013 section 3's default edge layout: the first group at time zero and each
// later group one slowest-period step after the previous one, so no two groups
// of a cycle share an edge. Used when a profile supplies no edge times; all
// edges stay zero when no group has a period.
void assign_capture_edges(std::vector<CaptureGroup>& groups);

// ADR-013 section 3's clock-level exclusion: a declared clock that clocks no
// state-holding gate is left out of the procedure. `clock_gates` is the
// declared-clock view of the design (db::clock_gate_counts), and `dropped`
// receives the excluded clock names in name order so the caller can report them.
//
// A clock leaves every timing set that names it and the set is recomputed from
// the clocks that remain; a group left with no clock is removed, and a cycle left
// with no group is removed with it. An intermediate state whose domain is gone
// goes too, and the cycle that is last after the drop carries none, because no
// final cycle owns an intermediate value.
//
// Section 1 defines the kind by the cycles, so a drop re-derives it whenever the
// cycles change: one cycle with one group is simultaneous, one cycle with several
// groups is grouped, and two or more cycles are sequential. A procedure that loses
// nothing keeps the kind it arrived with, which both builders already derived from
// its cycles, so a drop that leaves one cycle cannot keep describing a sequential
// procedure.
//
// The returned procedure is the one the run applies, so the capture identity
// describes what is actually pulsed.
CaptureProcedure exclude_idle_clocks(const CaptureProcedure& procedure,
                                     const db::Constraints& constraints,
                                     const std::vector<db::ClockGateCount>& clock_gates,
                                     std::vector<std::string>& dropped);

}  // namespace gmax::dft
