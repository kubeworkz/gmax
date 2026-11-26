// ADR-013 section 1 and section 3: the profile that declares the capture
// procedure, read into the model slice 1 defined. Library only: no run, no
// refusal pass and no report field is exercised here. Framework-free like the
// other tests.

#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "core/db/constraints.h"
#include "core/dft/capture.h"
#include "core/dft/capture_profile.h"
#include "core/frontend/diagnostics.h"

namespace db = gmax::db;
namespace dft = gmax::dft;
namespace fe = gmax::frontend;

namespace {

int failures = 0;

void expect(const bool ok, const std::string_view what) {
    if (!ok) {
        std::cerr << "FAIL " << what << std::endl;
        ++failures;
    }
}

db::ConstraintClock make_clock(const std::string& name, const double period) {
    db::ConstraintClock clock;
    clock.name = name;
    clock.period = period;
    clock.source = name;
    return clock;
}

db::Constraints make_constraints() {
    db::Constraints constraints;
    constraints.present = true;
    constraints.complete = true;

    db::ConstraintClock fast = make_clock("clk_a", 10.0);
    fast.has_waveform = true;
    fast.rise = 0.0;
    fast.fall = 5.0;
    constraints.clocks = {fast, make_clock("clk_b", 20.0)};
    return constraints;
}

dft::CaptureProfileResult read(const std::string& text, const db::Constraints& constraints) {
    return dft::read_capture_profile(text, constraints, "profile.json");
}

bool has_code(const dft::CaptureProfileResult& result, const fe::DiagnosticCode code) {
    for (const fe::Diagnostic& diagnostic : result.diagnostics) {
        if (diagnostic.code == code) {
            return true;
        }
    }
    return false;
}

const fe::Diagnostic* first(const dft::CaptureProfileResult& result) {
    return result.diagnostics.empty() ? nullptr : &result.diagnostics.front();
}

bool names_object(const dft::CaptureProfileResult& result, const std::string_view where) {
    const fe::Diagnostic* diagnostic = first(result);
    return diagnostic != nullptr && diagnostic->object == where;
}

const std::string absent_capture = R"({"scan": {"style": "mux_d"}})";
const std::string null_capture = R"({"capture": null})";

const std::string simultaneous_profile = R"({
  "capture": {
    "kind": "simultaneous",
    "cycles": [{"groups": [{"clocks": ["clk_b", "clk_a"]}]}]
  }
})";

const std::string grouped_profile = R"({
  "capture": {
    "kind": "grouped",
    "cycles": [{"groups": [
      {"name": "fast", "clocks": ["clk_a"], "edge": 0},
      {"name": "slow", "clocks": ["clk_b"], "edge": 20}
    ]}]
  }
})";

const std::string grouped_default_edges = R"({
  "capture": {
    "kind": "grouped",
    "cycles": [{"groups": [{"clocks": ["clk_a"]}, {"clocks": ["clk_b"]}]}]
  }
})";

const std::string sequential_profile = R"({
  "capture": {
    "kind": "sequential",
    "cycles": [
      {"groups": [{"clocks": ["clk_b"]}]},
      {"groups": [{"clocks": ["clk_a"]}]}
    ]
  }
})";

void test_absent_capture() {
    const db::Constraints constraints = make_constraints();

    const dft::CaptureProfileResult none = read(absent_capture, constraints);
    expect(none.ok, "a profile without capture is readable");
    expect(!none.declared, "a profile without capture declares nothing");
    expect(none.procedure.cycles.empty(), "no capture means no cycles");

    const dft::CaptureProfileResult null = read(null_capture, constraints);
    expect(null.ok && !null.declared, "a null capture declares nothing");
}

void test_malformed_json() {
    const dft::CaptureProfileResult result = read(R"({"capture": )", make_constraints());
    expect(!result.ok, "truncated JSON is refused");
    expect(has_code(result, fe::DiagnosticCode::InvalidJson), "the finding is invalid-json");
    const fe::Diagnostic* diagnostic = first(result);
    expect(diagnostic != nullptr && diagnostic->source.file == "profile.json",
           "the finding names the source");
    expect(diagnostic != nullptr && diagnostic->message.find("line") != std::string::npos,
           "the JSON reader reports a position");

    const dft::CaptureProfileResult array = read("[]", make_constraints());
    expect(!array.ok && has_code(array, fe::DiagnosticCode::CaptureProfileSyntax),
           "a non-object profile is refused");
}

void test_unknown_kind() {
    const dft::CaptureProfileResult result =
        read(R"({"capture": {"kind": "pulse", "cycles": []}})", make_constraints());
    expect(!result.ok, "an unknown kind is refused");
    expect(has_code(result, fe::DiagnosticCode::CaptureProfileInvalid), "the finding is invalid");
    expect(names_object(result, "capture.kind"), "the finding names the kind key");
    expect(result.declared, "a refused capture still counts as declared");
}

