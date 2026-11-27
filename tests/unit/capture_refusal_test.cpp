// ADR-013 section 4: the refusal findings, their contract codes, their order and
// the block they render. Library only; the run wiring, exit status and the
// absence of artifacts are asserted by the process tests.

#include <cstdint>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/db/constraints.h"
#include "core/dft/capture.h"
#include "core/dft/capture_profile.h"
#include "core/dft/capture_refusal.h"

namespace db = gmax::db;
namespace dft = gmax::dft;

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

// clk_a and clk_b sit in one asynchronous row, one group each, so the SDC keeps
// them apart. clk_a has a waveform and clk_b has none.
db::Constraints make_constraints() {
    db::Constraints constraints;
    constraints.present = true;
    constraints.complete = true;

    db::ConstraintClock fast = make_clock("clk_a", 10.0);
    fast.has_waveform = true;
    fast.rise = 0.0;
    fast.fall = 5.0;
    constraints.clocks = {fast, make_clock("clk_b", 20.0)};

    db::ConstraintClockGroup row;
    row.kind = db::ClockGroupKind::Asynchronous;
    row.name = "async";
    row.groups = {{"clk_a"}, {"clk_b"}};
    constraints.clock_groups = {row};
    return constraints;
}

std::vector<db::ClockGateCount> gates(const std::size_t a, const std::size_t b) {
    return {{"clk_a", a}, {"clk_b", b}};
}

dft::CaptureGroup make_group(const db::Constraints& constraints,
                             const std::vector<std::string>& clocks, const double edge) {
    dft::CaptureGroup group;
    group.timing = dft::capture_timing_set(constraints, clocks, edge);
    return group;
}

dft::CaptureCycle cycle_of(std::vector<dft::CaptureGroup> groups) {
    dft::CaptureCycle cycle;
    cycle.groups = std::move(groups);
    return cycle;
}

dft::CaptureProcedure make_procedure(const dft::CaptureKind kind,
                                     std::vector<dft::CaptureCycle> cycles) {
    dft::CaptureProcedure procedure;
    procedure.kind = kind;
    procedure.derived = false;
    procedure.cycles = std::move(cycles);
    return procedure;
}

bool has_code(const std::vector<dft::CaptureRefusal>& refusals,
              const dft::CaptureRefusalCode code) {
    for (const dft::CaptureRefusal& refusal : refusals) {
        if (refusal.code == code) {
            return true;
        }
    }
    return false;
}

std::string detail_of(const std::vector<dft::CaptureRefusal>& refusals,
                      const dft::CaptureRefusalCode code) {
    for (const dft::CaptureRefusal& refusal : refusals) {
        if (refusal.code == code) {
            return refusal.detail;
        }
    }
    return std::string();
}

bool contains(const std::string& text, const std::string_view needle) {
    return text.find(needle) != std::string::npos;
}

void test_code_names() {
    using dft::CaptureRefusalCode;
    expect(dft::capture_refusal_code_name(CaptureRefusalCode::SimultaneousAcrossGroups) ==
               "simultaneous-across-groups",
           "condition 1 keeps its kebab code");
    expect(dft::capture_refusal_code_name(CaptureRefusalCode::IntermediateState) ==
               "intermediate-state",
           "condition 2 keeps its kebab code");
    expect(dft::capture_refusal_code_name(CaptureRefusalCode::GroupTimingDisagrees) ==
               "group-timing-disagrees",
           "condition 3 keeps its kebab code");
    expect(dft::capture_refusal_code_name(CaptureRefusalCode::ClockRepeated) == "clock-repeated",
           "condition 4 keeps its kebab code");
    expect(dft::capture_refusal_code_name(CaptureRefusalCode::NoCaptureClock) == "no-capture-clock",
           "condition 5 keeps its kebab code");
    expect(dft::capture_refusal_code_name(CaptureRefusalCode::CrossingWithoutLockup) ==
               "crossing-without-lockup",
           "condition 6 keeps its kebab code");
}

