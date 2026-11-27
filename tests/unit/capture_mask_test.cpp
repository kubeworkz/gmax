// ADR-013 section 2: the mask a masked intermediate state applies to the run's
// observation points, and the `scanned` default a cycle that states nothing keeps.
// Library only: no run, no refusal and no coverage count is exercised here.

#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/db/constraints.h"
#include "core/db/db.h"
#include "core/db/flat.h"
#include "core/db/string_table.h"
#include "core/dft/capture.h"
#include "core/dft/capture_mask.h"

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

db::Gate gate(db::GateType type, std::string name, std::vector<std::string> fanins,
              std::string out) {
    db::Gate result;
    result.type = type;
    result.name = std::move(name);
    result.fanins = std::move(fanins);
    result.out = std::move(out);
    return result;
}

db::NetId net_named(const db::FlatNetlist& flat, const db::StringTable& strings,
                    const std::string_view name) {
    for (db::NetId net = 0; net < flat.net_count(); ++net) {
        if (strings.at(flat.name_id[net]) == name) {
            return net;
        }
    }
    return db::kInvalidId;
}

// Two declared domains and one observation the mask must not touch on each side of
// the walk: `q_a` and `q_m` are the masked domain's combinational fanout, `q_b` is
// another domain's output, `q_c` sits behind a lockup latch and a flop, so the walk
// stops before it, and `q_d` is input logic that touches no state at all.
db::DesignDb make_database() {
    db::Design source;
    source.top = "top";
    db::Module top;
    top.name = "top";
    top.inputs = {"clk_a", "clk_b", "d"};
    top.outputs = {"q_a", "q_m", "q_b", "q_c", "q_d"};
    top.gates = {
        gate(db::GateType::ScanCell, "ff_a", {"d", "clk_a"}, "n_a"),
        gate(db::GateType::Buf, "g_qa", {"n_a"}, "q_a"),
        gate(db::GateType::And2, "g_m", {"n_a", "d"}, "n_m"),
        gate(db::GateType::Buf, "g_qm", {"n_m"}, "q_m"),
        gate(db::GateType::ScanCell, "ff_b", {"d", "clk_b"}, "n_b"),
        gate(db::GateType::Buf, "g_qb", {"n_b"}, "q_b"),
        gate(db::GateType::Buf, "g_qd", {"d"}, "q_d"),
        gate(db::GateType::Dlat, "dl", {"n_a", "clk_b"}, "n_h"),
        gate(db::GateType::Dff, "ff_c", {"n_h", "clk_b"}, "n_c"),
        gate(db::GateType::Buf, "g_qc", {"n_c"}, "q_c"),
    };
    source.modules = {top};

    db::DesignDb database;
    database.design() = std::move(source);
    (void)database.flatten();

    db::Constraints constraints;
    constraints.present = true;
    constraints.complete = true;
    db::ConstraintClock fast;
    fast.name = "clk_a";
    fast.period = 10.0;
    fast.source = "clk_a";
    db::ConstraintClock slow;
    slow.name = "clk_b";
    slow.period = 20.0;
    slow.source = "clk_b";
    constraints.clocks = {fast, slow};
    database.constraints() = std::move(constraints);
    return database;
}

dft::CaptureCycle cycle_of(const std::vector<std::string>& clocks) {
    dft::CaptureGroup group;
    group.timing.clocks = clocks;
    dft::CaptureCycle cycle;
    cycle.groups = {group};
    return cycle;
}

dft::CaptureProcedure sequence() {
    dft::CaptureProcedure procedure;
    procedure.kind = dft::CaptureKind::Sequential;
    procedure.derived = false;
    procedure.cycles = {cycle_of({"clk_a"}), cycle_of({"clk_b"})};
    return procedure;
}

struct Observations {
    std::vector<db::NetId> all;
    db::NetId q_a = db::kInvalidId;
    db::NetId q_m = db::kInvalidId;
    db::NetId q_b = db::kInvalidId;
    db::NetId q_c = db::kInvalidId;
    db::NetId q_d = db::kInvalidId;
};

