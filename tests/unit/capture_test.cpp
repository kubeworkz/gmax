// ADR-013: the capture procedure model, its per-group timing sets, and the
// canonical payload and hash. Library only: no run, no report field and no
// refusal is exercised here. Framework-free like the other tests.

#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/db/constraints.h"
#include "core/db/crc32.h"
#include "core/dft/capture.h"

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

db::Constraints make_constraints() {
    db::Constraints constraints;
    constraints.present = true;
    constraints.complete = true;

    db::ConstraintClock fast = make_clock("clk_a", 10.0);
    fast.has_waveform = true;
    fast.rise = 0.0;
    fast.fall = 5.0;
    db::ConstraintClock slow = make_clock("clk_b", 20.0);
    // Written in reverse name order: the derived procedure sorts by clock name,
    // so source order must not decide the cycles.
    constraints.clocks = {slow, fast};

    db::ConstraintClockGroup group;
    group.kind = db::ClockGroupKind::Asynchronous;
    group.name = "async";
    group.groups = {{"clk_b"}, {"clk_a"}};
    constraints.clock_groups = {group};
    return constraints;
}

void test_kind_names() {
    expect(dft::capture_kind_name(dft::CaptureKind::Simultaneous) == "simultaneous",
           "simultaneous kind name");
    expect(dft::capture_kind_name(dft::CaptureKind::Grouped) == "grouped", "grouped kind name");
    expect(dft::capture_kind_name(dft::CaptureKind::Sequential) == "sequential",
           "sequential kind name");
}

void test_timing_set() {
    const db::Constraints constraints = make_constraints();

    const dft::CaptureTimingSet fast = dft::capture_timing_set(constraints, {"clk_a"}, 0.0);
    expect(fast.period == 10.0, "timing set takes the clock period");
    expect(fast.has_waveform, "timing set takes the waveform flag");
    expect(fast.rise == 0.0 && fast.fall == 5.0, "timing set takes rise and fall");
    expect(fast.edge == 0.0, "first group sits at edge zero");

    // A group is a set: the order its names were written in is not procedure.
    const std::vector<std::string> reordered = {"clk_b", "clk_a"};
    const dft::CaptureTimingSet first = dft::capture_timing_set(constraints, reordered, 20.0);
    expect(first.clocks == (std::vector<std::string>{"clk_a", "clk_b"}),
           "timing set sorts its clocks bytewise");
    expect(first.period == 10.0, "timing set takes the bytewise smallest clock");
    expect(first.edge == 20.0, "timing set keeps the requested edge");

    const dft::CaptureTimingSet unknown = dft::capture_timing_set(constraints, {"nope"}, 0.0);
    expect(unknown.period == 0.0 && !unknown.has_waveform, "undeclared clock invents no timing");

    const dft::CaptureTimingSet empty = dft::capture_timing_set(constraints, {}, 5.0);
    expect(empty.clocks.empty() && empty.period == 0.0 && empty.edge == 5.0,
           "empty group keeps only its edge");
}

void test_timing_agreement() {
    const db::Constraints constraints = make_constraints();

    expect(dft::capture_group_timing_agrees(constraints, {"clk_a"}),
           "one clock agrees with itself");
    expect(!dft::capture_group_timing_agrees(constraints, {"clk_a", "clk_b"}),
           "clocks with different periods disagree");
    expect(!dft::capture_group_timing_agrees(constraints, {"clk_a", "nope"}),
           "an undeclared clock cannot agree");
    expect(dft::capture_group_timing_agrees(constraints, {}), "an empty group agrees");

    db::Constraints same = make_constraints();
    same.clocks.push_back(make_clock("clk_c", 10.0));
    same.clocks.back().has_waveform = true;
    same.clocks.back().rise = 0.0;
    same.clocks.back().fall = 5.0;
    expect(dft::capture_group_timing_agrees(same, {"clk_a", "clk_c"}),
           "clocks with one period and waveform agree");

    // Same period, different waveform: still a disagreement (section 3).
    same.clocks.back().fall = 7.0;
    expect(!dft::capture_group_timing_agrees(same, {"clk_c", "clk_a"}),
           "clocks with different waveforms disagree");
}

