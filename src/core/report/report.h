#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "core/atpg/podem.h"
#include "core/atpg/sat.h"
#include "core/db/status.h"
#include "core/sim/fault.h"

namespace gmax::report {

// One collapsed fault and the PODEM outcome that produced the report. The
// net name is read from the design's string table by the CLI so the report can
// be audited without loading the database again.
struct FaultOutcome {
    sim::Fault fault;
    std::string net_name;
    atpg::PodemStatus status = atpg::PodemStatus::Aborted;
    std::size_t decisions = 0;
    std::size_t backtracks = 0;
    // "podem" for a structural result, "sat" when a PODEM budget abort was
    // resolved by the complete SAT miter.
    std::string method = "podem";
};

struct CoverageSummary {
    // Collapsed-fault counts, one per status. These are the counts the ATPG
    // search reports directly.
    std::size_t collapsed = 0;
    std::size_t detected = 0;
    std::size_t untestable = 0;
    std::size_t aborted = 0;

    // Raw-fault counts, obtained by mapping every raw fault to the outcome of
    // its collapsed representative.
    std::size_t raw = 0;
    std::size_t raw_detected = 0;
    std::size_t raw_untestable = 0;
    std::size_t raw_aborted = 0;

    std::size_t decisions = 0;
    std::size_t backtracks = 0;
};

struct CoverageReport {
    std::string design;
    std::uint32_t netlist_hash = 0;
    std::uint32_t fault_list_hash = 0;
    atpg::PodemBudget budget;
    CoverageSummary summary;
    std::vector<FaultOutcome> faults;
};

// Stable, compact JSON. It is deliberately built by hand here rather than by a
// general JSON writer: the shape is tiny, and keeping field order fixed makes
// repeated runs byte-identical.
std::string coverage_json(const CoverageReport& report);

struct RunManifest {
    std::string tool_version;
    std::string design;
    std::string design_path;
    std::string faults_path;
    std::string coverage_path;
    std::uint32_t netlist_hash = 0;
    std::uint32_t fault_list_hash = 0;
    std::uint32_t coverage_hash = 0;
    std::uint32_t gates = 0;
    std::uint32_t nets = 0;
    atpg::PodemBudget budget;
    // ADR-011: the worker count actually used. The determinism test compares
    // manifests with this field excluded, so 1-thread and N-thread runs differ
    // only here.
    std::uint32_t threads = 1;
    CoverageSummary summary;
};

std::string manifest_json(const RunManifest& manifest);

struct SatSmokeCase {
    sim::Fault fault;
    std::string net_name;
    std::string expected;  // "sat" or "unsat"
    atpg::SatFaultStatus status = atpg::SatFaultStatus::Unknown;
    std::size_t conflicts = 0;
    std::size_t variables = 0;
    std::size_t clauses = 0;
    bool solver_timed_out = false;
};

struct SatSmokeReport {
    std::string design;
    std::uint32_t netlist_hash = 0;
    std::uint32_t fault_list_hash = 0;
    std::vector<SatSmokeCase> cases;
};

// Stable smoke evidence for G0.2. The independent-solver cross-check is added
// by the process test after it runs the same cases through Yosys SAT.
std::string sat_smoke_json(const SatSmokeReport& report);

// Writes a complete report. The caller owns directory creation so it can turn
// a bad output path into a design/import error before any ATPG work is done.
db::Status write_text(const std::string& path, std::string_view text);

std::string hex32(std::uint32_t value);

}  // namespace gmax::report