void test_clean_procedure() {
    const db::Constraints constraints = make_constraints();
    const dft::CaptureProcedure procedure = dft::resolve_capture_procedure(constraints);
    expect(dft::find_capture_refusals(procedure, constraints, gates(2, 1)).empty(),
           "a procedure the design supports is not refused");
    expect(dft::find_capture_refusals(procedure, constraints, gates(1, 0)).empty(),
           "one dropped clock is not a refusal");
}

void test_simultaneous_across_groups() {
    const db::Constraints constraints = make_constraints();
    // One group holding both clocks while the SDC keeps them apart.
    const dft::CaptureProcedure procedure =
        make_procedure(dft::CaptureKind::Simultaneous,
                       {cycle_of({make_group(constraints, {"clk_a", "clk_b"}, 0.0)})});

    const std::vector<dft::CaptureRefusal> refusals =
        dft::find_capture_refusals(procedure, constraints, gates(1, 1));
    expect(has_code(refusals, dft::CaptureRefusalCode::SimultaneousAcrossGroups),
           "clocks the SDC keeps apart are refused when pulsed together");
    const std::string detail =
        detail_of(refusals, dft::CaptureRefusalCode::SimultaneousAcrossGroups);
    expect(contains(detail, "cycle 0"), "the finding names the cycle");
    expect(contains(detail, "'clk_a' (group 'async')"),
           "the finding names the first clock's group");
    expect(contains(detail, "'clk_b' (group 'async')"),
           "the finding names the second clock's group");
    expect(contains(detail, "keeps apart"), "the finding says why");

    // The same clocks in one declared group are one domain, so no refusal.
    db::Constraints grouped = constraints;
    grouped.clock_groups[0].groups = {{"clk_a", "clk_b"}};
    // These two clocks still disagree on timing, which condition 3 reports; the
    // point here is only that grouping removes condition 1.
    expect(!has_code(dft::find_capture_refusals(procedure, grouped, gates(1, 1)),
                     dft::CaptureRefusalCode::SimultaneousAcrossGroups),
           "clocks the SDC groups together are not simultaneous-across-groups");

    // A clock with no declared group is its own group, so it is apart from every
    // other clock even when neither is grouped.
    db::Constraints ungrouped = constraints;
    ungrouped.clock_groups.clear();
    expect(has_code(dft::find_capture_refusals(procedure, ungrouped, gates(1, 1)),
                    dft::CaptureRefusalCode::SimultaneousAcrossGroups),
           "two ungrouped clocks are never one simultaneous domain");
    const std::string ungrouped_detail =
        detail_of(dft::find_capture_refusals(procedure, ungrouped, gates(1, 1)),
                  dft::CaptureRefusalCode::SimultaneousAcrossGroups);
    expect(contains(ungrouped_detail, "(no declared group)"),
           "an ungrouped clock is reported as having no group");
}

void test_shared_edge() {
    const db::Constraints constraints = make_constraints();
    // Two groups at one edge time is simultaneous capture by timing.
    const dft::CaptureProcedure procedure = make_procedure(
        dft::CaptureKind::Grouped, {cycle_of({make_group(constraints, {"clk_a"}, 0.0),
                                              make_group(constraints, {"clk_b"}, 0.0)})});
    const std::vector<dft::CaptureRefusal> refusals =
        dft::find_capture_refusals(procedure, constraints, gates(1, 1));
    const std::string detail =
        detail_of(refusals, dft::CaptureRefusalCode::SimultaneousAcrossGroups);
    expect(contains(detail, "one edge time"), "two groups at one edge are refused");
    expect(contains(detail, "'clk_a'") && contains(detail, "'clk_b'"),
           "the finding names both clocks");

    // The ordinary grouped layout is one slowest period apart, so it runs.
    const dft::CaptureProcedure ok = make_procedure(
        dft::CaptureKind::Grouped, {cycle_of({make_group(constraints, {"clk_a"}, 0.0),
                                              make_group(constraints, {"clk_b"}, 20.0)})});
    expect(dft::find_capture_refusals(ok, constraints, gates(1, 1)).empty(),
           "groups at different edges are not a refusal");
}

