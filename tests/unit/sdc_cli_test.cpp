// WP-110: the read_sdc command must read a file through the CLI, report the
// parsed constraints, and return a design error with a source diagnostic for a
// malformed file. Framework-free like the other tests.

#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "core/db/db.h"
#include "core/db/design.h"
#include "core/db/ids.h"
#include "shell/cli.h"
#include "shell/commands.h"

namespace shell = gmax::shell;

namespace {

int failures = 0;

void expect(const bool ok, const std::string_view what) {
    if (!ok) {
        std::cerr << "FAIL " << what << std::endl;
        ++failures;
    }
}

bool contains(const std::string_view text, const std::string_view needle) {
    return text.find(needle) != std::string_view::npos;
}

void write_file(const std::filesystem::path& path, const std::string_view text) {
    std::ofstream file(path, std::ios::binary);
    file << text;
    expect(file.good(), "the SDC fixture writes");
}

struct RunResult {
    shell::ExitCode code = shell::ExitCode::Success;
    std::string out;
    std::string err;
};

RunResult run(const shell::Cli& cli, const std::vector<std::string>& args) {
    std::ostringstream out;
    std::ostringstream err;
    const shell::ExitCode code = cli.run(std::span<const std::string>(args), out, err);
    return {code, out.str(), err.str()};
}

// Built step by step so the attachment arguments stay readable at any width.
RunResult run_attach(const shell::Cli& cli, const std::filesystem::path& sdc,
                     const std::filesystem::path& design, const std::filesystem::path& output) {
    std::vector<std::string> args = {"read_sdc", sdc.string()};
    args.push_back("--design");
    args.push_back(design.string());
    args.push_back("--output");
    args.push_back(output.string());
    return run(cli, args);
}

void test_valid_file(const shell::Cli& cli) {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "gmax-sdc-cli-valid.sdc";
    write_file(path, R"SDC(
create_clock -name clk -period 10 -waveform {0 5} [get_ports clk]
create_clock -name clk_b -period 20 [get_ports clk_b]
create_generated_clock -name clk_div -source [get_ports clk] \
  -divide_by 2 [get_pins u_div/Q]
set_clock_groups -asynchronous -group {clk clk_div} -group {clk_b}
set_false_path -from [get_clocks clk] -to [get_clocks clk_b]
set_case_analysis 1 [get_ports test_mode]
set_input_delay -clock [get_clocks clk] 2 [get_ports data_in]
set_output_delay -clock [get_clocks clk] 1 [get_ports data_out]
set_driving_cell -lib_cell BUF [get_ports data_in]
set_load 0.5 [get_ports data_out]
)SDC");

    const RunResult result = run(cli, {"read_sdc", path.string()});
    expect(result.code == shell::ExitCode::Success, "read_sdc accepts a valid file");
    expect(result.err.empty(), "read_sdc writes no diagnostics for a valid file");
    expect(contains(result.out, "sdc: " + path.string()), "read_sdc reports its input path");
    expect(contains(result.out, "clocks: 3"), "read_sdc reports all clocks");
    expect(contains(result.out, "clock clk period=10 source=clk"),
           "read_sdc reports a primary clock");
    expect(contains(result.out, "clock groups: 1"), "read_sdc reports clock groups");
    expect(contains(result.out, "group asynchronous"), "read_sdc reports the group kind");
    expect(contains(result.out, "case analysis: 1"), "read_sdc reports case analysis");
    expect(contains(result.out, "case test_mode=1"), "read_sdc reports the case value");
    expect(contains(result.out, "false paths: 1"), "read_sdc reports false paths");
    expect(contains(result.out, "input delays: 1"), "read_sdc reports input delays");
    expect(contains(result.out, "output delays: 1"), "read_sdc reports output delays");
    expect(contains(result.out, "driving cells: 1"), "read_sdc reports driving cells");
    expect(contains(result.out, "loads: 1"), "read_sdc reports loads");
    expect(contains(result.out, "warnings: 0"), "read_sdc reports no warnings");

    std::error_code ignored;
    std::filesystem::remove(path, ignored);
}

void test_error_diagnostic(const shell::Cli& cli) {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "gmax-sdc-cli-invalid.sdc";
    write_file(path, "create_clock -period 10\n");

    const RunResult result = run(cli, {"read_sdc", path.string()});
    expect(result.code == shell::ExitCode::Design, "read_sdc returns design on a parse error");
    expect(result.out.empty(), "read_sdc writes no report on a parse error");
    expect(contains(result.err, "gmax: sdc-invalid-value:"), "read_sdc prints the diagnostic code");
    expect(contains(result.err, "create_clock requires exactly one source object"),
           "read_sdc prints the parser diagnostic");
    expect(contains(result.err, path.string()), "read_sdc names the input file");

    std::error_code ignored;
    std::filesystem::remove(path, ignored);
}

// ADR-012: report-only stays the default and now prints the canonical hash and
// status; attachment requires both options and never targets its own input.
void test_attachment_options(const shell::Cli& cli) {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "gmax-sdc-cli-attach.sdc";
    write_file(path, "create_clock -name clk -period 10 [get_ports clk]\n");

    const RunResult report = run(cli, {"read_sdc", path.string()});
    expect(report.code == shell::ExitCode::Success, "report-only read_sdc succeeds");
    expect(contains(report.out, "constraints status: complete"),
           "report-only prints the constraints status");
    expect(contains(report.out, "constraints hash: "),
           "report-only prints the canonical constraints hash");

    const RunResult half = run(cli, {"read_sdc", path.string(), "--design", "a.dfdb"});
    expect(half.code == shell::ExitCode::Usage, "--design without --output is a usage error");
    expect(contains(half.err, "needs --design and --output together"),
           "the paired-option error says what is missing");

    const std::filesystem::path same_db = "same.dfdb";
    const RunResult same = run_attach(cli, path, same_db, same_db);
    expect(same.code == shell::ExitCode::Usage, "--output equal to --design is a usage error");
    expect(contains(same.err, "never overwrites"),
           "the collision error explains the no-overwrite rule");

    std::error_code ignored;
    std::filesystem::remove(path, ignored);
}

// The attachment path against a real database: a case value on a top-level input
// port is a target that can be applied, so the set stays complete; one on an
// internal net is carried but marks the set incomplete (ADR-012 section 4).
void build_inverter(gmax::db::DesignDb& database) {
    gmax::db::Design design;
    design.top = "top";
    gmax::db::Module module;
    module.name = "top";
    module.inputs = {"a"};
    module.outputs = {"y"};
    gmax::db::Gate gate;
    gate.type = gmax::db::GateType::Not;
    gate.name = "g0";
    gate.fanins = {"a"};
    gate.out = "y";
    module.gates = {gate};
    design.modules = {module};

    database.design() = design;
    expect(database.flatten().ok(), "the attach fixture flattens");
}

void write_inverter_db(const std::filesystem::path& path) {
    gmax::db::DesignDb database;
    build_inverter(database);
    expect(database.save(path.string()).ok(), "the attach fixture saves");
}

void test_attach_to_design(const shell::Cli& cli) {
    const std::filesystem::path directory = std::filesystem::temp_directory_path();
    const std::filesystem::path sdc = directory / "gmax-sdc-attach.sdc";
    const std::filesystem::path input = directory / "gmax-sdc-attach-in.dfdb";
    const std::filesystem::path output = directory / "gmax-sdc-attach-out.dfdb";
    const std::filesystem::path internal = directory / "gmax-sdc-attach-internal.sdc";
    const std::filesystem::path incomplete_out = directory / "gmax-sdc-attach-inc.dfdb";

    write_file(sdc, "set_case_analysis 0 [get_ports a]\n");
    write_inverter_db(input);

    const RunResult result = run_attach(cli, sdc, input, output);
    expect(result.code == shell::ExitCode::Success, "read_sdc attaches to a design");
    expect(contains(result.out, "attached: "), "read_sdc reports the attachment");
    expect(contains(result.out, "constraints status: complete"),
           "a case value on a top-level input port is complete");

    gmax::db::DesignDb loaded;
    expect(gmax::db::DesignDb::load(output.string(), loaded).ok(), "the attached database loads");
    expect(loaded.has_constraints(), "the attached database carries constraints");
    expect(loaded.constraints().case_analysis.size() == 1, "the case value survived attachment");
    expect(loaded.constraints().case_analysis[0].object == "a", "the case object is the port name");

    gmax::db::DesignDb unchanged;
    expect(gmax::db::DesignDb::load(input.string(), unchanged).ok(),
           "the input database still loads");
    expect(!unchanged.has_constraints(), "the input database is not overwritten");

    write_file(internal, "set_case_analysis 0 [get_ports y]\n");
    const RunResult incomplete = run_attach(cli, internal, input, incomplete_out);
    expect(incomplete.code == shell::ExitCode::Success, "an unenforceable target still attaches");
    expect(contains(incomplete.out, "constraints status: incomplete"),
           "an internal target marks the set incomplete rather than being dropped");

    std::error_code ignored;
    for (const std::filesystem::path& path : {sdc, input, output, internal, incomplete_out}) {
        std::filesystem::remove(path, ignored);
    }
}

// A stored set that claims to be complete but names a target this netlist cannot
// enforce. Attachment refuses to produce one, so the fixture is hand-built: the
// guard exists for a database that was edited or written by another tool after
// the set was attached.
void write_inverter_db_with_case(const std::filesystem::path& path,
                                 const std::string& target) {
    gmax::db::DesignDb database;
    build_inverter(database);
    gmax::db::Constraints& constraints = database.constraints();
    constraints.present = true;
    constraints.complete = true;
    constraints.case_analysis = {{target, false}};
    expect(database.save(path.string()).ok(), "the guard fixture saves");
}

// ADR-012 sections 3 and 4: a set that cannot be applied is never graded. Both
// refusals happen before faults are read, so the fixture passes a faults path
// that does not exist: a run that reached read_faults would report a different
// error and prove the guard had not fired.
void test_run_atpg_refuses_unenforceable_case(const shell::Cli& cli) {
    const std::filesystem::path directory = std::filesystem::temp_directory_path();
    const std::filesystem::path unresolved_design = directory / "gmax-atpg-guard-unresolved.dfdb";
    const std::filesystem::path internal_design = directory / "gmax-atpg-guard-internal.dfdb";
    const std::filesystem::path faults = directory / "gmax-atpg-guard.faults";
    const std::filesystem::path unresolved_report = directory / "gmax-atpg-guard-a";
    const std::filesystem::path internal_report = directory / "gmax-atpg-guard-b";

    write_inverter_db_with_case(unresolved_design, "nope");
    write_inverter_db_with_case(internal_design, "y");

    const auto grade = [&](const std::filesystem::path& design,
                          const std::filesystem::path& report) {
        const std::vector<std::string> args = {"run_atpg", design.string(), faults.string(), "0",
                                              "1",        report.string()};
        return run(cli, args);
    };

    const RunResult unresolved = grade(unresolved_design, unresolved_report);
    expect(unresolved.code == shell::ExitCode::Design,
           "an unresolvable case target is a design error");
    expect(contains(unresolved.err, "case-analysis object 'nope' does not resolve to a net"),
           "the unresolvable target is named");
    expect(!std::filesystem::exists(unresolved_report / "coverage.json"),
           "a refused run writes no coverage report");
    expect(!std::filesystem::exists(unresolved_report / "manifest.json"),
           "a refused run writes no manifest");

    const RunResult internal = grade(internal_design, internal_report);
    expect(internal.code == shell::ExitCode::Design,
           "a case target that is not a primary input is a design error");
    expect(contains(internal.err, "case-analysis object 'y' is not a primary input"),
           "the non-input target is named");
    expect(!std::filesystem::exists(internal_report / "coverage.json"),
           "the non-input refusal writes no coverage report");
    expect(!std::filesystem::exists(internal_report / "manifest.json"),
           "the non-input refusal writes no manifest");

    std::error_code ignored;
    for (const std::filesystem::path& path :
         {unresolved_design, internal_design, unresolved_report, internal_report}) {
        std::filesystem::remove_all(path, ignored);
    }
}

}  // namespace

int main() {
    shell::Cli cli("0.0.0");
    std::string error;
    expect(shell::register_commands(cli, error), "the application commands register");
    expect(error.empty(), "command registration leaves no error");
    expect(contains(cli.global_help(), "read_sdc <sdc>"),
           "global help includes the read_sdc command");

    test_valid_file(cli);
    test_error_diagnostic(cli);
    test_attachment_options(cli);
    test_attach_to_design(cli);
    test_run_atpg_refuses_unenforceable_case(cli);

    if (failures != 0) {
        std::cerr << failures << " SDC CLI check(s) failed" << std::endl;
        return 1;
    }
    std::cout << "SDC CLI checks passed" << std::endl;
    return 0;
}
