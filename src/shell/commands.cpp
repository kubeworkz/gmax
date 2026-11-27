#include "shell/commands.h"

#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <limits>
#include <ostream>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include "core/atpg/fault_file.h"
#include "core/atpg/fault_list.h"
#include "core/atpg/podem.h"
#include "core/atpg/sat.h"
#include "core/db/constraints.h"
#include "core/db/crc32.h"
#include "core/db/db.h"
#include "core/db/design.h"
#include "core/db/status.h"
#include "core/dft/capture.h"
#include "core/dft/capture_crossing.h"
#include "core/dft/capture_profile.h"
#include "core/dft/capture_refusal.h"
#include "core/frontend/diagnostics.h"
#include "core/frontend/sdc.h"
#include "core/frontend/sdc_constraints.h"
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

// ADR-011: a work unit is a fixed 64-fault chunk in fault-list order, matching
// the word engine's lane chunking, and workers merge by fault ID rather than by
// completion order.
constexpr std::size_t kFaultChunk = 64;
constexpr std::size_t kMaxThreads = 256;

bool parse_threads(const Invocation& invocation, std::size_t& threads, std::string& error) {
    for (const OptionValue& option : invocation.options) {
        if (option.name != "threads") {
            continue;
        }
        std::uint64_t value = 0;
        if (!parse_u64(option.value, value) || value == 0 || value > kMaxThreads) {
            error = "invalid --threads '" + option.value + "', expected 1.." +
                    std::to_string(kMaxThreads);
            return false;
        }
        threads = static_cast<std::size_t>(value);
    }
    return true;
}

// The option named `name`, or nullptr when the command line did not carry it. The
// parser rejects a repeated option, so at most one match exists.
const OptionValue* find_option(const Invocation& invocation, const std::string_view name) {
    for (const OptionValue& option : invocation.options) {
        if (option.name == name) {
            return &option;
        }
    }
    return nullptr;
}

