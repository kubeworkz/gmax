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

struct SdcFalsePath {
    std::vector<std::string> from;
    std::vector<std::string> through;
    std::vector<std::string> to;
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