void test_group_timing() {
    const db::Constraints constraints = make_constraints();
    const dft::CaptureProcedure periods =
        make_procedure(dft::CaptureKind::Simultaneous,
                       {cycle_of({make_group(constraints, {"clk_a", "clk_b"}, 0.0)})});
    const std::string period_detail =
        detail_of(dft::find_capture_refusals(periods, constraints, gates(1, 1)),
                  dft::CaptureRefusalCode::GroupTimingDisagrees);
    expect(contains(period_detail, "mixes periods 10 ns (clk_a) and 20 ns (clk_b)"),
           "the finding names both periods and both clocks");
    expect(contains(period_detail, "group [clk_a, clk_b]"),
           "an unnamed group is identified by its clock list");

    // Same period, different waveform: still a disagreement (section 3).
    db::Constraints waveforms = make_constraints();
    db::ConstraintClock slow = make_clock("clk_b", 10.0);
    waveforms.clocks = {waveforms.clocks[0], slow};
    db::ConstraintClock risen = make_clock("clk_c", 10.0);
    risen.has_waveform = true;
    risen.rise = 0.0;
    risen.fall = 10.0;
    waveforms.clocks.push_back(risen);
    const dft::CaptureProcedure mixed =
        make_procedure(dft::CaptureKind::Simultaneous,
                       {cycle_of({make_group(waveforms, {"clk_b", "clk_c"}, 0.0)})});
    const std::string waveform_detail =
        detail_of(dft::find_capture_refusals(mixed, waveforms, gates(1, 1)),
                  dft::CaptureRefusalCode::GroupTimingDisagrees);
    expect(contains(waveform_detail, "mixes waveforms"), "a waveform disagreement is named as one");
    expect(contains(waveform_detail, "clk_c rise 0 ns fall 10 ns"),
           "the finding gives the waveform it saw");
    expect(contains(waveform_detail, "clk_b has no waveform"),
           "the finding says which clock has no waveform");
}

void test_clock_repeated() {
    const db::Constraints constraints = make_constraints();
    const dft::CaptureProcedure across = make_procedure(
        dft::CaptureKind::Sequential, {cycle_of({make_group(constraints, {"clk_a"}, 0.0)}),
                                       cycle_of({make_group(constraints, {"clk_b"}, 0.0)}),
                                       cycle_of({make_group(constraints, {"clk_a"}, 0.0)})});
    const std::string detail =
        detail_of(dft::find_capture_refusals(across, constraints, gates(1, 1)),
                  dft::CaptureRefusalCode::ClockRepeated);
    expect(detail == "'clk_a' appears in cycle 0 and cycle 2",
           "a clock in two cycles names both cycles");

    const dft::CaptureProcedure twice = make_procedure(
        dft::CaptureKind::Sequential, {cycle_of({make_group(constraints, {"clk_a", "clk_a"}, 0.0)}),
                                       cycle_of({make_group(constraints, {"clk_b"}, 0.0)})});
    const std::string twice_detail =
        detail_of(dft::find_capture_refusals(twice, constraints, gates(1, 1)),
                  dft::CaptureRefusalCode::ClockRepeated);
    expect(twice_detail == "'clk_a' appears twice in cycle 0",
           "a clock twice in one cycle says so");

    // One finding per repeated clock, not one per extra occurrence.
    const dft::CaptureProcedure thrice = make_procedure(
        dft::CaptureKind::Sequential, {cycle_of({make_group(constraints, {"clk_a"}, 0.0)}),
                                       cycle_of({make_group(constraints, {"clk_a"}, 0.0)}),
                                       cycle_of({make_group(constraints, {"clk_a"}, 0.0)})});
    const std::vector<dft::CaptureRefusal> thrice_refusals =
        dft::find_capture_refusals(thrice, constraints, gates(1, 1));
    std::size_t count = 0;
    for (const dft::CaptureRefusal& refusal : thrice_refusals) {
        count += refusal.code == dft::CaptureRefusalCode::ClockRepeated ? 1 : 0;
    }
    expect(count == 1, "one finding per repeated clock");
}

