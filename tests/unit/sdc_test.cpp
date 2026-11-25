// WP-110: restricted SDC parsing, clock/generated-clock relationships, clock
// groups, test-mode case analysis, capture false paths, and unsupported-command
// warnings. Framework-free like the other tests.

#include <cstddef>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "core/frontend/diagnostics.h"
#include "core/frontend/sdc.h"

namespace frontend = gmax::frontend;

namespace {

int failures = 0;

void expect(bool ok, std::string_view what) {
    if (!ok) {
        std::cerr << "FAIL " << what << std::endl;
        ++failures;
    }
}

bool contains(std::string_view text, std::string_view needle) {
    return text.find(needle) != std::string_view::npos;
}

std::size_t error_count(const frontend::SdcResult& result) {
    std::size_t count = 0;
    for (const frontend::Diagnostic& diagnostic : result.diagnostics) {
        if (diagnostic.is_error()) {
            ++count;
        }
    }
    return count;
}

void test_full_fixture() {
    constexpr std::string_view kSdc = R"SDC(
# synthetic G100-like fixture; no chip data
create_clock -name clk_a -period 10 -waveform {0 5} [get_ports clk_a]
create_clock -name clk_b -period 12 [get_ports {clk_b}]
create_generated_clock -name clk_div -source [get_ports clk_a] \
  -divide_by 2 [get_pins u_div/Q]
set_clock_groups -asynchronous -group {clk_a clk_div} -group {clk_b}
set_false_path -from [get_clocks clk_a] -to [get_clocks clk_b]
set_case_analysis 1 [get_ports test_mode]
set_input_delay -clock [get_clocks clk_a] 2.5 [get_ports data_in]
set_output_delay -clock [get_clocks clk_a] -max 1.0 [get_ports data_out]
set_driving_cell -lib_cell BUF [get_ports data_in]
set_load 0.5 [get_ports data_out]
)SDC";

    const frontend::SdcResult result = frontend::read_sdc(kSdc, "fixture.sdc");
    expect(result.ok, "the synthetic fixture parses");
    expect(error_count(result) == 0, "the synthetic fixture has no errors");
    expect(result.diagnostics.empty(), "the synthetic fixture has no warnings");

    const frontend::SdcConstraints& c = result.constraints;
    expect(c.clocks.size() == 3, "three clocks are recorded");
    const frontend::SdcClock* clk_a = c.find_clock("clk_a");
    expect(clk_a != nullptr, "clk_a is present");
    if (clk_a != nullptr) {
        expect(clk_a->period == 10.0, "clk_a keeps its period");
        expect(clk_a->has_waveform && clk_a->rise == 0.0 && clk_a->fall == 5.0,
               "clk_a keeps its waveform");
        expect(clk_a->source == "clk_a" && !clk_a->generated, "clk_a is a primary clock");
    }
    const frontend::SdcClock* clk_b = c.find_clock("clk_b");
    expect(clk_b != nullptr && clk_b->period == 12.0, "clk_b keeps its period");
    const frontend::SdcClock* clk_div = c.find_clock("clk_div");
    expect(clk_div != nullptr && clk_div->generated, "clk_div is generated");
    if (clk_div != nullptr) {
        expect(clk_div->source == "u_div/Q", "the generated clock keeps its target");
        expect(clk_div->source_pin == "clk_a", "the generated clock keeps its source");
        expect(clk_div->divide_by == 2, "the generated clock keeps divide_by");
    }

    expect(c.clock_groups.size() == 1, "one clock-group command is recorded");
    if (!c.clock_groups.empty()) {
        const frontend::SdcClockGroup& group = c.clock_groups.front();
        expect(group.kind == frontend::SdcClockGroupKind::Asynchronous,
               "the group is asynchronous");
        expect(group.groups.size() == 2, "the command has two groups");
        expect(group.groups[0].size() == 2 && group.groups[0][0] == "clk_a" &&
                   group.groups[0][1] == "clk_div",
               "the first group keeps both clocks");
        expect(group.groups[1].size() == 1 && group.groups[1][0] == "clk_b",
               "the second group keeps its clock");
    }

    expect(c.false_paths.size() == 1, "one false path is recorded");
    if (!c.false_paths.empty()) {
        expect(c.false_paths[0].from == std::vector<std::string>{"clk_a"},
               "the false path keeps its from clock");
        expect(c.false_paths[0].to == std::vector<std::string>{"clk_b"},
               "the false path keeps its to clock");
        expect(c.false_paths[0].through.empty(), "the false path has no through list");
    }