void test_derived_procedure() {
    const db::Constraints constraints = make_constraints();
    const dft::CaptureProcedure procedure = dft::resolve_capture_procedure(constraints);

    expect(procedure.kind == dft::CaptureKind::Sequential, "derived procedure is sequential");
    expect(procedure.derived, "derived procedure is marked derived");
    expect(procedure.cycles.size() == 2, "one cycle per declared clock");
    if (procedure.cycles.size() == 2) {
        expect(procedure.cycles[0].groups.size() == 1, "a derived cycle pulses one group");
        expect(procedure.cycles[0].groups[0].timing.clocks == (std::vector<std::string>{"clk_a"}),
               "cycles are ordered by clock name");
        expect(procedure.cycles[1].groups[0].timing.clocks == (std::vector<std::string>{"clk_b"}),
               "the second cycle is the next clock name");
        expect(procedure.cycles[0].groups[0].timing.period == 10.0, "cycle keeps clk_a period");
        expect(procedure.cycles[0].groups[0].timing.edge == 0.0, "derived cycle starts at zero");
        expect(procedure.cycles[0].groups[0].name.empty(), "a derived group has no SDC name");
        // Section 5: a non-final cycle states the state of the domain it does not
        // pulse, and the engine's default is to scan it.
        expect(procedure.cycles[0].intermediate.size() == 1,
               "a non-final cycle records one intermediate state");
        expect(procedure.cycles[0].intermediate[0].first == "clk_a",
               "the state names the cycle's own domain");
        expect(procedure.cycles[0].intermediate[0].second == dft::CaptureIntermediateState::Scanned,
               "the derived default is scanned");
        expect(procedure.cycles[1].intermediate.empty(), "the final cycle records no state");
    }

    const dft::CaptureProcedure again = dft::resolve_capture_procedure(constraints);
    expect(again == procedure, "resolution is deterministic");
    expect(dft::capture_hash(again) == dft::capture_hash(procedure), "same input, same hash");
}

void test_empty_procedure() {
    db::Constraints absent;
    expect(dft::resolve_capture_procedure(absent).cycles.empty(), "no constraints, no cycles");

    db::Constraints declared;
    declared.present = true;
    expect(dft::resolve_capture_procedure(declared).cycles.empty(), "no clocks, no cycles");
}

void test_duplicate_clock_kept() {
    db::Constraints constraints = make_constraints();
    constraints.clocks.push_back(make_clock("clk_a", 10.0));
    const dft::CaptureProcedure procedure = dft::resolve_capture_procedure(constraints);
    expect(procedure.cycles.size() == 3, "a repeated clock is kept for condition 4 to refuse");
}

void test_edge_layout() {
    std::vector<dft::CaptureGroup> groups(3);
    groups[0].timing.period = 10.0;
    groups[1].timing.period = 20.0;
    groups[2].timing.period = 5.0;
    dft::assign_capture_edges(groups);
    expect(groups[0].timing.edge == 0.0, "first group at edge zero");
    expect(groups[1].timing.edge == 20.0, "groups are a slowest period apart");
    expect(groups[2].timing.edge == 40.0, "the step repeats, not the group's own period");

    std::vector<dft::CaptureGroup> unclocked(2);
    dft::assign_capture_edges(unclocked);
    expect(unclocked[1].timing.edge == 0.0, "groups without a period share edge zero");

    std::vector<dft::CaptureGroup> single(1);
    single[0].timing.period = 8.0;
    dft::assign_capture_edges(single);
    expect(single[0].timing.edge == 0.0, "a single group stays at zero");
}

void test_canonical_payload() {
    const db::Constraints constraints = make_constraints();
    const dft::CaptureProcedure procedure = dft::resolve_capture_procedure(constraints);
    const std::string payload = dft::canonical_capture_payload(procedure);

    expect(payload.rfind("gmax-capture-4\n", 0) == 0, "payload carries its own version");
    expect(payload.find("sequential") != std::string::npos, "payload names the kind");
    expect(payload.find("clk_a") != std::string::npos, "payload names the clocks");
    expect(payload.find("derived") == std::string::npos, "provenance is excluded from the payload");

    // Provenance is not procedure: the same cycles with the other flag hash the
    // same, so a profile that writes the default still identifies the run.
    dft::CaptureProcedure declared = procedure;
    declared.derived = false;
    expect(dft::canonical_capture_payload(declared) == payload, "derived flag is not hashed");
    expect(dft::capture_hash(declared) == dft::capture_hash(procedure),
           "derived flag is not hashed");

    expect(dft::capture_hash(procedure) == db::crc32(std::string_view(payload)),
           "capture hash is the CRC32 of the payload");

    // Cycle order is procedure, so swapping cycles must change the hash.
    dft::CaptureProcedure swapped = procedure;
    std::swap(swapped.cycles[0], swapped.cycles[1]);
    expect(dft::capture_hash(swapped) != dft::capture_hash(procedure),
           "cycle order changes the hash");

    // A group's timing numbers are procedure; the clock list order is not.
    dft::CaptureProcedure retimed = procedure;
    retimed.cycles[0].groups[0].timing.period = 11.0;
    expect(dft::capture_hash(retimed) != dft::capture_hash(procedure), "period changes the hash");

    dft::CaptureProcedure renamed = procedure;
    renamed.cycles[0].groups[0].timing.clocks = {"zeta", "clk_a"};
    dft::CaptureProcedure written = procedure;
    written.cycles[0].groups[0].timing.clocks = {"clk_a", "zeta"};
    expect(dft::canonical_capture_payload(renamed) == dft::canonical_capture_payload(written),
           "a group's clock list is written sorted");

    // A group label is diagnostics, not procedure: two procedures that differ
    // only in the label must hash the same, and the text must never be written.
    dft::CaptureProcedure labelled = procedure;
    labelled.cycles[0].groups[0].name = "fast_domain";
    dft::CaptureProcedure relabelled = procedure;
    relabelled.cycles[0].groups[0].name = "slow_domain";
    expect(dft::canonical_capture_payload(labelled) == dft::canonical_capture_payload(relabelled),
           "a group label is not payload");
    expect(dft::capture_hash(labelled) == dft::capture_hash(procedure),
           "a group label is not hashed");
    expect(dft::canonical_capture_payload(labelled).find("fast_domain") == std::string::npos,
           "the label text never reaches the payload");

    // Section 5: only a non-final cycle writes an intermediate line, the default
    // is written rather than omitted, and a masked domain changes the identity.
    expect(payload.find("intermediate\t0\tclk_a\tscanned") != std::string::npos,
           "a non-final cycle writes its default state");
    expect(payload.find("intermediate\t1\t") == std::string::npos,
           "the final cycle writes no intermediate state");

    dft::CaptureProcedure masked = procedure;
    masked.cycles[0].intermediate[0].second = dft::CaptureIntermediateState::Masked;
    expect(dft::capture_hash(masked) != dft::capture_hash(procedure),
           "the intermediate state is part of the hash");
    expect(dft::canonical_capture_payload(masked).find("intermediate\t0\tclk_a\tmasked") !=
               std::string::npos,
           "a masked domain is written as masked");

    // The state belongs to the cycle's domains, so a cycle that pulses both of
    // its domains in one group still states each of them.
    dft::CaptureProcedure both = procedure;
    both.cycles[0].groups[0].timing.clocks = {"clk_a", "clk_b"};
    expect(dft::canonical_capture_payload(both).find("intermediate\t0\tclk_b\tscanned") !=
               std::string::npos,
           "every domain of a cycle is stated");
}

