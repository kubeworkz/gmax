#pragma once

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

}  // namespace gmax::db