    expect(c.case_analysis.size() == 1, "one case analysis is recorded");
    const frontend::SdcCaseAnalysis* test_mode = c.find_case("test_mode");
    expect(test_mode != nullptr && test_mode->value, "test_mode is constrained to 1");

    expect(c.delays.size() == 2, "two delays are recorded");
    if (c.delays.size() == 2) {
        expect(c.delays[0].kind == frontend::SdcDelayKind::Input &&
                   c.delays[0].object == "data_in" && c.delays[0].value == 2.5,
               "the input delay is recorded");
        expect(c.delays[1].kind == frontend::SdcDelayKind::Output &&
                   c.delays[1].object == "data_out" && c.delays[1].value == 1.0 &&
                   c.delays[1].max && !c.delays[1].min,
               "the output delay and its max flag are recorded");
    }
    expect(c.driving_cells.size() == 1 && c.driving_cells[0].object == "data_in" &&
               c.driving_cells[0].lib_cell == "BUF",
           "the driving cell is recorded");
    expect(c.loads.size() == 1 && c.loads[0].object == "data_out" && c.loads[0].value == 0.5,
           "the load is recorded");
}

void test_unsupported_commands() {
    constexpr std::string_view kSdc = R"SDC(
set_operating_conditions slow
set_max_delay 5 [get_ports foo]
)SDC";
    const frontend::SdcResult result = frontend::read_sdc(kSdc, "warnings.sdc");
    expect(result.ok, "unsupported commands do not fail the parse");
    expect(result.diagnostics.size() == 2, "both unsupported commands produce warnings");
    if (result.diagnostics.size() == 2) {
        expect(result.diagnostics[0].code == frontend::DiagnosticCode::SdcUnsupportedCommand,
               "the first diagnostic is an unsupported-command warning");
        expect(!result.diagnostics[0].is_error(), "the unsupported command is a warning");
        expect(contains(result.diagnostics[0].message, "set_operating_conditions"),
               "the warning names the command");
        expect(result.diagnostics[0].source.line == 2, "the warning keeps the command line");
        expect(result.diagnostics[1].source.line == 3, "the second warning keeps its line");
    }
}

void test_malformed_commands() {
    {
        const frontend::SdcResult result =
            frontend::read_sdc("create_clock -period 10\n", "missing-source.sdc");
        expect(!result.ok, "a clock without a source fails");
        expect(error_count(result) == 1, "the missing source has one error");
    }
    {
        const frontend::SdcResult result =
            frontend::read_sdc("set_clock_groups -asynchronous -group {clk_a}\n", "one-group.sdc");
        expect(!result.ok, "one clock group fails");
        expect(!result.diagnostics.empty() &&
                   result.diagnostics[0].code == frontend::DiagnosticCode::SdcInvalidValue,
               "the one-group error has the expected code");
    }
    {
        const frontend::SdcResult result =
            frontend::read_sdc("set_case_analysis 1 [get_ports test_mode]\n"
                               "set_case_analysis 0 [get_ports test_mode]\n",
                               "case-conflict.sdc");
        expect(!result.ok, "conflicting case analysis fails");
        expect(!result.diagnostics.empty() &&
                   result.diagnostics.back().code == frontend::DiagnosticCode::SdcInvalidValue,
               "the conflict has the expected code");
    }
    {
        const frontend::SdcResult result =
            frontend::read_sdc("create_clock -period 10 {clk_a\n", "bad-brace.sdc");
        expect(!result.ok, "an unterminated brace fails");
        expect(!result.diagnostics.empty() &&
                   result.diagnostics.back().code == frontend::DiagnosticCode::SdcSyntax,
               "the brace error has the syntax code");
    }
}

void test_comments_and_continuation() {
    constexpr std::string_view kSdc = R"SDC(
# a continuation keeps the command together
create_clock -name clk_c \
  -period 4 [get_ports clk_c]
set_case_analysis 0 [get_ports "test_mode"]
)SDC";
    const frontend::SdcResult result = frontend::read_sdc(kSdc, "continuation.sdc");
    expect(result.ok, "a continued command parses");
    expect(result.constraints.clocks.size() == 1 &&
               result.constraints.find_clock("clk_c") != nullptr,
           "the continued clock is recorded");
    expect(result.constraints.find_case("test_mode") != nullptr,
           "a quoted object name is recorded");
}