void test_group_order() {
    const db::Constraints constraints = make_constraints();

    dft::CaptureGroup fast;
    fast.timing = dft::capture_timing_set(constraints, {"clk_a"}, 0.0);
    dft::CaptureGroup slow;
    slow.timing = dft::capture_timing_set(constraints, {"clk_b"}, 20.0);

    dft::CaptureCycle cycle;
    cycle.groups = {fast, slow};
    dft::CaptureProcedure forward;
    forward.kind = dft::CaptureKind::Grouped;
    forward.derived = false;
    forward.cycles = {cycle};

    // The edge places the group, so the profile's listing order is not procedure.
    dft::CaptureProcedure reversed = forward;
    std::swap(reversed.cycles[0].groups[0], reversed.cycles[0].groups[1]);
    expect(dft::canonical_capture_payload(reversed) == dft::canonical_capture_payload(forward),
           "group order does not change the payload");
    expect(dft::capture_hash(reversed) == dft::capture_hash(forward),
           "group order does not change the hash");

    const std::string payload = dft::canonical_capture_payload(reversed);
    expect(payload.find("clk_a") < payload.find("clk_b"), "groups are written in edge order");
    expect(payload.find("group\t0\t0\t0") != std::string::npos,
           "the earliest edge takes group index zero");
    expect(payload.find("group\t0\t1\t20") != std::string::npos,
           "the later edge takes group index one");

    // Equal edges are refused later, but the encoding still has to be total, so
    // the clock list breaks the tie.
    dft::CaptureProcedure tied = forward;
    tied.cycles[0].groups[1].timing.edge = 0.0;
    dft::CaptureProcedure tied_reversed = tied;
    std::swap(tied_reversed.cycles[0].groups[0], tied_reversed.cycles[0].groups[1]);
    expect(dft::canonical_capture_payload(tied_reversed) == dft::canonical_capture_payload(tied),
           "an edge tie is broken by the clock list");
}

void test_constraints_hash_unchanged() {
    db::Constraints constraints = make_constraints();
    const std::uint32_t before = db::constraints_hash(constraints);
    const dft::CaptureProcedure procedure = dft::resolve_capture_procedure(constraints);
    const std::uint32_t payload_hash = dft::capture_hash(procedure);
    expect(payload_hash != 0, "a procedure with cycles has a hash");

    // ADR-013 section 6: a procedure is how the constraints are used, not a
    // constraint. Resolving one must not touch the set or its payload.
    expect(db::constraints_hash(constraints) == before, "constraints hash is unchanged");
    expect(constraints == make_constraints(), "resolution does not mutate the constraints");
}

}  // namespace

int main() {
    test_kind_names();
    test_timing_set();
    test_timing_agreement();
    test_derived_procedure();
    test_empty_procedure();
    test_duplicate_clock_kept();
    test_edge_layout();
    test_canonical_payload();
    test_group_order();
    test_constraints_hash_unchanged();

    if (failures != 0) {
        std::cerr << failures << " failure(s)" << std::endl;
        return 1;
    }
    return 0;
}
