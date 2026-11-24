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

    if (failures != 0) {
        std::cerr << failures << " SDC CLI check(s) failed" << std::endl;
        return 1;
    }
    std::cout << "SDC CLI checks passed" << std::endl;
    return 0;
}