void test_negative_delays() {
    constexpr std::string_view kSdc = R"SDC(
create_clock -name clk -period 10 [get_ports clk]
set_input_delay -clock [get_clocks clk] -0.5 [get_ports data_in]
set_output_delay -clock [get_clocks clk] -1.25 [get_ports data_out]
)SDC";
    const frontend::SdcResult result = frontend::read_sdc(kSdc, "negative-delays.sdc");
    expect(result.ok, "negative delay values parse");
    expect(result.diagnostics.empty(), "negative delay values produce no warnings");
    expect(result.constraints.delays.size() == 2, "both negative delays are recorded");
    if (result.constraints.delays.size() == 2) {
        expect(result.constraints.delays[0].kind == frontend::SdcDelayKind::Input &&
                   result.constraints.delays[0].object == "data_in" &&
                   result.constraints.delays[0].value == -0.5,
               "a negative input delay is a value, not an option");
        expect(result.constraints.delays[1].kind == frontend::SdcDelayKind::Output &&
                   result.constraints.delays[1].object == "data_out" &&
                   result.constraints.delays[1].value == -1.25,
               "a negative output delay is a value, not an option");
    }
}

void test_crlf_continuations() {
    const std::string kSdc = "create_clock -name clk_a -period 10\\"
                             "\r\n"
                             "  [get_ports clk_a]\r\n"
                             "create_clock -name clk_b \\"
                             "\r\n"
                             "  -period 20 [get_ports clk_b]\r\n";
    const frontend::SdcResult result = frontend::read_sdc(kSdc, "crlf-continuation.sdc");
    expect(result.ok, "CRLF continuations keep commands together");
    expect(result.diagnostics.empty(), "CRLF continuations produce no warnings");
    expect(result.constraints.clocks.size() == 2, "both continued clocks are recorded");
    const frontend::SdcClock* clk_a = result.constraints.find_clock("clk_a");
    const frontend::SdcClock* clk_b = result.constraints.find_clock("clk_b");
    expect(clk_a != nullptr && clk_a->period == 10.0,
           "a continuation adjacent to a word splits the token");
    expect(clk_b != nullptr && clk_b->period == 20.0,
           "a continuation after whitespace keeps the option stream");
}

void test_braced_continuations() {
    const std::string lf = "create_clock -name clk_a -period 10 [get_ports clk_a]\n"
                           "create_clock -name clk_b -period 10 [get_ports clk_b]\n"
                           "create_clock -name clk_c -period 10 [get_ports clk_c]\n"
                           "set_clock_groups -asynchronous -group {clk_a\\\n"
                           "  clk_b} -group {clk_c}\n";
    const std::string crlf = "create_clock -name clk_a -period 10 [get_ports clk_a]\r\n"
                             "create_clock -name clk_b -period 10 [get_ports clk_b]\r\n"
                             "create_clock -name clk_c -period 10 [get_ports clk_c]\r\n"
                             "set_clock_groups -asynchronous -group {clk_a\\\r\n"
                             "  clk_b} -group {clk_c}\r\n";

    struct Case {
        const char* label;
        const std::string* sdc;
    };
    const Case cases[] = {{"LF", &lf}, {"CRLF", &crlf}};
    for (const Case& test : cases) {
        const frontend::SdcResult result = frontend::read_sdc(*test.sdc, test.label);
        expect(result.ok, std::string(test.label) + " braced continuation parses");
        expect(result.diagnostics.empty(),
               std::string(test.label) + " braced continuation has no warnings");
        expect(result.constraints.clock_groups.size() == 1,
               std::string(test.label) + " records one clock group");
        if (result.constraints.clock_groups.size() != 1) {
            continue;
        }
        const frontend::SdcClockGroup& group = result.constraints.clock_groups.front();
        expect(group.groups.size() == 2, std::string(test.label) + " records both braced groups");
        if (group.groups.size() == 2) {
            expect(group.groups[0] == std::vector<std::string>{"clk_a", "clk_b"},
                   std::string(test.label) + " splits the continued braced list into clocks");
            expect(group.groups[1] == std::vector<std::string>{"clk_c"},
                   std::string(test.label) + " keeps the second group intact");
        }
    }
}

