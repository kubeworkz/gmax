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

    // A member names a clock, not the port it was defined on. A clock declared
    // with -name leaves its source a port, so the two forms are not the same
    // member and the warning says why.
    constexpr std::string_view kSource = R"SDC(
create_clock -name core_clk -period 10 [get_ports clk]
set_clock_groups -asynchronous -group {clk} -group {core_clk}
)SDC";
    const frontend::SdcResult source = frontend::read_sdc(kSource, "groups-source.sdc");
    expect(source.ok, "a source-only group member does not refuse the file");
    expect(source.diagnostics.size() == 1, "only the source-only member is reported");
    if (source.diagnostics.size() == 1) {
        expect(source.diagnostics[0].code == frontend::DiagnosticCode::SdcClockGroupUnknownMember,
               "a source object is not a clock name");
        expect(source.diagnostics[0].object == "clk", "the warning names the source member");
        expect(contains(source.diagnostics[0].message, "it is a clock source"),
               "the warning says the member is a clock source");
    }
    if (source.constraints.clock_groups.size() == 1) {
        const frontend::SdcClockGroupIssues issues = frontend::sdc_clock_group_issues(
            source.constraints, source.constraints.clock_groups[0]);
        expect(issues.unknown == std::vector<std::string>{"clk"} && issues.conflicting.empty(),
               "the declared name is a member and the source is not");
    }

    // Without -name the clock is named after its source, so the common one-word
    // form needs no separate rule.
    constexpr std::string_view kDefaultName = R"SDC(
create_clock -period 10 [get_ports clk]
create_clock -name core_clk -period 20 [get_ports clk_2]
set_clock_groups -asynchronous -group {clk} -group {core_clk}
)SDC";
    const frontend::SdcResult unnamed = frontend::read_sdc(kDefaultName, "groups-default-name.sdc");
    expect(unnamed.ok, "the default-name form parses");
    expect(unnamed.diagnostics.empty(),
           "a clock without -name is a member under the name of its source");

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

// ADR-012 section 4: a generated clock's -master_clock names a declared clock
// and must agree with the command's -source. An unknown master, a master the
// -source does not lead to, and a clock that names itself are warnings: the
// clock keeps its recorded master, so the status and hash are unchanged.
void test_generated_clock_masters() {
    constexpr std::string_view kLinked = R"SDC(
create_clock -name clk_a -period 10 [get_ports clk_a]
create_generated_clock -name gen_a -source [get_ports clk_a] -master_clock clk_a [get_nets ra]
)SDC";
    const frontend::SdcResult linked = frontend::read_sdc(kLinked, "gen-linked.sdc");
    expect(linked.ok, "a generated clock with a linked master parses");
    expect(linked.diagnostics.empty(), "a linked master warns about nothing");
    if (const frontend::SdcClock* gen_a = linked.constraints.find_clock("gen_a")) {
        const frontend::SdcGeneratedClockIssues issues =
            frontend::sdc_generated_clock_issues(linked.constraints, *gen_a);
        expect(issues.unknown.empty() && issues.conflicting.empty(),
               "a master defined on the -source has no issue");
    }

    // Without -master_clock the -source alone is the link.
    constexpr std::string_view kImplicit = R"SDC(
create_clock -name clk_a -period 10 [get_ports clk_a]
create_generated_clock -name gen_a -source [get_ports clk_a] [get_nets ra]
)SDC";
    const frontend::SdcResult implicit = frontend::read_sdc(kImplicit, "gen-implicit.sdc");
    expect(implicit.diagnostics.empty(), "an implicit single master warns about nothing");

    // -source may name the master clock object rather than the object the master
    // is defined on; the names differ but the link is the same.
    constexpr std::string_view kClockObject = R"SDC(
create_clock -name core_clk -period 10 [get_ports clk]
create_generated_clock -name gen_core -source [get_clocks core_clk] -master_clock core_clk [get_nets ra]
)SDC";
    const frontend::SdcResult clock_object =
        frontend::read_sdc(kClockObject, "gen-clock-object.sdc");
    expect(clock_object.diagnostics.empty(), "a -source naming the master clock object is linked");

    constexpr std::string_view kUnknown = R"SDC(
create_clock -name clk_a -period 10 [get_ports clk_a]
create_generated_clock -name gen_a -source [get_ports clk_a] -master_clock clk_typo [get_nets ra]
)SDC";
    const frontend::SdcResult unknown = frontend::read_sdc(kUnknown, "gen-unknown.sdc");
    expect(unknown.ok, "an unknown master does not refuse the file");
    expect(error_count(unknown) == 0, "an unknown master is not an error");
    expect(unknown.diagnostics.size() == 1, "one unknown master produces one warning");
    if (unknown.diagnostics.size() == 1) {
        const frontend::Diagnostic& diagnostic = unknown.diagnostics[0];
        expect(diagnostic.code == frontend::DiagnosticCode::SdcGeneratedClockUnknownMaster,
               "the finding has the unknown-master code");
        expect(contains(diagnostic.message, "clk_typo"), "the warning names the unknown master");
        expect(contains(diagnostic.message, "gen_a"), "the warning names the generated clock");
        expect(diagnostic.object == "clk_typo", "the warning carries the master as its object");
        expect(diagnostic.source.line == 3, "the warning keeps the command line");
    }
    if (const frontend::SdcClock* gen_a = unknown.constraints.find_clock("gen_a")) {
        expect(gen_a->master_clock == "clk_typo",
               "the unknown master is kept on the generated clock");
        const frontend::SdcGeneratedClockIssues issues =
            frontend::sdc_generated_clock_issues(unknown.constraints, *gen_a);
        expect(issues.unknown == std::vector<std::string>{"clk_typo"} && issues.conflicting.empty(),
               "the issue list names the unknown master");
    }

    // A master that names a clock source is the mistake the group check already
    // explains, so the message says the same thing.
    constexpr std::string_view kSource = R"SDC(