Observations observations_of(const db::DesignDb& database) {
    Observations result;
    result.q_a = net_named(database.flat(), database.strings(), "q_a");
    result.q_m = net_named(database.flat(), database.strings(), "q_m");
    result.q_b = net_named(database.flat(), database.strings(), "q_b");
    result.q_c = net_named(database.flat(), database.strings(), "q_c");
    result.q_d = net_named(database.flat(), database.strings(), "q_d");
    result.all = {result.q_a, result.q_m, result.q_b, result.q_c, result.q_d};
    return result;
}

void test_default_and_explicit_state() {
    const dft::CaptureCycle quiet = cycle_of({"clk_a"});
    expect(dft::capture_intermediate_state(quiet, "clk_a") ==
               dft::CaptureIntermediateState::Scanned,
           "a cycle that states nothing defaults to scanned");
    expect(dft::capture_intermediate_state(quiet, "clk_b") ==
               dft::CaptureIntermediateState::Scanned,
           "a domain the cycle does not state is scanned");

    dft::CaptureCycle stated = quiet;
    stated.intermediate = {{"clk_a", dft::CaptureIntermediateState::Masked}};
    expect(dft::capture_intermediate_state(stated, "clk_a") ==
               dft::CaptureIntermediateState::Masked,
           "a stated domain keeps its state");
}

void test_mask_reaches_combinational_observations() {
    const db::DesignDb database = make_database();
    const Observations observations = observations_of(database);

    dft::CaptureProcedure procedure = sequence();
    procedure.cycles[0].intermediate = {{"clk_a", dft::CaptureIntermediateState::Masked}};
    const dft::CaptureMask mask = dft::apply_capture_mask(
        procedure, database.constraints(), database.flat(), database.strings(), observations.all);

    expect(mask.domains == (std::vector<std::string>{"clk_a"}), "the masked domain is named");
    expect(mask.observables ==
               (std::vector<db::NetId>{observations.q_b, observations.q_c, observations.q_d}),
           "only the observations the domain reaches are masked");
    expect(mask.observables.size() == observations.all.size() - 2,
           "the mask removes exactly two observation points");
}

void test_scanned_procedure_leaves_observations() {
    const db::DesignDb database = make_database();
    const Observations observations = observations_of(database);
    const dft::CaptureMask mask = dft::apply_capture_mask(
        sequence(), database.constraints(), database.flat(), database.strings(), observations.all);
    expect(mask.domains.empty(), "no cycle masks anything");
    expect(mask.observables == observations.all, "an unmasked run keeps every observation");
}

void test_final_cycle_and_unpulsed_entries() {
    const db::DesignDb database = make_database();
    const Observations observations = observations_of(database);

    // The final cycle owns no intermediate value, so its state is ignored.
    dft::CaptureProcedure late = sequence();
    late.cycles[1].intermediate = {{"clk_b", dft::CaptureIntermediateState::Masked}};
    const dft::CaptureMask final_mask = dft::apply_capture_mask(
        late, database.constraints(), database.flat(), database.strings(), observations.all);
    expect(final_mask.domains.empty(), "the final cycle masks nothing");
    expect(final_mask.observables == observations.all, "the final cycle removes nothing");

    // An entry for a domain the cycle does not capture is not that cycle's state.
    dft::CaptureProcedure stale = sequence();
    stale.cycles[0].intermediate = {{"clk_b", dft::CaptureIntermediateState::Masked}};
    const dft::CaptureMask stale_mask = dft::apply_capture_mask(
        stale, database.constraints(), database.flat(), database.strings(), observations.all);
    expect(stale_mask.domains.empty(), "an unpulsed domain is not masked at this cycle");
    expect(stale_mask.observables == observations.all, "nothing is removed for an unpulsed entry");
}

}  // namespace

int main() {
    test_default_and_explicit_state();
    test_mask_reaches_combinational_observations();
    test_scanned_procedure_leaves_observations();
    test_final_cycle_and_unpulsed_entries();
    if (failures != 0) {
        std::cerr << failures << " failure(s)" << std::endl;
        return 1;
    }
    std::cout << "capture_mask: all tests passed" << std::endl;
    return 0;
}