// ADR-013 section 4: a profile the reader rejects is an unreadable procedure, which is
// a run-level failure without a refusal code -- none of the six conditions judged a
// procedure that never existed. The object path inside the document names the place to
// fix, and the message already names the file, so no zero line number is printed.
void print_profile_diagnostics(const std::vector<frontend::Diagnostic>& diagnostics,
                               std::ostream& err) {
    for (const frontend::Diagnostic& diagnostic : diagnostics) {
        err << "gmax: " << frontend::diagnostic_code_name(diagnostic.code) << ": "
            << diagnostic.message;
        if (!diagnostic.object.empty()) {
            err << " (" << diagnostic.object << ')';
        }
        err << '\n';
    }
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

ExitCode read_verilog_handler(const Invocation& invocation, std::ostream& out, std::ostream& err) {
    const std::vector<std::string>& positionals = invocation.positionals;
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

ExitCode write_faults_handler(const Invocation& invocation, std::ostream& out, std::ostream& err) {
    const std::vector<std::string>& positionals = invocation.positionals;
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

// Resolves a constraint object name to the one net that carries it. False means
// no such net or more than one, which the attachment step reports as an
// unresolved or ambiguous reference (ADR-012 section 3).
bool resolve_net(const db::DesignDb& database, const std::string_view name, db::NetId& out) {
    const db::FlatNetlist& flat = database.flat();
    const db::StringTable& strings = database.strings();
    bool found = false;
    for (db::NetId net = 0; net < flat.net_count(); ++net) {
        if (strings.at(flat.name_id[net]) != name) {
            continue;
        }
        if (found) {
            return false;
        }
        out = net;
        found = true;
    }
    return found;
}

// Resolves the attached case values to nets (ADR-012 section 4). Returns an empty
// string on success, otherwise a message naming the target that cannot be
// enforced: a set that cannot be applied must not be graded as if it had been.
std::string resolve_case_assignments(const db::DesignDb& database,
                                     std::vector<db::CaseAssignment>& out) {
    for (const db::ConstraintCaseAnalysis& item : database.constraints().case_analysis) {
        db::NetId net = db::kInvalidId;
        if (!resolve_net(database, item.object, net)) {
            return "case-analysis object '" + item.object + "' does not resolve to a net";
        }
        if (database.flat().driver[net] != db::kPrimaryInput) {
            return "case-analysis object '" + item.object + "' is not a primary input";
        }
        out.push_back({net, item.value});
    }
    return {};
}

struct FaultGrade {
    atpg::PodemStatus status = atpg::PodemStatus::Aborted;
    std::size_t decisions = 0;
    std::size_t backtracks = 0;
    bool sat_fallback = false;
};

// Grades one fault: PODEM first, then the SAT closer only for a budget abort.
// The function touches no shared state, so workers call it concurrently and
// write into disjoint fault-indexed slots.
FaultGrade grade_fault(const db::FlatNetlist& netlist, const sim::Fault& fault,
                       const std::span<const db::NetId> observables,
                       const atpg::PodemBudget& budget,
                       const std::span<const db::CaseAssignment> constraints) {
    const auto fault_start = std::chrono::steady_clock::now();
    const atpg::PodemResult result =
        atpg::run_podem(netlist, fault, observables, budget, constraints);
    FaultGrade grade;
    grade.status = result.status;
    grade.decisions = result.decisions;
    grade.backtracks = result.backtracks;
    // PODEM is the structural engine; a budget abort is not evidence of
    // redundancy. Hand only those faults to the complete SAT miter so a test can
    // be recovered or an UNSAT proof can classify the fault as untestable. SAT
    // gets only the wall-clock time left in this fault's budget; a
    // zero-backtrack run is an explicit caller request for an incomplete
    // result, so it stays aborted and keeps exit code 4.
    if (grade.status == atpg::PodemStatus::Aborted && budget.max_backtracks > 0) {
        bool sat_time_available = true;
        std::uint64_t remaining_time_ms = 0;
        if (budget.max_time_ms != 0) {
            const double elapsed = std::chrono::duration<double, std::milli>(
                                       std::chrono::steady_clock::now() - fault_start)
                                       .count();
            sat_time_available = elapsed < static_cast<double>(budget.max_time_ms);
            if (sat_time_available) {
                // Floor the remainder: SAT may use less than the caller's budget
                // but must not receive a fresh millisecond after a time-budget
                // abort. Zero here means stop, not "unlimited".
                remaining_time_ms =
                    static_cast<std::uint64_t>(static_cast<double>(budget.max_time_ms) - elapsed);
                sat_time_available = remaining_time_ms != 0;
            }
        }
        if (sat_time_available) {
            const atpg::SatFaultResult proof = atpg::check_fault(
                netlist, fault, observables, 1000000, remaining_time_ms, nullptr, constraints);
            if (proof.status == atpg::SatFaultStatus::Detected) {
                grade.status = atpg::PodemStatus::Detected;
                grade.sat_fallback = true;
            } else if (proof.status == atpg::SatFaultStatus::Untestable) {
                grade.status = atpg::PodemStatus::Untestable;
                grade.sat_fallback = true;
            }
        }
    }
    return grade;
}

ExitCode run_atpg_handler(const Invocation& invocation, std::ostream& out, std::ostream& err) {
    const std::vector<std::string>& positionals = invocation.positionals;
    const std::string& design_path = positionals[0];
    const std::string& faults_path = positionals[1];

    atpg::PodemBudget budget;
    std::string budget_error;
    if (!parse_budget(positionals, budget, budget_error)) {
        err << "gmax: " << budget_error << "\n";
        return ExitCode::Usage;
    }

    std::size_t threads = 1;
    std::string threads_error;
    if (!parse_threads(invocation, threads, threads_error)) {
        err << "gmax: " << threads_error << "\n";
        return ExitCode::Usage;
    }

    const bool write_reports = positionals.size() > 4;
    const std::string report_directory = write_reports ? positionals[4] : std::string();
    if (write_reports && report_directory.empty()) {
        err << "gmax: report-dir must not be empty\n";
        return ExitCode::Usage;
    }
    // ADR-013 section 4: a refusal writes nothing, so the report directory is not created
    // during argument handling, where a later refusal would leave it behind. Only a path
    // that can never hold the reports is rejected here, and that check creates nothing.
    if (write_reports) {
        std::error_code directory_error;
        const std::filesystem::file_status report_status =
            std::filesystem::status(report_directory, directory_error);
        const std::filesystem::file_type report_type = report_status.type();
        if (report_type != std::filesystem::file_type::not_found &&
            report_type != std::filesystem::file_type::none &&
            !std::filesystem::is_directory(report_status)) {
            err << "gmax: cannot use report directory '" << report_directory
                << "': not a directory\n";
            return ExitCode::Design;
        }
    }

    db::DesignDb database;
    const db::Status loaded = db::DesignDb::load(design_path, database);
    if (!loaded.ok()) {
        err << "gmax: cannot load design '" << design_path << "': " << loaded.message() << "\n";
        return ExitCode::Design;
    }

    // ADR-012 section 3: an incomplete set is never graded. The stored warnings
    // are printed and the run stops as a design error before faults are read or
    // any report is written. There is no implicit "ignore SDC" mode.
    const db::ConstraintsStatus constraints_status = db::constraints_status(database.constraints());
    std::uint32_t constraints_hash_value = 0;
    if (constraints_status != db::ConstraintsStatus::Absent) {
        constraints_hash_value = db::constraints_hash(database.constraints());
    }
    if (constraints_status == db::ConstraintsStatus::Incomplete) {
        err << "gmax: design constraints are incomplete; refusing to grade\n";
        for (const std::string& name : database.constraints().unsupported) {
            err << "gmax:   unsupported construct: " << name << '\n';
        }
        return ExitCode::Design;
    }

    // The values are resolved once, before any grading work starts, so a target
    // that cannot be enforced stops the run instead of being skipped silently.
    std::vector<db::CaseAssignment> case_assignments;
    if (constraints_status == db::ConstraintsStatus::Complete) {
        const std::string resolve_error = resolve_case_assignments(database, case_assignments);
        if (!resolve_error.empty()) {
            err << "gmax: " << resolve_error << '\n';
            return ExitCode::Design;
        }
    }

    std::string primary_output_error;
    const std::vector<db::NetId> observables = primary_outputs(database, primary_output_error);
    if (!primary_output_error.empty()) {
        err << "gmax: " << primary_output_error << "\n";
        return ExitCode::Design;
    }

    // ADR-013 section 4: a procedure the run cannot apply refuses it here,
    // after the constraint checks and before the fault list is read, so a refused
    // run prints no fault count and writes no report. ExitCode::Design is the same
    // status a load failure and an incomplete set use; refusals add no new code.
    // ADR-013 section 3: a declared clock that clocks no state-holding gate is
    // dropped from the procedure before it is applied, so the procedure the run
    // grades is the one without it. A drop that leaves another clock is a warning;
    // a drop that empties the list is section 4's condition 5.
    // ADR-013 section 1: the profile declares the procedure and the SDC decides which
    // clocks exist, their groups and their waveforms, so the document is read against
    // the constraints the run already loaded. Without one the engine derives the
    // procedure, and a profile that carries no capture object derives it too.
    const OptionValue* profile_option = find_option(invocation, "profile");
    if (profile_option != nullptr && profile_option->value.empty()) {
        err << "gmax: profile path must not be empty\n";
        return ExitCode::Usage;
    }
    gmax::dft::CaptureProfileResult profile;
    if (profile_option != nullptr) {
        profile =
            gmax::dft::read_capture_profile_file(profile_option->value, database.constraints());
        if (!profile.ok) {
            // Section 4: the findings print, the run exits 2 and writes nothing. The
            // check runs before the fault list is read, like every other refusal.
            print_profile_diagnostics(profile.diagnostics, err);
            return ExitCode::Design;
        }
    }
    const gmax::dft::CaptureProcedure declared_procedure =
        profile.declared ? profile.procedure
                         : gmax::dft::resolve_capture_procedure(database.constraints());

    const std::vector<db::ClockGateCount> clock_gates =
        db::clock_gate_counts(database.constraints(), database.flat(), database.strings());
    std::vector<std::string> dropped_clocks;
    const gmax::dft::CaptureProcedure capture_procedure = gmax::dft::exclude_idle_clocks(
        declared_procedure, database.constraints(), clock_gates, dropped_clocks);
    // ADR-013 section 5: DFT003 is a question about the design, so it is a second
    // pass over the same procedure. Both passes are merged before the block is
    // written, so one run reports every reason instead of one reason per run.
    std::vector<gmax::dft::CaptureRefusal> capture_refusals =
        gmax::dft::find_capture_refusals(capture_procedure, database.constraints(), clock_gates);
    const std::vector<gmax::dft::CaptureRefusal> crossing_refusals =
        gmax::dft::find_crossing_refusals(capture_procedure, database.constraints(),
                                          database.flat(), database.strings());
    capture_refusals.insert(capture_refusals.end(), crossing_refusals.begin(),
                            crossing_refusals.end());
    gmax::dft::sort_capture_refusals(capture_refusals);
    if (!capture_refusals.empty()) {
        gmax::dft::write_capture_refusals(err, capture_refusals);
        return ExitCode::Design;
    }
    for (const std::string& clock : dropped_clocks) {
        err << "gmax: warning: clock '" << clock
            << "' clocks no state-holding gate and was left out of the capture procedure\n";
    }

    atpg::FaultListFile faults;
    const db::Status read = atpg::read_faults(faults_path, database, faults);
    if (!read.ok()) {
        err << "gmax: cannot read faults '" << faults_path << "': " << read.message() << "\n";
        return ExitCode::Design;
    }

    out << "raw faults: " << faults.raw.size() << "\n";
    out << "collapsed faults: " << faults.collapsed.size() << "\n";

    const std::size_t grade_count = faults.collapsed.size();
    std::vector<FaultGrade> grades(grade_count);
    std::vector<std::exception_ptr> grade_errors(grade_count);
    const std::size_t chunk_count = (grade_count + kFaultChunk - 1) / kFaultChunk;
    const std::size_t worker_count = chunk_count == 0 ? 1 : std::min(threads, chunk_count);
    const auto grade_chunk = [&](const std::size_t chunk) {
        const std::size_t begin = chunk * kFaultChunk;
        const std::size_t end = std::min(begin + kFaultChunk, grade_count);
        for (std::size_t index = begin; index < end; ++index) {
            try {
                grades[index] = grade_fault(database.flat(), faults.collapsed[index], observables,
                                            budget, case_assignments);
            } catch (...) {
                grade_errors[index] = std::current_exception();
            }
        }
    };

    if (worker_count == 1) {
        for (std::size_t chunk = 0; chunk < chunk_count; ++chunk) {
            grade_chunk(chunk);
        }
    } else {
        // Chunks are handed out by index and results land in fault-ID slots, so
        // the merge is an order-independent assignment rather than a reduction.
        std::atomic<std::size_t> next_chunk{0};
        std::vector<std::thread> workers;
        workers.reserve(worker_count);
        for (std::size_t worker = 0; worker < worker_count; ++worker) {
            workers.emplace_back([&] {
                for (;;) {
                    const std::size_t chunk = next_chunk.fetch_add(1, std::memory_order_relaxed);
                    if (chunk >= chunk_count) {
                        return;
                    }
                    grade_chunk(chunk);
                }
            });
        }
        for (std::thread& worker : workers) {
            worker.join();
        }
    }
    for (const std::exception_ptr& error : grade_errors) {
        if (error) {
            std::rethrow_exception(error);
        }
    }

    std::size_t detected = 0;
    std::size_t untestable = 0;
    std::size_t aborted = 0;
    std::size_t backtracks = 0;
    std::size_t decisions = 0;
    std::vector<report::FaultOutcome> outcomes;
    outcomes.reserve(grade_count);
    for (std::size_t index = 0; index < grade_count; ++index) {
        const sim::Fault& fault = faults.collapsed[index];
        const FaultGrade& grade = grades[index];
        switch (grade.status) {
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
        backtracks += grade.backtracks;
        decisions += grade.decisions;
        outcomes.push_back(report::FaultOutcome{fault, net_name(database, fault.net), grade.status,
                                                grade.decisions, grade.backtracks,
                                                grade.sat_fallback ? "sat" : "podem"});

        out << "fault " << fault.net << ' ' << (fault.stuck == sim::StuckValue::One ? '1' : '0')
            << ": " << atpg::podem_status_name(grade.status) << " decisions=" << grade.decisions
            << " backtracks=" << grade.backtracks << (grade.sat_fallback ? " [sat]" : "") << '\n';
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
    out << "constraints status: " << db::constraints_status_name(constraints_status) << "\n";
    if (constraints_status != db::ConstraintsStatus::Absent) {
        out << "constraints hash: " << report::hex32(constraints_hash_value) << "\n";
    }

    report::CoverageReport coverage;
    coverage.design = database.design().top;
    coverage.netlist_hash = faults.header.netlist_hash;
    coverage.fault_list_hash = faults.header.fault_list_hash;
    coverage.constraints_status = constraints_status;
    coverage.constraints_hash = constraints_hash_value;
    // ADR-013 section 6: the capture object. A procedure with no cycle makes no
    // capture claim, which the report calls `absent`; anything else reached grading,
    // so its procedure is `enforced` and the hash covers the canonical payload the
    // run actually applied.
    report::CaptureReport capture_report;
    capture_report.absent = capture_procedure.cycles.empty();
    if (!capture_report.absent) {
        capture_report.procedure =
            std::string(gmax::dft::capture_kind_name(capture_procedure.kind));
        capture_report.derived = capture_procedure.derived;
        capture_report.hash = gmax::dft::capture_hash(capture_procedure);
        for (const gmax::dft::CaptureCycle& cycle : capture_procedure.cycles) {
            report::CaptureCycleReport cycle_report;
            for (const gmax::dft::CaptureGroup& group : cycle.groups) {
                report::CaptureGroupReport group_report;
                group_report.clocks = group.timing.clocks;
                group_report.edge = group.timing.edge;
                cycle_report.groups.push_back(std::move(group_report));
            }
            capture_report.cycles.push_back(std::move(cycle_report));
        }
    }
    // ADR-013 section 5's DFT019: a false path between two clocks the procedure
    // captures apart hides the capture edge between them. It is a warning, so the run
    // grades and records it rather than refusing.
    std::vector<report::FalsePathWarningReport> false_path_warnings;
    for (const gmax::dft::FalsePathWarning& warning :
         gmax::dft::find_false_path_warnings(capture_procedure, database.constraints())) {
        report::FalsePathWarningReport finding;
        finding.from = warning.from_clock;
        finding.to = warning.to_clock;
        finding.line = warning.line;
        false_path_warnings.push_back(std::move(finding));
    }
    coverage.false_paths = false_path_warnings;
    coverage.capture = capture_report;
    // ADR-013 section 6: the enforced list names what the run actually used, never
    // what the database holds. A procedure that ran applied the clock-group model,
    // so an enforced capture cannot be reported without it, and a run that examined
    // a retained false-path set names that too.
    coverage.enforced = report::enforced_constructs(!case_assignments.empty(), capture_report,
                                                    !database.constraints().false_paths.empty());
    coverage.budget = budget;
    coverage.summary = summary;
    coverage.faults = std::move(outcomes);
    const std::string coverage_text = report::coverage_json(coverage);

    if (write_reports) {
        // ADR-013 section 4: the directory appears only when the reports are about to be
        // written, so every refusal above leaves no trace on disk.
        std::error_code directory_error;
        std::filesystem::create_directories(report_directory, directory_error);
        if (directory_error || !std::filesystem::is_directory(report_directory, directory_error)) {
            err << "gmax: cannot use report directory '" << report_directory
                << "': " << (directory_error ? directory_error.message() : "not a directory")
                << '\n';
            return ExitCode::Design;
        }
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
        // ADR-012: constraints are a run input with their own identity. The SDC
        // path is provenance only and is never part of the hash.
        manifest.constraints_status = constraints_status;
        manifest.constraints_hash = constraints_hash_value;
        manifest.capture = capture_report;
        manifest.false_paths = false_path_warnings;
        manifest.gates = database.flat().gate_count();
        manifest.nets = database.flat().net_count();
        manifest.budget = budget;
        manifest.threads = static_cast<std::uint32_t>(worker_count);
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

ExitCode sat_smoke_handler(const Invocation& invocation, std::ostream& out, std::ostream& err) {
    const std::vector<std::string>& positionals = invocation.positionals;
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

ExitCode read_sdc_handler(const Invocation& invocation, std::ostream& out, std::ostream& err) {
    const std::string& sdc_path = invocation.positionals[0];
    const frontend::SdcResult result = frontend::read_sdc_file(sdc_path);
    if (!result.diagnostics.empty()) {
        print_diagnostics(result.diagnostics, err);
    }
    if (!result.ok) {
        if (result.diagnostics.empty()) {
            err << "gmax: the SDC frontend failed without a diagnostic\n";
        }
        return ExitCode::Design;
    }

    const frontend::SdcConstraints& constraints = result.constraints;
    std::size_t warnings = 0;
    for (const frontend::Diagnostic& diagnostic : result.diagnostics) {
        if (!diagnostic.is_error()) {
            ++warnings;
        }
    }
    std::size_t input_delays = 0;
    std::size_t output_delays = 0;
    for (const frontend::SdcDelay& delay : constraints.delays) {
        if (delay.kind == frontend::SdcDelayKind::Input) {
            ++input_delays;
        } else {
            ++output_delays;
        }
    }

    out << "sdc: " << sdc_path << '\n';
    out << "clocks: " << constraints.clocks.size() << '\n';
    for (const frontend::SdcClock& clock : constraints.clocks) {
        out << "  clock " << clock.name << " period=" << clock.period << " source=" << clock.source;
        if (clock.generated) {
            out << " generated master=" << clock.master_clock << " source_pin=" << clock.source_pin;
            if (clock.divide_by != 0) {
                out << " divide_by=" << clock.divide_by;
            }
            if (clock.multiply_by != 0) {
                out << " multiply_by=" << clock.multiply_by;
            }
        }
        out << '\n';
        // ADR-012 section 4: a generated clock's master is checked against the
        // declared clocks and the clock's -source, and an implicit link is
        // checked for an ambiguous source. A finding is annotated here and
        // counted in `warnings`; the clock keeps its recorded master.
        if (clock.generated) {
            const frontend::SdcGeneratedClockIssues issues =
                frontend::sdc_generated_clock_issues(constraints, clock);
            for (const std::string& master : issues.unknown) {
                out << "  unknown master " << master << '\n';
            }
            for (const std::string& master : issues.conflicting) {
                out << "  conflicting master " << master << '\n';
            }
            if (!issues.ambiguous.empty()) {
                out << "  ambiguous source " << clock.source_pin << " (";
                for (std::size_t i = 0; i < issues.ambiguous.size(); ++i) {
                    if (i != 0) {
                        out << ' ';
                    }
                    out << issues.ambiguous[i];
                }
                out << ")" << '\n';
            }
            for (const std::string& cycle : issues.cycles) {
                out << "  master cycle " << cycle << '\n';
            }
        }
    }
    out << "clock groups: " << constraints.clock_groups.size() << '\n';
    for (const frontend::SdcClockGroup& group : constraints.clock_groups) {
        out << "  group " << frontend::sdc_clock_group_kind_name(group.kind);
        for (const std::vector<std::string>& clocks : group.groups) {
            out << " [";
            for (std::size_t i = 0; i < clocks.size(); ++i) {
                if (i != 0) {
                    out << ' ';
                }
                out << clocks[i];
            }
            out << ']';
        }
        out << '\n';
        // ADR-012 section 4: the members are checked against the declared clock
        // names. A finding is annotated here and counted in `warnings`; the
        // member stays in the group and the set keeps its status.
        const frontend::SdcClockGroupIssues issues =
            frontend::sdc_clock_group_issues(constraints, group);
        for (const std::string& member : issues.unknown) {
            out << "  unknown member " << member << '\n';
        }
        for (const std::string& member : issues.conflicting) {
            out << "  conflicting member " << member << '\n';
        }
    }
    out << "case analysis: " << constraints.case_analysis.size() << '\n';
    for (const frontend::SdcCaseAnalysis& item : constraints.case_analysis) {
        out << "  case " << item.object << '=' << (item.value ? '1' : '0') << '\n';
    }
    out << "false paths: " << constraints.false_paths.size() << '\n';
    for (const frontend::SdcFalsePath& path : constraints.false_paths) {
        out << "  false_path from=";
        for (std::size_t i = 0; i < path.from.size(); ++i) {
            out << (i == 0 ? "" : ",") << path.from[i];
        }
        out << " to=";
        for (std::size_t i = 0; i < path.to.size(); ++i) {
            out << (i == 0 ? "" : ",") << path.to[i];
        }
        out << '\n';
    }
    out << "input delays: " << input_delays << '\n';
    out << "output delays: " << output_delays << '\n';
    out << "driving cells: " << constraints.driving_cells.size() << '\n';
    out << "loads: " << constraints.loads.size() << '\n';
    out << "warnings: " << warnings << '\n';

    // ADR-012 decision 1: report-only stays the default. Attachment needs both
    // --design and --output, and read_sdc never overwrites its input database.
    std::string design_option;
    std::string output_option;
    for (const OptionValue& option : invocation.options) {
        if (option.name == "design") {
            design_option = option.value;
        } else if (option.name == "output") {
            output_option = option.value;
        }
    }
    if (design_option.empty() != output_option.empty()) {
        err << "gmax: read_sdc needs --design and --output together\n";
        return ExitCode::Usage;
    }

    db::Constraints attached = frontend::to_constraints(constraints, result.diagnostics);
    db::ClockDomains domains;
    bool have_domains = false;

    if (!design_option.empty()) {
        std::error_code equivalent_error;
        if (design_option == output_option ||
            (std::filesystem::exists(design_option, equivalent_error) &&
             std::filesystem::equivalent(design_option, output_option, equivalent_error))) {
            err << "gmax: --output must differ from --design; read_sdc never overwrites its "
                   "input database\n";
            return ExitCode::Usage;
        }

        db::DesignDb database;
        const db::Status loaded = db::DesignDb::load(design_option, database);
        if (!loaded.ok()) {
            err << "gmax: cannot load design '" << design_option << "': " << loaded.message()
                << '\n';
            return ExitCode::Design;
        }

        // A resolved case value is enforceable only on a net that is a top-level
        // input port. An object that names nothing is an error; one that resolves
        // to something else marks the whole set incomplete rather than being
        // silently dropped while its supported companions are kept.
        for (const db::ConstraintCaseAnalysis& item : attached.case_analysis) {
            db::NetId net = db::kInvalidId;
            if (!resolve_net(database, item.object, net)) {
                err << "gmax: case-analysis object '" << item.object
                    << "' does not resolve unambiguously\n";
                return ExitCode::Design;
            }
            if (database.flat().driver[net] != db::kPrimaryInput) {
                attached.complete = false;
            }
        }
        // Clock sources are the other object references this engine resolves;
        // every remaining record is retained for reporting and scan DRC, not
        // enforced, so its names are carried without resolution. A clock that
        // resolves somewhere other than a top-level input port is reported and
        // kept: clocks change no fault outcome, so refusing the set would say
        // more than the engine can back up.
        for (const db::ConstraintClock& clock : attached.clocks) {
            if (clock.source.empty()) {
                continue;
            }
            db::NetId net = db::kInvalidId;
            if (!resolve_net(database, clock.source, net)) {
                err << "gmax: clock source '" << clock.source
                    << "' does not resolve unambiguously\n";
                return ExitCode::Design;
            }
            if (database.flat().driver[net] != db::kPrimaryInput) {
                err << "gmax: clock source '" << clock.source << "' is not a top-level input\n";
            }
        }

        // A false path is retained, not enforced, so an endpoint that resolves
        // nowhere is reported and the set is kept whole. Clock endpoints were
        // checked against the declared names when the file was read; the rest
        // name a port, pin or net, which only the design can resolve.
        const auto report_unresolved = [&](const std::vector<std::string>& endpoints,
                                           const std::string_view option) {
            for (const std::string& endpoint : endpoints) {
                if (constraints.find_clock(endpoint) != nullptr) {
                    continue;
                }
                db::NetId net = db::kInvalidId;
                if (!resolve_net(database, endpoint, net)) {
                    err << "gmax: false path " << option << " '" << endpoint
                        << "' does not resolve to a port, pin or net\n";
                }
            }
        };
        for (const frontend::SdcFalsePath& path : constraints.false_paths) {
            report_unresolved(path.from, "-from");
            report_unresolved(path.through, "-through");
            report_unresolved(path.to, "-to");
        }

        database.constraints() = attached;
        domains = db::clock_domains(attached, database.flat(), database.strings());
        have_domains = true;
        const db::Status saved = database.save(output_option);
        if (!saved.ok()) {
            err << "gmax: cannot write '" << output_option << "': " << saved.message() << '\n';
            return ExitCode::Design;
        }
        out << "attached: " << output_option << '\n';
    }

    out << "constraints status: " << db::constraints_status_name(db::constraints_status(attached))
        << '\n';
    out << "constraints hash: " << report::hex32(db::constraints_hash(attached)) << '\n';
    if (have_domains) {
        // The declared clocks that actually clock a state-holding gate. The
        // report-only form has no netlist to count against, so it omits the
        // line rather than printing a zero it did not measure.
        out << "clock domains: " << domains.domains << " (" << domains.flops << " flops)\n";
    }
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

    CommandSpec sdc_spec;
    sdc_spec.name = "read_sdc";
    sdc_spec.summary =
        "parse a restricted SDC subset; report it, or attach it to a design database";
    sdc_spec.positionals = {
        {"sdc", "SDC constraints file", true},
    };
    sdc_spec.options = {
        {"design", "input.dfdb", "attach the parsed constraints to this design database"},
        {"output", "output.dfdb", "write the attached database here (requires --design)"},
    };
    if (!cli.add_command(std::move(sdc_spec), read_sdc_handler, error)) {
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
    run_spec.options = {
        {"profile", "capture.json", "capture procedure declaration; derived when absent"},
        {"threads", "N", "worker threads; 1 (the default) stays single-threaded"},
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
