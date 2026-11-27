#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "core/atpg/podem.h"
#include "core/atpg/sat.h"
#include "core/db/constraints.h"
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

// ADR-013 section 6: one group of one capture cycle, as the manifest lists it.
struct CaptureGroupReport {
    std::vector<std::string> clocks;  // bytewise sorted, as the payload writes them
    double edge = 0.0;
};

struct CaptureCycleReport {
    std::vector<CaptureGroupReport> groups;  // in edge order, as section 1 orders them
};

// ADR-013 section 6's `capture` object. `absent` is the run's word for no capture
// claim: no constraints section, or no capture clock, so no procedure ran. Otherwise
// a procedure ran and the remaining fields describe it. A refused run writes no
// report at all, so a refusal never appears here.
struct CaptureReport {
    bool absent = true;
    // The capture kind's name: `simultaneous`, `grouped` or `sequential`. Empty when
    // absent, because a procedure with no cycle claims no capture and carries no kind.
    std::string procedure;
    bool derived = false;
    std::uint32_t hash = 0;
    // The canonical procedure body. The coverage report carries the object without
    // it; the manifest writes it, one entry per cycle.
    std::vector<CaptureCycleReport> cycles;
};

struct CoverageReport {
    std::string design;
    std::uint32_t netlist_hash = 0;
    std::uint32_t fault_list_hash = 0;
    // ADR-012: constraints are reported as their own identity and status, never
    // folded into the netlist or fault-list hashes. The enforced set is a list of
    // construct names; it stays empty until the engines apply resolved values, so
    // a report cannot claim enforcement that did not happen.
    db::ConstraintsStatus constraints_status = db::ConstraintsStatus::Absent;
    std::uint32_t constraints_hash = 0;
    std::vector<std::string> enforced;
    CaptureReport capture;
    atpg::PodemBudget budget;
    CoverageSummary summary;
    std::vector<FaultOutcome> faults;
};

// The construct names the run applied, in a fixed order so two runs over one input
// report byte-identical coverage. `case_analysis` is what PODEM and the SAT miter
// consume; a `capture` object that is not absent means a procedure ran and applied
// the clock-group model (ADR-013 sections 1 and 6). False paths stay out: ADR-012
// section 4 retains them rather than applying them, and the engine that uses them
// adds the name here when it does.
std::vector<std::string> enforced_constructs(bool case_analysis, const CaptureReport& capture);

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
    // ADR-012: constraints are an input to the run, with their own hash and the
    // provenance path of the SDC that produced them.
    std::string constraints_path;
    db::ConstraintsStatus constraints_status = db::ConstraintsStatus::Absent;
    std::uint32_t constraints_hash = 0;
    // The same object as the coverage report's, plus the procedure body.
    CaptureReport capture;
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
