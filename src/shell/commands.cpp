#include "shell/commands.h"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <ostream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "core/atpg/fault_file.h"
#include "core/atpg/fault_list.h"
#include "core/atpg/podem.h"
#include "core/atpg/sat.h"
#include "core/db/crc32.h"
#include "core/db/db.h"
#include "core/db/design.h"
#include "core/db/status.h"
#include "core/frontend/diagnostics.h"
#include "core/frontend/yosys.h"
#include "core/report/report.h"
#include "core/sim/fault.h"
#include "core/version.h"
#include "shell/cli.h"

namespace gmax::shell {
namespace {

// The flattened graph does not carry a primary-output flag, so resolve the
// top module's output names through the string table. These are the observation
// points the structural collapse and PODEM search use.
std::vector<db::NetId> primary_outputs(const db::DesignDb& database, std::string& error) {
    const db::Module* top = db::find_module(database.design(), database.design().top);
    if (top == nullptr) {
        error = "top module '" + database.design().top + "' is not defined";
        return {};
    }

    const db::FlatNetlist& flat = database.flat();
    std::vector<db::NetId> outputs;
    outputs.reserve(top->outputs.size());
    for (const std::string& name : top->outputs) {
        db::NetId found = db::kInvalidId;
        for (db::NetId net = 0; net < flat.net_count(); ++net) {
            if (database.strings().at(flat.name_id[net]) == name) {
                found = net;
                break;
            }
        }
        if (found == db::kInvalidId) {
            error = "top output '" + name + "' has no flattened net";
            return {};
        }
        outputs.push_back(found);
    }
    return outputs;
}

void print_diagnostics(const std::vector<frontend::Diagnostic>& diagnostics, std::ostream& err) {
    for (const frontend::Diagnostic& diagnostic : diagnostics) {
        err << "gmax: " << frontend::diagnostic_code_name(diagnostic.code) << ": "
            << diagnostic.message;
        if (!diagnostic.object.empty()) {
            err << " (" << diagnostic.object << ')';
        }
        if (!diagnostic.source.empty()) {
            err << " (" << diagnostic.source.file << ':' << diagnostic.source.line;
            if (diagnostic.source.column != 0) {
                err << '.' << diagnostic.source.column;
            }
            err << ')';
        }
        err << '\n';
    }
    if (diagnostics.empty()) {
        err << "gmax: the Yosys frontend failed without a diagnostic\n";
    }
}

std::string net_name(const db::DesignDb& database, const db::NetId net) {
    const db::FlatNetlist& flat = database.flat();
    if (net >= flat.net_count() || flat.name_id[net] >= database.strings().size()) {
        return {};
    }
    return std::string(database.strings().at(flat.name_id[net]));
}

bool parse_u64(const std::string& text, std::uint64_t& value) {
    if (text.empty()) {
        return false;
    }
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    return error == std::errc{} && end == text.data() + text.size();
}

bool parse_budget(const std::vector<std::string>& positionals, atpg::PodemBudget& budget,
                  std::string& error) {
    if (positionals.size() > 2) {
        std::uint64_t value = 0;
        if (!parse_u64(positionals[2], value)) {
            error = "invalid max-backtracks '" + positionals[2] + "'";
            return false;
        }
        if (value > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
            error = "max-backtracks is too large";
            return false;
        }
        budget.max_backtracks = static_cast<std::size_t>(value);
    }
    if (positionals.size() > 3) {
        std::uint64_t value = 0;
        if (!parse_u64(positionals[3], value)) {
            error = "invalid max-time-ms '" + positionals[3] + "'";
            return false;
        }
        budget.max_time_ms = value;
    }
    return true;
}

bool parse_fault_spec(const std::string& text, sim::Fault& fault, std::string& error) {
    const std::size_t colon = text.rfind(':');
    if (colon == std::string::npos || colon == 0 || colon + 1 == text.size()) {
        error = "invalid fault '" + text + "', expected <net>:<sa0|sa1>";
        return false;
    }
    std::uint64_t net = 0;
    if (!parse_u64(text.substr(0, colon), net) ||
        net > static_cast<std::uint64_t>(db::kInvalidId)) {
        error = "invalid fault net in '" + text + "'";
        return false;
    }
    const std::string stuck = text.substr(colon + 1);
    if (stuck == "0" || stuck == "sa0") {
        fault = {static_cast<db::NetId>(net), sim::StuckValue::Zero};
        return true;
    }
    if (stuck == "1" || stuck == "sa1") {
        fault = {static_cast<db::NetId>(net), sim::StuckValue::One};
        return true;
    }
    error = "invalid fault value in '" + text + "', expected sa0 or sa1";
    return false;
}

ExitCode read_verilog_handler(const std::vector<std::string>& positionals, std::ostream& out,
                              std::ostream& err) {
    const std::string& netlist_path = positionals[0];
    const std::string& design_path = positionals[1];
    const std::string& top = positionals[2];

    frontend::YosysOptions options;
    options.netlist = netlist_path;
    options.top = top;
    const frontend::YosysResult result = frontend::elaborate_with_yosys(options);
    if (!result.ok) {
        print_diagnostics(result.diagnostics, err);
        return ExitCode::Design;
    }

    db::DesignDb database;
    database.design() = result.design;
    const db::Status flattened = database.flatten();
    if (!flattened.ok()) {
        err << "gmax: cannot flatten the Yosys design: " << flattened.message() << '\n';
        return ExitCode::Design;
    }

    const db::Status saved = database.save(design_path);
    if (!saved.ok()) {
        err << "gmax: cannot write design '" << design_path << "': " << saved.message() << '\n';
        return ExitCode::Design;
    }

    out << "wrote '" << design_path << "'\n";
    out << "top: " << database.design().top << '\n';
    out << "gates: " << database.flat().gate_count() << '\n';
    out << "nets: " << database.flat().net_count() << '\n';
    out << "netlist hash: " << report::hex32(atpg::netlist_hash(database)) << '\n';
    return ExitCode::Success;
}

ExitCode write_faults_handler(const std::vector<std::string>& positionals, std::ostream& out,
                              std::ostream& err) {
    const std::string& design_path = positionals[0];
    const std::string& output_path = positionals[1];

    db::DesignDb database;
    const db::Status loaded = db::DesignDb::load(design_path, database);
    if (!loaded.ok()) {
        err << "gmax: cannot load design '" << design_path << "': " << loaded.message() << "\n";
        return ExitCode::Design;
    }

    std::string primary_output_error;
    const std::vector<db::NetId> observables = primary_outputs(database, primary_output_error);
    if (!primary_output_error.empty()) {
        err << "gmax: " << primary_output_error << "\n";
        return ExitCode::Design;
    }

    const db::FlatNetlist& flat = database.flat();
    const std::vector<sim::Fault> raw = atpg::stuck_at_faults(flat);
    const atpg::CollapsedFaultList collapsed = atpg::collapse_stuck_at_faults(flat, observables);
    const db::Status written = atpg::write_faults(output_path, database, raw, collapsed);
    if (!written.ok()) {
        err << "gmax: cannot write faults '" << output_path << "': " << written.message() << "\n";
        return ExitCode::Design;
    }

    out << "wrote '" << output_path << "'\n";
    out << "raw faults: " << raw.size() << "\n";
    out << "collapsed faults: " << collapsed.collapsed_count() << "\n";
    return ExitCode::Success;
}

ExitCode run_atpg_handler(const std::vector<std::string>& positionals, std::ostream& out,
                          std::ostream& err) {
    const std::string& design_path = positionals[0];
    const std::string& faults_path = positionals[1];

    atpg::PodemBudget budget;
    std::string budget_error;
    if (!parse_budget(positionals, budget, budget_error)) {
        err << "gmax: " << budget_error << "\n";
        return ExitCode::Usage;
    }

    const bool write_reports = positionals.size() > 4;
    const std::string report_directory = write_reports ? positionals[4] : std::string();
    if (write_reports && report_directory.empty()) {
        err << "gmax: report-dir must not be empty\n";
        return ExitCode::Usage;
    }
    if (write_reports) {
        std::error_code directory_error;
        std::filesystem::create_directories(report_directory, directory_error);
        if (directory_error || !std::filesystem::is_directory(report_directory, directory_error)) {
            err << "gmax: cannot use report directory '" << report_directory
                << "': " << (directory_error ? directory_error.message() : "not a directory")
                << '\n';
            return ExitCode::Design;
        }
    }

    db::DesignDb database;
    const db::Status loaded = db::DesignDb::load(design_path, database);
    if (!loaded.ok()) {
        err << "gmax: cannot load design '" << design_path << "': " << loaded.message() << "\n";
        return ExitCode::Design;
    }

    std::string primary_output_error;
    const std::vector<db::NetId> observables = primary_outputs(database, primary_output_error);
    if (!primary_output_error.empty()) {
        err << "gmax: " << primary_output_error << "\n";
        return ExitCode::Design;
    }

    atpg::FaultListFile faults;
    const db::Status read = atpg::read_faults(faults_path, database, faults);
    if (!read.ok()) {
        err << "gmax: cannot read faults '" << faults_path << "': " << read.message() << "\n";
        return ExitCode::Design;
    }

    std::size_t detected = 0;
    std::size_t untestable = 0;
    std::size_t aborted = 0;
    std::size_t backtracks = 0;
    std::size_t decisions = 0;
    std::vector<report::FaultOutcome> outcomes;
    outcomes.reserve(faults.collapsed.size());

    out << "raw faults: " << faults.raw.size() << "\n";
    out << "collapsed faults: " << faults.collapsed.size() << "\n";
    for (const sim::Fault& fault : faults.collapsed) {
        const auto fault_start = std::chrono::steady_clock::now();
        const atpg::PodemResult result =
            atpg::run_podem(database.flat(), fault, observables, budget);
        atpg::PodemStatus status = result.status;
        bool sat_fallback = false;
        // PODEM is the structural engine; a budget abort is not evidence of
        // redundancy. Hand only those faults to the complete SAT miter so a
        // test can be recovered or an UNSAT proof can classify the fault as
        // untestable. SAT gets only the wall-clock time left in this fault's
        // budget; a zero-backtrack run is an explicit caller request for an
        // incomplete result, so it stays aborted and keeps exit code 4.
        if (status == atpg::PodemStatus::Aborted && budget.max_backtracks > 0) {
            bool sat_time_available = true;
            std::uint64_t remaining_time_ms = 0;
            if (budget.max_time_ms != 0) {
                const double elapsed = std::chrono::duration<double, std::milli>(
                                           std::chrono::steady_clock::now() - fault_start)
                                           .count();
                sat_time_available = elapsed < static_cast<double>(budget.max_time_ms);
                if (sat_time_available) {
                    // Floor the remainder: SAT may use less than the caller's
                    // budget but must not receive a fresh millisecond after a
                    // time-budget abort. Zero here means stop, not "unlimited".
                    remaining_time_ms = static_cast<std::uint64_t>(
                        static_cast<double>(budget.max_time_ms) - elapsed);
                    sat_time_available = remaining_time_ms != 0;
                }
            }
            if (sat_time_available) {
                const atpg::SatFaultResult proof = atpg::check_fault(
                    database.flat(), fault, observables, 1000000, remaining_time_ms);
                if (proof.status == atpg::SatFaultStatus::Detected) {
                    status = atpg::PodemStatus::Detected;
                    sat_fallback = true;
                } else if (proof.status == atpg::SatFaultStatus::Untestable) {
                    status = atpg::PodemStatus::Untestable;
                    sat_fallback = true;
                }
            }
        }
        switch (status) {
        case atpg::PodemStatus::Detected:
            ++detected;
            break;
        case atpg::PodemStatus::Untestable:
            ++untestable;
            break;
        case atpg::PodemStatus::Aborted:
            ++aborted;
            break;
        }
        backtracks += result.backtracks;
        decisions += result.decisions;
        outcomes.push_back(report::FaultOutcome{fault, net_name(database, fault.net), status,
                                                result.decisions, result.backtracks,
                                                sat_fallback ? "sat" : "podem"});

        out << "fault " << fault.net << ' ' << (fault.stuck == sim::StuckValue::One ? '1' : '0')
            << ": " << atpg::podem_status_name(status) << " decisions=" << result.decisions
            << " backtracks=" << result.backtracks << (sat_fallback ? " [sat]" : "") << '\n';
    }

    report::CoverageSummary summary;
    summary.raw = faults.raw.size();
    summary.collapsed = faults.collapsed.size();
    summary.detected = detected;
    summary.untestable = untestable;
    summary.aborted = aborted;
    summary.decisions = decisions;
    summary.backtracks = backtracks;
    std::vector<atpg::PodemStatus> collapsed_status;
    collapsed_status.reserve(outcomes.size());
    for (const report::FaultOutcome& outcome : outcomes) {
        collapsed_status.push_back(outcome.status);
    }
    for (const std::uint32_t collapsed_id : faults.raw_to_collapsed) {
        const atpg::PodemStatus status = collapsed_id < collapsed_status.size()
                                             ? collapsed_status[collapsed_id]
                                             : atpg::PodemStatus::Aborted;
        switch (status) {
        case atpg::PodemStatus::Detected:
            ++summary.raw_detected;
            break;
        case atpg::PodemStatus::Untestable:
            ++summary.raw_untestable;
            break;
        case atpg::PodemStatus::Aborted:
            ++summary.raw_aborted;
            break;
        }
    }

    out << "detected: " << detected << "\n";
    out << "untestable: " << untestable << "\n";
    out << "aborted: " << aborted << "\n";
    out << "backtracks: " << backtracks << "\n";
    out << "decisions: " << decisions << "\n";
    out << "coverage raw: " << summary.raw_detected << '/' << summary.raw << "\n";
    out << "coverage collapsed: " << summary.detected << '/' << summary.collapsed << "\n";
    out << "coverage testable: " << summary.detected << '/' << (summary.detected + summary.aborted)
        << "\n";
    out << "coverage atpg: " << summary.detected << '/' << summary.collapsed << "\n";
    out << "netlist hash: " << report::hex32(faults.header.netlist_hash) << "\n";
    out << "fault-list hash: " << report::hex32(faults.header.fault_list_hash) << "\n";

    report::CoverageReport coverage;
    coverage.design = database.design().top;
    coverage.netlist_hash = faults.header.netlist_hash;
    coverage.fault_list_hash = faults.header.fault_list_hash;
    coverage.budget = budget;
    coverage.summary = summary;
    coverage.faults = std::move(outcomes);
    const std::string coverage_text = report::coverage_json(coverage);

    if (write_reports) {
        const std::filesystem::path coverage_path =
            std::filesystem::path(report_directory) / "coverage.json";
        const std::filesystem::path manifest_path =
            std::filesystem::path(report_directory) / "manifest.json";
        const db::Status coverage_status =
            report::write_text(coverage_path.string(), coverage_text);
        if (!coverage_status.ok()) {
            err << "gmax: cannot write coverage report '" << coverage_path.string()
                << "': " << coverage_status.message() << '\n';
            return ExitCode::Design;
        }

        report::RunManifest manifest;
        manifest.tool_version = std::string(version());
        manifest.design = database.design().top;
        manifest.design_path = design_path;
        manifest.faults_path = faults_path;
        manifest.coverage_path = coverage_path.string();
        manifest.netlist_hash = faults.header.netlist_hash;
        manifest.fault_list_hash = faults.header.fault_list_hash;
        manifest.coverage_hash = db::crc32(coverage_text);
        manifest.gates = database.flat().gate_count();
        manifest.nets = database.flat().net_count();
        manifest.budget = budget;
        manifest.summary = summary;
        const db::Status manifest_status =
            report::write_text(manifest_path.string(), report::manifest_json(manifest));
        if (!manifest_status.ok()) {
            err << "gmax: cannot write run manifest '" << manifest_path.string()
                << "': " << manifest_status.message() << '\n';
            return ExitCode::Design;
        }
        out << "coverage report: '" << coverage_path.string() << "'\n";
        out << "run manifest: '" << manifest_path.string() << "'\n";
    }

    return aborted == 0 ? ExitCode::Success : ExitCode::AtpgIncomplete;
}

ExitCode sat_smoke_handler(const std::vector<std::string>& positionals, std::ostream& out,
                           std::ostream& err) {
    const std::string& design_path = positionals[0];
    const std::string& faults_path = positionals[1];
    const std::string& report_path = positionals[2];

    sim::Fault sat_fault;
    sim::Fault unsat_fault;
    std::string parse_error;
    if (!parse_fault_spec(positionals[3], sat_fault, parse_error) ||
        !parse_fault_spec(positionals[4], unsat_fault, parse_error)) {
        err << "gmax: " << parse_error << "\n";
        return ExitCode::Usage;
    }

    db::DesignDb database;
    const db::Status loaded = db::DesignDb::load(design_path, database);
    if (!loaded.ok()) {
        err << "gmax: cannot load design '" << design_path << "': " << loaded.message() << "\n";
        return ExitCode::Design;
    }

    std::string primary_output_error;
    const std::vector<db::NetId> observables = primary_outputs(database, primary_output_error);
    if (!primary_output_error.empty()) {
        err << "gmax: " << primary_output_error << "\n";
        return ExitCode::Design;
    }

    atpg::FaultListFile faults;
    const db::Status read = atpg::read_faults(faults_path, database, faults);
    if (!read.ok()) {
        err << "gmax: cannot read faults '" << faults_path << "': " << read.message() << "\n";
        return ExitCode::Design;
    }

    for (const sim::Fault& fault : {sat_fault, unsat_fault}) {
        if (std::find(faults.collapsed.begin(), faults.collapsed.end(), fault) ==
            faults.collapsed.end()) {
            err << "gmax: fault " << fault.net << ' '
                << (fault.stuck == sim::StuckValue::One ? '1' : '0')
                << " is not in the collapsed fault list\n";
            return ExitCode::Design;
        }
    }

    report::SatSmokeReport smoke;
    smoke.design = database.design().top;
    smoke.netlist_hash = faults.header.netlist_hash;
    smoke.fault_list_hash = faults.header.fault_list_hash;

    const std::pair<sim::Fault, std::string> cases[] = {
        {sat_fault, "sat"},
        {unsat_fault, "unsat"},
    };
    for (const auto& [fault, expected] : cases) {
        const atpg::SatFaultResult result = atpg::check_fault(database.flat(), fault, observables);
        report::SatSmokeCase item;
        item.fault = fault;
        item.net_name = net_name(database, fault.net);
        item.expected = expected;
        item.status = result.status;
        item.conflicts = result.conflicts;
        item.variables = result.variables;
        item.clauses = result.clauses;
        item.solver_timed_out = result.solver_timed_out;
        smoke.cases.push_back(std::move(item));

        const bool expected_match =
            (expected == "sat" && result.status == atpg::SatFaultStatus::Detected) ||
            (expected == "unsat" && result.status == atpg::SatFaultStatus::Untestable);
        if (!expected_match) {
            err << "gmax: SAT smoke case " << fault.net << ' '
                << (fault.stuck == sim::StuckValue::One ? '1' : '0') << " was not " << expected
                << "\n";
            return ExitCode::AtpgIncomplete;
        }
    }

    const std::filesystem::path path(report_path);
    if (path.has_parent_path()) {
        std::error_code directory_error;
        std::filesystem::create_directories(path.parent_path(), directory_error);
        if (directory_error) {
            err << "gmax: cannot create report directory '" << path.parent_path().string()
                << "': " << directory_error.message() << '\n';
            return ExitCode::Design;
        }
    }
    const db::Status written = report::write_text(report_path, report::sat_smoke_json(smoke));
    if (!written.ok()) {
        err << "gmax: cannot write SAT smoke report '" << report_path << "': " << written.message()
            << '\n';
        return ExitCode::Design;
    }

    out << "sat smoke cases: " << smoke.cases.size() << '\n';
    out << "sat smoke report: '" << report_path << "'\n";
    return ExitCode::Success;
}

}  // namespace

bool register_commands(Cli& cli, std::string& error) {
    CommandSpec read_spec;
    read_spec.name = "read_verilog";
    read_spec.summary = "run Yosys and write a flattened .dfdb design database";
    read_spec.positionals = {
        {"netlist", "gate-level Verilog netlist", true},
        {"design", "output .dfdb design database", true},
        {"top", "top module name", true},
    };
    if (!cli.add_command(std::move(read_spec), read_verilog_handler, error)) {
        return false;
    }

    CommandSpec write_spec;
    write_spec.name = "write_faults";
    write_spec.summary = "write a .faults fault list from a flattened .dfdb";
    write_spec.positionals = {
        {"design", "flattened .dfdb design database", true},
        {"faults", "output .faults file", true},
    };
    if (!cli.add_command(std::move(write_spec), write_faults_handler, error)) {
        return false;
    }

    CommandSpec run_spec;
    run_spec.name = "run_atpg";
    run_spec.summary = "run PODEM and the SAT closer over the collapsed faults in a .faults list";
    run_spec.positionals = {
        {"design", "flattened .dfdb design database", true},
        {"faults", "read .faults file", true},
        {"max-backtracks", "per-fault backtrack limit", false},
        {"max-time-ms", "per-fault wall-clock limit; 0 disables", false},
        {"report-dir", "directory for coverage.json and manifest.json", false},
    };
    if (!cli.add_command(std::move(run_spec), run_atpg_handler, error)) {
        return false;
    }

    CommandSpec smoke_spec;
    smoke_spec.name = "sat_smoke";
    smoke_spec.summary = "run one SAT and one UNSAT case through the SAT solver boundary";
    smoke_spec.positionals = {
        {"design", "flattened .dfdb design database", true},
        {"faults", "read .faults file", true},
        {"report", "output sat_smoke.json", true},
        {"sat-fault", "testable fault as <net>:<sa0|sa1>", true},
        {"unsat-fault", "redundant fault as <net>:<sa0|sa1>", true},
    };
    return cli.add_command(std::move(smoke_spec), sat_smoke_handler, error);
}

}  // namespace gmax::shell