create_clock -name core_clk -period 10 [get_ports clk]
create_generated_clock -name gen_core -source [get_ports clk] -master_clock clk [get_nets ra]
)SDC";
    const frontend::SdcResult source = frontend::read_sdc(kSource, "gen-source.sdc");
    expect(source.diagnostics.size() == 1, "a source-only master is reported once");
    if (source.diagnostics.size() == 1) {
        expect(source.diagnostics[0].code ==
                   frontend::DiagnosticCode::SdcGeneratedClockUnknownMaster,
               "a source object is not a clock name");
        expect(source.diagnostics[0].object == "clk", "the warning names the master");
        expect(contains(source.diagnostics[0].message, "it is a clock source"),
               "the warning says the master is a clock source");
    }

    // The master resolves, but it is defined on another object than -source, so
    // the command names two links at once.
    constexpr std::string_view kConflict = R"SDC(
create_clock -name clk_a -period 10 [get_ports clk_a]
create_clock -name clk_b -period 20 [get_ports clk_b]
create_generated_clock -name gen_a -source [get_ports clk_a] -master_clock clk_b [get_nets ra]
)SDC";
    const frontend::SdcResult conflict = frontend::read_sdc(kConflict, "gen-conflict.sdc");
    expect(conflict.ok, "a conflicting master does not refuse the file");
    expect(conflict.diagnostics.size() == 1, "one conflicting master produces one warning");
    if (conflict.diagnostics.size() == 1) {
        const frontend::Diagnostic& diagnostic = conflict.diagnostics[0];
        expect(diagnostic.code == frontend::DiagnosticCode::SdcGeneratedClockConflictingMaster,
               "the finding has the conflicting-master code");
        expect(contains(diagnostic.message, "-source 'clk_a'") &&
                   contains(diagnostic.message, "'clk_b' is defined on 'clk_b'"),
               "the warning names both ends of the link");
        expect(diagnostic.object == "clk_b", "the warning carries the master as its object");
        expect(diagnostic.source.line == 4, "the warning keeps the command line");
    }

    // A clock that derives from itself has no root.
    constexpr std::string_view kSelf = R"SDC(
create_clock -name clk_a -period 10 [get_ports clk_a]
create_generated_clock -name gen_a -source [get_ports clk_a] -master_clock gen_a [get_nets ra]
)SDC";
    const frontend::SdcResult self = frontend::read_sdc(kSelf, "gen-self.sdc");
    expect(self.diagnostics.size() == 1, "a self-linked clock is reported once");
    if (self.diagnostics.size() == 1) {
        expect(self.diagnostics[0].code ==
                   frontend::DiagnosticCode::SdcGeneratedClockConflictingMaster,
               "a self-link conflicts with the clock itself");
        expect(contains(self.diagnostics[0].message, "names itself"),
               "the warning says the clock names itself");
    }

    // A generated master is a clock like any other: the link is the object that
    // master is defined on, not the object the primary clock came from.
    constexpr std::string_view kChain = R"SDC(
create_clock -name clk_a -period 10 [get_ports clk_a]
create_generated_clock -name gen_a -source [get_ports clk_a] -master_clock clk_a [get_nets ra]
create_generated_clock -name gen_b -source [get_nets ra] -master_clock gen_a [get_nets rb]
)SDC";
    const frontend::SdcResult chain = frontend::read_sdc(kChain, "gen-chain.sdc");
    expect(chain.diagnostics.empty(), "a generated master links through its own source");

    // Both findings in one file are independent, and clocks are reported in
    // declaration order.
    constexpr std::string_view kMixed = R"SDC(
create_clock -name clk_a -period 10 [get_ports clk_a]
create_clock -name clk_b -period 20 [get_ports clk_b]
create_generated_clock -name gen_a -source [get_ports clk_a] -master_clock clk_typo [get_nets ra]
create_generated_clock -name gen_b -source [get_ports clk_b] -master_clock clk_a [get_nets rb]
)SDC";
    const frontend::SdcResult mixed = frontend::read_sdc(kMixed, "gen-mixed.sdc");
    expect(mixed.diagnostics.size() == 2, "unknown and conflicting masters are independent");
    if (mixed.diagnostics.size() == 2) {
        expect(mixed.diagnostics[0].code ==
                       frontend::DiagnosticCode::SdcGeneratedClockUnknownMaster &&
                   mixed.diagnostics[0].object == "clk_typo",
               "the unknown master is reported for the first clock");
        expect(mixed.diagnostics[1].code ==
                       frontend::DiagnosticCode::SdcGeneratedClockConflictingMaster &&
                   mixed.diagnostics[1].object == "clk_a",
               "the conflicting master is reported for the second clock");
    }
    expect(mixed.constraints.clocks.size() == 4, "both generated clocks stay in the set");
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
    test_generated_clock_masters();
    if (failures != 0) {
        std::cerr << failures << " SDC test(s) failed" << std::endl;
        return 1;
    }
    std::cout << "all SDC tests passed" << std::endl;
    return 0;
}
