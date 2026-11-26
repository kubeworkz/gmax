#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "core/frontend/diagnostics.h"

namespace gmax::frontend {

// WP-110 keeps the SDC model deliberately smaller than a timing engine. The
// model stores what scan DRC and multi-clock ATPG need: clocks, generated-clock
// relationships, compatible/exclusive clock groups, capture false paths, case
// analysis (the test-mode view), and the timing annotations that are retained
// but not evaluated in v1.

enum class SdcClockGroupKind : std::uint8_t {
    Asynchronous,
    LogicallyExclusive,
    PhysicallyExclusive,
};

std::string_view sdc_clock_group_kind_name(SdcClockGroupKind kind);

struct SdcClock {
    std::string name;
    double period = 0.0;
    bool has_waveform = false;
    double rise = 0.0;
    double fall = 0.0;
    // The port or pin the clock is defined on. For a generated clock this is
    // the target pin; source_pin keeps create_generated_clock's -source value.
    std::string source;
    bool generated = false;
    std::string master_clock;
    std::string source_pin;
    int divide_by = 0;
    int multiply_by = 0;
    std::uint32_t line = 0;
};

// One set_clock_groups command. Each inner vector is one -group; clocks in
// different groups have the command's relationship.
struct SdcClockGroup {
    SdcClockGroupKind kind = SdcClockGroupKind::Asynchronous;
    std::string name;
    std::vector<std::vector<std::string>> groups;
    std::uint32_t line = 0;
};

// ADR-012 section 4: a set_clock_groups member names a clock, so membership is
// checked against the declared clock names without a netlist. A member that
// names no declared clock, and a clock that appears in more than one -group of
// the same command, are reported; the set is kept whole.
struct SdcClockGroupIssues {
    std::vector<std::string> unknown;
    std::vector<std::string> conflicting;
};

// ADR-012 section 4: a generated clock's -master_clock names a declared clock,
// and the -source must lead to that clock. A master that names no clock, a
// master the -source does not lead to, a -source carrying more than one clock
// with no -master_clock, and a master chain that closes on itself are reported;
// the clock keeps its recorded master.
struct SdcGeneratedClockIssues {
    std::vector<std::string> unknown;
    std::vector<std::string> conflicting;
    // Sorted clocks the -source carries when -master_clock is absent; more than
    // one leaves the implicit link ambiguous.
    std::vector<std::string> ambiguous;
    // A -master_clock chain that repeats, rendered as "a -> b -> a".
    std::vector<std::string> cycles;
};

struct SdcFalsePath {
    std::vector<std::string> from;
    std::vector<std::string> through;
    std::vector<std::string> to;
    // Endpoints that came from a [get_clocks ...] query. from/through/to keep
    // the resolved names the constraint stores; these say which of them the
    // file asked to be clocks, because a bare or port/pin/net endpoint cannot
    // be checked without a netlist.
    std::vector<std::string> from_clocks;
    std::vector<std::string> through_clocks;
    std::vector<std::string> to_clocks;
    std::uint32_t line = 0;
};

// set_case_analysis is the SDC representation of test-mode pin values. A
// conflict for one object is a parse error, not a last-writer-wins rule.
struct SdcCaseAnalysis {
    std::string object;
    bool value = false;
    std::uint32_t line = 0;
};

enum class SdcDelayKind : std::uint8_t { Input, Output };

struct SdcDelay {
    SdcDelayKind kind = SdcDelayKind::Input;
    std::string object;
    std::string clock;
    double value = 0.0;
    bool max = false;
    bool min = false;
    std::uint32_t line = 0;
};

struct SdcDrivingCell {
    std::string object;
    std::string lib_cell;
    std::string pin;
    std::uint32_t line = 0;
};

struct SdcLoad {
    std::string object;
    double value = 0.0;
    std::uint32_t line = 0;
};

struct SdcConstraints {
    std::vector<SdcClock> clocks;
    std::vector<SdcClockGroup> clock_groups;
    std::vector<SdcFalsePath> false_paths;
    std::vector<SdcCaseAnalysis> case_analysis;
    std::vector<SdcDelay> delays;
    std::vector<SdcDrivingCell> driving_cells;
    std::vector<SdcLoad> loads;

    const SdcClock* find_clock(std::string_view name) const;
    const SdcCaseAnalysis* find_case(std::string_view object) const;
};

// Sorted and deduplicated: a member is reported once per command however often
// the source repeats it.
SdcClockGroupIssues sdc_clock_group_issues(const SdcConstraints& constraints,
                                           const SdcClockGroup& group);

// Empty for a primary clock. Without -master_clock the -source alone is the
// link, so it must carry exactly one clock.
SdcGeneratedClockIssues sdc_generated_clock_issues(const SdcConstraints& constraints,
                                                   const SdcClock& clock);

struct SdcResult {
    bool ok = false;
    SdcConstraints constraints;
    std::vector<Diagnostic> diagnostics;
};

// Parses the architecture 7.3 subset. Unsupported commands are warnings with
// the command and line; malformed supported commands are errors. The parser
// never executes Tcl, expands variables, or evaluates timing.
SdcResult read_sdc(std::string_view text, std::string_view source = "<sdc>");
SdcResult read_sdc_file(const std::string& path);

}  // namespace gmax::frontend