void test_simultaneous() {
    const dft::CaptureProfileResult result = read(simultaneous_profile, make_constraints());
    expect(result.ok, "a simultaneous procedure parses");
    expect(result.declared, "the procedure is declared");
    expect(!result.procedure.derived, "a declared procedure is not derived");
    expect(result.procedure.kind == dft::CaptureKind::Simultaneous, "the kind is kept");
    expect(result.procedure.cycles.size() == 1, "simultaneous is one cycle");
    if (result.procedure.cycles.size() == 1 && result.procedure.cycles[0].groups.size() == 1) {
        const dft::CaptureTimingSet& timing = result.procedure.cycles[0].groups[0].timing;
        expect(timing.clocks == (std::vector<std::string>{"clk_a", "clk_b"}),
               "clocks are sorted in the timing set");
        expect(timing.period == 10.0, "the bytewise smallest clock sets the period");
        expect(timing.edge == 0.0, "a single group sits at edge zero");
        expect(result.procedure.cycles[0].groups[0].name.empty(),
               "a group without a name keeps an empty one");
    } else {
        expect(false, "simultaneous is one cycle with one group");
    }
}

void test_grouped() {
    const db::Constraints constraints = make_constraints();
    const dft::CaptureProfileResult result = read(grouped_profile, constraints);
    expect(result.ok, "a grouped procedure parses");
    expect(result.procedure.kind == dft::CaptureKind::Grouped, "the kind is kept");
    expect(result.procedure.cycles.size() == 1, "grouped is one cycle");
    if (result.procedure.cycles.size() == 1 && result.procedure.cycles[0].groups.size() == 2) {
        const dft::CaptureCycle& cycle = result.procedure.cycles[0];
        expect(cycle.groups[0].name == "fast" && cycle.groups[1].name == "slow",
               "group names are kept");
        expect(cycle.groups[0].timing.edge == 0.0, "the first explicit edge is kept");
        expect(cycle.groups[1].timing.edge == 20.0, "the second explicit edge is kept");
        expect(cycle.groups[1].timing.period == 20.0, "each group takes its own clock period");
    } else {
        expect(false, "grouped is one cycle with two groups");
    }

    const dft::CaptureProfileResult defaults = read(grouped_default_edges, constraints);
    expect(defaults.ok, "a grouped procedure without edges parses");
    if (defaults.ok && defaults.procedure.cycles.size() == 1 &&
        defaults.procedure.cycles[0].groups.size() == 2) {
        const dft::CaptureCycle& cycle = defaults.procedure.cycles[0];
        expect(cycle.groups[0].timing.edge == 0.0, "the default layout starts at zero");
        expect(cycle.groups[1].timing.edge == 20.0,
               "the default layout is one slowest period apart");
    } else {
        expect(false, "the default layout has two groups to place");
    }

    // A group label is diagnostics, not identity: the same procedure written
    // without labels keeps its hash (TBD-06 draft).
    const dft::CaptureProfileResult unnamed = read(R"({
      "capture": {"kind": "grouped", "cycles": [{"groups": [
        {"clocks": ["clk_a"], "edge": 0},
        {"clocks": ["clk_b"], "edge": 20}
      ]}]}
    })",
                                                   constraints);
    expect(unnamed.ok, "the same procedure without labels parses");
    expect(unnamed.procedure.cycles.size() == 1 && unnamed.procedure.cycles[0].groups.size() == 2,
           "the unlabelled procedure keeps its groups");
    expect(unnamed.procedure.cycles[0].groups[0].name.empty(),
           "an unlabelled group keeps an empty name");
    expect(dft::capture_hash(unnamed.procedure) == dft::capture_hash(result.procedure),
           "a group label is not part of the capture hash");
}

void test_grouped_errors() {
    const dft::CaptureProfileResult mixed = read(R"({
      "capture": {"kind": "grouped", "cycles": [{"groups": [
        {"clocks": ["clk_a"], "edge": 0},
        {"clocks": ["clk_b"]}
      ]}]}
    })",
                                                 make_constraints());
    expect(!mixed.ok, "a half-specified edge layout is refused");
    expect(has_code(mixed, fe::DiagnosticCode::CaptureProfileInvalid), "the finding is invalid");
    expect(names_object(mixed, "capture.cycles[0]"), "the finding names the cycle");

    const dft::CaptureProfileResult single = read(R"({
      "capture": {"kind": "grouped", "cycles": [{"groups": [{"clocks": ["clk_a"]}]}]}
    })",
                                                  make_constraints());
    expect(!single.ok, "grouped with one group is refused");

    const dft::CaptureProfileResult negative = read(R"({
      "capture": {"kind": "grouped", "cycles": [{"groups": [
        {"clocks": ["clk_a"], "edge": -1},
        {"clocks": ["clk_b"], "edge": 20}
      ]}]}
    })",
                                                    make_constraints());
    expect(!negative.ok, "a negative edge is refused");
    expect(names_object(negative, "capture.cycles[0].groups[0].edge"),
           "the finding names the edge");
}