void test_no_capture_clock() {
    const db::Constraints constraints = make_constraints();
    const dft::CaptureProcedure derived = dft::resolve_capture_procedure(constraints);
    std::vector<std::string> dropped;

    // The exclusion runs before the refusal, so the procedure the pass sees is the
    // one the run would apply.
    const dft::CaptureProcedure emptied =
        dft::exclude_idle_clocks(derived, constraints, gates(0, 0), dropped);
    const std::vector<dft::CaptureRefusal> refusals =
        dft::find_capture_refusals(emptied, constraints, gates(0, 0));
    expect(refusals.size() == 2, "every dropped clock gets its own finding");
    expect(refusals.size() == 2 && refusals[0].code == dft::CaptureRefusalCode::NoCaptureClock,
           "the findings carry condition 5");
    expect(refusals.size() == 2 &&
               refusals[0].detail == "'clk_a' clocks no state-holding gate and was dropped",
           "the first finding is the first clock in name order");
    expect(refusals.size() == 2 &&
               refusals[1].detail == "'clk_b' clocks no state-holding gate and was dropped",
           "the second finding is the second clock in name order");

    const dft::CaptureProcedure partial =
        dft::exclude_idle_clocks(derived, constraints, gates(0, 3), dropped);
    expect(!partial.cycles.empty(), "the clock that does clock state survives the drop");
    expect(dft::find_capture_refusals(partial, constraints, gates(0, 3)).empty(),
           "one clock that does clock state is enough to run");

    expect(dft::find_capture_refusals(derived, constraints, {}).empty(),
           "without a design view condition 5 says nothing");
}

// A profile file can declare one group holding clocks the SDC keeps apart -- see the
// mixed-group note in capture_profile.h -- so the verdict belongs to condition 1 and
// must not depend on whether section 3 happens to drop a neighbouring clock. Condition
// 1 asks every group of every shape, because a group is one capture edge whether its
// cycle has a second group or not; the drop below only changes the shape, and the
// finding is identical before and after it.
void test_group_spanning_declared_groups() {
    // One period and no waveform, so condition 3 has nothing to disagree about and
    // the only thing separating clk_a from clk_b is the declared group.
    db::ConstraintClockGroup fast;
    fast.kind = db::ClockGroupKind::Asynchronous;
    fast.name = "fast";
    fast.groups = {{"clk_a"}};
    db::ConstraintClockGroup slow;
    slow.kind = db::ClockGroupKind::Asynchronous;
    slow.name = "slow";
    slow.groups = {{"clk_b"}};
    db::ConstraintClockGroup third;
    third.kind = db::ClockGroupKind::Asynchronous;
    third.name = "third";
    third.groups = {{"clk_c"}};

    db::Constraints split;
    split.present = true;
    split.complete = true;
    split.clocks = {make_clock("clk_a", 10.0), make_clock("clk_b", 10.0),
                    make_clock("clk_c", 20.0)};
    split.clock_groups = {fast, slow, third};

    const dft::CaptureProfileResult profile =
        dft::read_capture_profile(R"({"capture": {"kind": "grouped", "cycles": [{"groups": [)"
                                  R"({"name": "mixed", "clocks": ["clk_a", "clk_b"], "edge": 0},)"
                                  R"({"clocks": ["clk_c"], "edge": 20}]}]}})",
                                  split, "capture.json");
    expect(profile.ok && profile.declared,
           "a profile can declare a group spanning declared groups");
    expect(profile.procedure.kind == dft::CaptureKind::Grouped, "the declared shape is grouped");

    const std::string detail =
        "cycle 0 pulses 'clk_a' (group 'fast') and 'clk_b' (group 'slow'), which "
        "set_clock_groups keeps apart";
    const std::vector<dft::CaptureRefusal> declared = dft::find_capture_refusals(
        profile.procedure, split, {{"clk_a", 1}, {"clk_b", 1}, {"clk_c", 1}});
    expect(declared.size() == 1, "the grouped procedure refuses before any drop");
    expect(declared.size() == 1 &&
               declared[0].code == dft::CaptureRefusalCode::SimultaneousAcrossGroups,
           "condition 1 is what refuses the group");
    expect(declared.size() == 1 && declared[0].detail == detail,
           "the finding names the two declared groups of the mixed group");

    std::vector<std::string> dropped;
    const dft::CaptureProcedure simultaneous = dft::exclude_idle_clocks(
        profile.procedure, split, {{"clk_a", 1}, {"clk_b", 1}, {"clk_c", 0}}, dropped);
    expect(dropped == (std::vector<std::string>{"clk_c"}), "only the idle clock is dropped");
    expect(simultaneous.kind == dft::CaptureKind::Simultaneous,
           "the drop still re-derives the shape");
    expect(dft::find_capture_refusals(simultaneous, split,
                                      {{"clk_a", 1}, {"clk_b", 1}, {"clk_c", 0}}) == declared,
           "the drop does not change the verdict or duplicate the finding");
}