// ADR-012 section 4: set_clock_groups members name clocks, so membership is
// checked against the declared clock names once the whole file is read. An
// unknown member and a clock in two -groups of one command warn; neither
// refuses the file nor drops the member from the retained set.
void test_clock_group_membership() {
    constexpr std::string_view kGood = R"SDC(
create_clock -name clk_a -period 10 [get_ports clk_a]
create_clock -name clk_b -period 20 [get_ports clk_b]
set_clock_groups -asynchronous -group {clk_a} -group {clk_b}
)SDC";
    const frontend::SdcResult good = frontend::read_sdc(kGood, "groups-good.sdc");
    expect(good.ok, "groups of declared clocks parse");
    expect(good.diagnostics.empty(), "groups of declared clocks warn about nothing");
    if (good.constraints.clock_groups.size() == 1) {
        const frontend::SdcClockGroupIssues issues =
            frontend::sdc_clock_group_issues(good.constraints, good.constraints.clock_groups[0]);
        expect(issues.unknown.empty() && issues.conflicting.empty(),
               "a command naming declared clocks has no membership issue");
    }

    constexpr std::string_view kUnknown = R"SDC(
create_clock -name clk_a -period 10 [get_ports clk_a]
create_clock -name clk_b -period 20 [get_ports clk_b]
set_clock_groups -asynchronous -group {clk_a clk_typo} -group {clk_b}
)SDC";
    const frontend::SdcResult unknown = frontend::read_sdc(kUnknown, "groups-unknown.sdc");
    expect(unknown.ok, "an unknown group member does not refuse the file");
    expect(error_count(unknown) == 0, "an unknown group member is not an error");
    expect(unknown.diagnostics.size() == 1, "one unknown member produces one warning");
    if (unknown.diagnostics.size() == 1) {
        expect(unknown.diagnostics[0].code == frontend::DiagnosticCode::SdcClockGroupUnknownMember,
               "the finding has the unknown-member code");
        expect(contains(unknown.diagnostics[0].message, "clk_typo"),
               "the warning names the unknown member");
        expect(unknown.diagnostics[0].object == "clk_typo",
               "the warning carries the member as its object");
        expect(unknown.diagnostics[0].source.line == 4, "the warning keeps the command line");
    }
    if (unknown.constraints.clock_groups.size() == 1) {
        expect(unknown.constraints.clock_groups[0].groups[0] ==
                   std::vector<std::string>{"clk_a", "clk_typo"},
               "the unknown member stays in the recorded group");
    }

    constexpr std::string_view kConflict = R"SDC(
create_clock -name clk_a -period 10 [get_ports clk_a]
create_clock -name clk_b -period 20 [get_ports clk_b]
set_clock_groups -asynchronous -group {clk_a} -group {clk_a clk_b}
)SDC";
    const frontend::SdcResult conflict = frontend::read_sdc(kConflict, "groups-conflict.sdc");
    expect(conflict.ok, "a clock in two groups does not refuse the file");
    expect(conflict.diagnostics.size() == 1, "one conflicting clock produces one warning");
    if (conflict.diagnostics.size() == 1) {
        expect(conflict.diagnostics[0].code ==
                   frontend::DiagnosticCode::SdcClockGroupConflictingMember,
               "the finding has the conflicting-member code");
        expect(contains(conflict.diagnostics[0].message, "more than one -group"),
               "the warning says the clock is in two groups");
        expect(conflict.diagnostics[0].object == "clk_a",
               "the warning carries the conflicting clock");
    }

    // One command can carry both findings. A member repeated across groups is
    // reported once, and the issue lists come back sorted.
    constexpr std::string_view kBoth = R"SDC(
create_clock -name clk_a -period 10 [get_ports clk_a]
set_clock_groups -asynchronous -group {clk_a clk_zz} -group {clk_a clk_zz} -group {clk_zz}
)SDC";
    const frontend::SdcResult both = frontend::read_sdc(kBoth, "groups-both.sdc");
    expect(both.ok, "mixed membership findings do not refuse the file");
    expect(both.diagnostics.size() == 3,
           "one unknown member and two conflicting clocks produce three warnings");
    if (both.constraints.clock_groups.size() == 1) {
        const frontend::SdcClockGroupIssues issues =
            frontend::sdc_clock_group_issues(both.constraints, both.constraints.clock_groups[0]);
        expect(issues.unknown == std::vector<std::string>{"clk_zz"},
               "the unknown list names the member once");
        expect(issues.conflicting == std::vector<std::string>{"clk_a", "clk_zz"},
               "the conflicting list is sorted and deduplicated");
    }
}

}  // namespace

int main() {
    test_full_fixture();
    test_unsupported_commands();
    test_malformed_commands();
    test_comments_and_continuation();
    test_negative_delays();
    test_crlf_continuations();
    test_braced_continuations();
    test_clock_group_membership();
    if (failures != 0) {
        std::cerr << failures << " SDC test(s) failed" << std::endl;
        return 1;
    }
    std::cout << "all SDC tests passed" << std::endl;
    return 0;
}