void test_sequential() {
    const dft::CaptureProfileResult result = read(sequential_profile, make_constraints());
    expect(result.ok, "a sequential procedure parses");
    expect(result.procedure.kind == dft::CaptureKind::Sequential, "the kind is kept");
    expect(result.procedure.cycles.size() == 2, "sequential keeps its cycles");
    if (result.procedure.cycles.size() == 2) {
        expect(result.procedure.cycles[0].groups.size() == 1, "each cycle pulses one group");
        expect(result.procedure.cycles[0].groups[0].timing.clocks ==
                   (std::vector<std::string>{"clk_b"}),
               "cycle order is the profile's order, not clock order");
        expect(result.procedure.cycles[1].groups[0].timing.clocks ==
                   (std::vector<std::string>{"clk_a"}),
               "the second cycle keeps its clock");
        expect(result.procedure.cycles[0].groups[0].timing.edge == 0.0,
               "each cycle's single group starts at zero");
    }

    const dft::CaptureProfileResult one = read(R"({
      "capture": {"kind": "sequential", "cycles": [{"groups": [{"clocks": ["clk_a"]}]}]}
    })",
                                               make_constraints());
    expect(!one.ok, "sequential with one cycle is refused");

    const dft::CaptureProfileResult wide = read(R"({
      "capture": {"kind": "sequential", "cycles": [
        {"groups": [{"clocks": ["clk_a"]}, {"clocks": ["clk_b"]}]},
        {"groups": [{"clocks": ["clk_a"]}]}
      ]}
    })",
                                                make_constraints());
    expect(!wide.ok, "sequential with two groups in a cycle is refused");
    expect(names_object(wide, "capture.cycles[0]"), "the finding names the cycle");
}

void test_clock_errors() {
    const dft::CaptureProfileResult unknown = read(R"({
      "capture": {"kind": "simultaneous", "cycles": [{"groups": [
        {"clocks": ["clk_a", "clk_x"]}
      ]}]}
    })",
                                                   make_constraints());
    expect(!unknown.ok, "an undeclared clock is refused");
    expect(has_code(unknown, fe::DiagnosticCode::CaptureProfileUnknownClock),
           "the finding is unknown-clock");
    expect(names_object(unknown, "capture.cycles[0].groups[0].clocks[1]"),
           "the finding names the clock entry");

    const dft::CaptureProfileResult empty = read(R"({
      "capture": {"kind": "simultaneous", "cycles": [{"groups": [{"clocks": []}]}]}
    })",
                                                 make_constraints());
    expect(!empty.ok, "a group with no clock is refused");
    expect(has_code(empty, fe::DiagnosticCode::CaptureProfileInvalid), "the finding is invalid");
}

void test_repeated_clock_accepted() {
    // Section 4 condition 4 is a run-level refusal; the parser keeps the repeat
    // so the procedure it refuses is the one the profile wrote.
    const dft::CaptureProfileResult result = read(R"({
      "capture": {"kind": "sequential", "cycles": [
        {"groups": [{"clocks": ["clk_a"]}]},
        {"groups": [{"clocks": ["clk_a"]}]}
      ]}
    })",
                                                  make_constraints());
    expect(result.ok, "a repeated clock parses");
    expect(result.procedure.cycles.size() == 2, "both cycles are kept");
}

void test_unknown_keys_ignored() {
    const dft::CaptureProfileResult result = read(R"({
      "capture": {
        "kind": "simultaneous",
        "comment": "newer profile",
        "cycles": [{"groups": [{"clocks": ["clk_a"], "note": "fast"}]}]
      }
    })",
                                                  make_constraints());
    expect(result.ok, "unknown keys do not fail the reader");
}

void test_identity() {
    const db::Constraints constraints = make_constraints();
    const std::uint32_t before = db::constraints_hash(constraints);
    const dft::CaptureProfileResult result = read(sequential_profile, constraints);
    expect(result.ok, "the sequential profile parses for the identity check");
    expect(db::constraints_hash(constraints) == before, "reading a profile changes no hash");
    expect(constraints == make_constraints(), "reading a profile mutates no constraint");

    // `derived` is provenance, so a declared procedure identical to the derived
    // one has the same capture hash (section 6).
    const dft::CaptureProcedure derived = dft::resolve_capture_procedure(constraints);
    const dft::CaptureProfileResult same = read(R"({
      "capture": {"kind": "sequential", "cycles": [
        {"groups": [{"clocks": ["clk_a"]}]},
        {"groups": [{"clocks": ["clk_b"]}]}
      ]}
    })",
                                                constraints);
    expect(same.ok, "the derived-equivalent profile parses");
    expect(dft::capture_hash(same.procedure) == dft::capture_hash(derived),
           "provenance is not part of the identity");
    expect(same.procedure != derived, "the procedures differ only in provenance");
}

}  // namespace

int main() {
    test_absent_capture();
    test_malformed_json();
    test_unknown_kind();
    test_simultaneous();
    test_grouped();
    test_grouped_errors();
    test_sequential();
    test_clock_errors();
    test_repeated_clock_accepted();
    test_unknown_keys_ignored();
    test_identity();

    if (failures != 0) {
        std::cerr << failures << " failure(s)" << std::endl;
        return 1;
    }
    return 0;
}