void test_order() {
    const db::Constraints constraints = make_constraints();
    // One cycle whose group mixes periods, and a later cycle repeating a clock:
    // condition 3 has to come before condition 4 regardless of which the
    // evaluator happens to find first. That group also holds the two clocks the
    // SDC keeps apart, so condition 1 fires ahead of both: the test pins the
    // order, so it counts three findings and names each position.
    const dft::CaptureProcedure procedure = make_procedure(
        dft::CaptureKind::Sequential, {cycle_of({make_group(constraints, {"clk_a", "clk_b"}, 0.0)}),
                                       cycle_of({make_group(constraints, {"clk_a"}, 0.0)})});
    const std::vector<dft::CaptureRefusal> refusals =
        dft::find_capture_refusals(procedure, constraints, gates(1, 1));
    expect(refusals.size() == 3, "every condition fires");
    expect(refusals.size() == 3 &&
               refusals[0].code == dft::CaptureRefusalCode::SimultaneousAcrossGroups,
           "condition 1 comes first");
    expect(refusals.size() == 3 &&
               refusals[1].code == dft::CaptureRefusalCode::GroupTimingDisagrees,
           "findings are ordered by condition");
    expect(refusals.size() == 3 && refusals[2].code == dft::CaptureRefusalCode::ClockRepeated,
           "the later condition follows");

    const std::vector<dft::CaptureRefusal> again =
        dft::find_capture_refusals(procedure, constraints, gates(1, 1));
    expect(again == refusals, "evaluation is deterministic");
}

void test_render() {
    std::ostringstream empty;
    dft::write_capture_refusals(empty, {});
    expect(empty.str().empty(), "nothing to refuse writes nothing");

    const db::Constraints constraints = make_constraints();
    const dft::CaptureProcedure procedure = make_procedure(
        dft::CaptureKind::Sequential, {cycle_of({make_group(constraints, {"clk_a"}, 0.0)}),
                                       cycle_of({make_group(constraints, {"clk_a"}, 0.0)})});
    std::ostringstream out;
    dft::write_capture_refusals(out,
                                dft::find_capture_refusals(procedure, constraints, gates(1, 1)));
    const std::string block = out.str();
    expect(block.rfind("gmax: cannot apply the capture procedure; refusing to grade\n", 0) == 0,
           "the block opens with the header");
    expect(contains(block, "gmax:   clock-repeated: 'clk_a' appears in cycle 0 and cycle 1"),
           "a finding is a two-space indented code and detail");
    expect(block.find("refusing to grade") == block.rfind("refusing to grade"),
           "the header appears once");
}

}  // namespace

int main() {
    test_code_names();
    test_clean_procedure();
    test_simultaneous_across_groups();
    test_shared_edge();
    test_group_timing();
    test_clock_repeated();
    test_no_capture_clock();
    test_group_spanning_declared_groups();
    test_order();
    test_render();

    if (failures != 0) {
        std::cerr << failures << " failure(s)" << std::endl;
        return 1;
    }
    return 0;
}
