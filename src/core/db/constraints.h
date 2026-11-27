#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "core/db/flat.h"
#include "core/db/ids.h"
#include "core/db/string_table.h"

namespace gmax::db {

// The constraint set as the design database stores it (ADR-012 section 2). The
// frontend parses SDC text into its own model; this is what survives into a
// .dfdb, what the constraints hash is computed over, and what the reports
// describe. Keeping it in db/ is deliberate: the database owns the persisted
// form, and the frontend converts into it, not the other way round.

enum class ConstraintsStatus : std::uint8_t {
    // No constraints section. Existing databases and unconstrained runs.
    Absent,
    // Every input the reader saw was representable.
    Complete,
    // The reader saw commands or object queries it could not represent, or a
    // supported constraint that the current engine cannot enforce.
    Incomplete,
};

std::string_view constraints_status_name(ConstraintsStatus status);

enum class ClockGroupKind : std::uint8_t {
    Asynchronous,
    LogicallyExclusive,
    PhysicallyExclusive,
};

std::string_view clock_group_kind_name(ClockGroupKind kind);

struct ConstraintClock {
    std::string name;
    double period = 0.0;
    bool has_waveform = false;
    double rise = 0.0;
    double fall = 0.0;
    // The port or pin the clock is defined on; for a generated clock, the
    // target pin, with source_pin keeping create_generated_clock's -source.
    std::string source;
    bool generated = false;
    std::string master_clock;
    std::string source_pin;
    int divide_by = 0;
    int multiply_by = 0;

    friend bool operator==(const ConstraintClock&, const ConstraintClock&) = default;
};

// One set_clock_groups command; each inner vector is one -group.
struct ConstraintClockGroup {
    ClockGroupKind kind = ClockGroupKind::Asynchronous;
    std::string name;
    std::vector<std::vector<std::string>> groups;

    friend bool operator==(const ConstraintClockGroup&, const ConstraintClockGroup&) = default;
};

struct ConstraintFalsePath {
    std::vector<std::string> from;
    std::vector<std::string> through;
    std::vector<std::string> to;
    // The endpoints the SDC wrote as [get_clocks], which is how the reader classified
    // them; `from`/`through`/`to` keep every name. The distinction matters because a
    // false path between two pulsed clocks is exactly the capture edge DFT019 reports,
    // while an object endpoint can excuse a crossing (ADR-013 section 5). Provenance
    // stays out of the canonical constraint payload, so attaching it never moves
    // `constraints_hash` (section 6), but it survives the database so a run can tell
    // `-from [get_clocks clk_a]` from `-from [get_ports clk_a]` when the clock took
    // the port's name.
    std::vector<std::string> from_clocks;
    std::vector<std::string> through_clocks;
    std::vector<std::string> to_clocks;
    // The SDC line the path was written on, so a finding can name it. Provenance like
    // the clock endpoints: stored with the path, absent from the canonical payload,
    // because moving a comment must not move `constraints_hash`.
    std::uint32_t line = 0;

    friend bool operator==(const ConstraintFalsePath&, const ConstraintFalsePath&) = default;
};

struct ConstraintCaseAnalysis {
    std::string object;
    bool value = false;

    friend bool operator==(const ConstraintCaseAnalysis&, const ConstraintCaseAnalysis&) = default;
};

enum class DelayKind : std::uint8_t { Input, Output };

struct ConstraintDelay {
    DelayKind kind = DelayKind::Input;
    std::string object;
    std::string clock;
    double value = 0.0;
    bool max = false;
    bool min = false;

    friend bool operator==(const ConstraintDelay&, const ConstraintDelay&) = default;
};

struct ConstraintDrivingCell {
    std::string object;
    std::string lib_cell;
    std::string pin;

    friend bool operator==(const ConstraintDrivingCell&, const ConstraintDrivingCell&) = default;
};

struct ConstraintLoad {
    std::string object;
    double value = 0.0;

    friend bool operator==(const ConstraintLoad&, const ConstraintLoad&) = default;
};

struct Constraints {
    // False when the database carries no constraints section. This is the
    // `absent` status and is not the same as an attached, empty, complete set.
    bool present = false;
    // False when any part of the input could not be represented or an attached
    // case value cannot be enforced by this engine.
    bool complete = true;

