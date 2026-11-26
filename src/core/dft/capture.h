#pragma once

#include <cstdint>
#include <string>
#include <string_view>
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
};

std::string_view capture_kind_name(CaptureKind kind);

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
    // The set_clock_groups group name, or empty when the engine derived the
    // group from the clocks alone.
    std::string name;
    CaptureTimingSet timing;

    friend bool operator==(const CaptureGroup&, const CaptureGroup&) = default;
};

struct CaptureCycle {
    std::vector<CaptureGroup> groups;

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
// cycles in order, groups in cycle order, clocks sorted bytewise within a group,
// and one decimal encoding. Source paths, comments, whitespace, line and column
// numbers, database IDs and the `derived` flag are excluded by construction.
std::string canonical_capture_payload(const CaptureProcedure& procedure);

// CRC32 of the canonical payload, printed as eight hex digits like the other
// hashes in the reports. `constraints_hash` is untouched by design: a procedure
// is how the constraints are used, not a constraint.
std::uint32_t capture_hash(const CaptureProcedure& procedure);

// The default derived procedure (ADR-013 section 1): sequential, one
// single-clock cycle per declared clock, ordered by clock name. A clock with no
// declared group is its own group, so undeclared clocks are never simultaneous.
// Empty when the constraints declare no clock. Declared rows of set_clock_groups
// do not enter the derived order; they shape a declared procedure.
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

}  // namespace gmax::dft