    std::vector<ConstraintClock> clocks;
    std::vector<ConstraintClockGroup> clock_groups;
    std::vector<ConstraintFalsePath> false_paths;
    std::vector<ConstraintCaseAnalysis> case_analysis;
    std::vector<ConstraintDelay> delays;
    std::vector<ConstraintDrivingCell> driving_cells;
    std::vector<ConstraintLoad> loads;

    // Names (bare command keywords, option tokens, or object-query names) of the
    // constructs that made the set incomplete, sorted and deduplicated. ADR-012
    // section 2: diagnostic message text and line numbers are not part of it.
    std::vector<std::string> unsupported;

    friend bool operator==(const Constraints&, const Constraints&) = default;
};

ConstraintsStatus constraints_status(const Constraints& constraints);

// The canonical payload the hash is computed over: fixed top-level field order,
// the ordering rules of ADR-012 section 2, and one canonical decimal encoding
// independent of source formatting. The source path, raw bytes, comments,
// whitespace and line numbers are excluded by construction.
std::string canonical_constraints_payload(const Constraints& constraints);

// CRC32 of the canonical payload (ADR-012 section 2), printed by the reports as
// eight hex digits, matching netlist_hash and fault_list_hash.
std::uint32_t constraints_hash(const Constraints& constraints);

// Shortest round-trip decimal for a double (`std::to_chars`), with negative zero
// normalized to "0". This is the numeric encoding the hash contract fixes.
std::string canonical_decimal(double value);

// One case-analysis value after resolution: the assignment PODEM applies before
// it activates the fault (ADR-012 section 4). Only nets that resolve to a
// top-level input port are ever represented here; the attachment step validates
// that and marks the set incomplete rather than dropping the target silently.
struct CaseAssignment {
    NetId net = kInvalidId;
    bool value = false;
};

// True when a constraint object name resolves to a net a case value could be
// applied to: a top-level input port, which includes clock and reset inputs
// (ADR-012 section 4, decision 1). A set containing any other target is stored
// but reported incomplete rather than having that target dropped silently. The
// attachment step uses this to decide completeness; applying the values is a
// later slice.
bool enforceable_case_target(std::string_view name, const FlatNetlist& flat,
                             const StringTable& strings);

// True when a clock's source object resolves to a top-level input port. Clocks
// are not applied to fault outcomes yet (ADR-012 section 4), so a clock whose
// source is elsewhere is still recorded and hashed; the attachment warns about
// it instead of refusing the set.
bool enforceable_clock_source(std::string_view name, const FlatNetlist& flat,
                              const StringTable& strings);

// The clock-domain view of a design: state-holding gates grouped by the clock
// net they sample. `domains` counts the distinct clock nets that a declared
// clock claims and that clock at least one such gate; `flops` counts the gates
// in those domains. A domain no clock declares is not counted, so the number
// describes what the SDC actually binds rather than what the netlist contains.
struct ClockDomains {
    std::size_t domains = 0;
    std::size_t flops = 0;

    friend bool operator==(const ClockDomains&, const ClockDomains&) = default;
};

ClockDomains clock_domains(const Constraints& constraints, const FlatNetlist& flat,
                           const StringTable& strings);

// One declared clock and how many state-holding gates sample its net. The names
// are sorted and unique; two names on one net appear separately and report the
// same gates, because they are one domain with two names. A clock whose source
// resolves to no net is left out, as in `clock_domains`.
//
// `gates` is every state-holding element that samples the net, which is the count
// section 4's condition 5 asks about. A capture cycle can pulse only an
// edge-triggered element, so `captured` counts the DFFs and scan cells and leaves
// lockup latches out, and `scan_cells` is the subset of those the netlist marks as
// scan cells -- the numerator section 4's condition 2 needs to decide whether a
// domain is fully scanned.
struct ClockGateCount {
    std::string clock;
    std::size_t gates = 0;
    std::size_t captured = 0;
    std::size_t scan_cells = 0;

    friend bool operator==(const ClockGateCount&, const ClockGateCount&) = default;
};

std::vector<ClockGateCount> clock_gate_counts(const Constraints& constraints,
                                              const FlatNetlist& flat, const StringTable& strings);

}  // namespace gmax::db
