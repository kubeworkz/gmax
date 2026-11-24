// WP-007: PODEM over the implication engine. Every expected assignment below
// was worked out by hand on the good/faulty pair definition: a fault on a is
// activated as D (good 1, faulty 0), and a D must reach an observable through
// gates whose side inputs are non-controlling. The tests are framework-free so
// third_party stays out of the build.

#include <initializer_list>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/atpg/logic5.h"
#include "core/atpg/podem.h"
#include "core/db/db.h"
#include "core/sim/fault.h"

namespace db = gmax::db;
namespace atpg = gmax::atpg;
namespace sim = gmax::sim;

namespace {

int failures = 0;

void expect(const bool ok, const std::string_view what) {
    if (!ok) {
        std::cerr << "FAIL " << what << std::endl;
        ++failures;
    }
}

void expect_logic(const atpg::Logic5 actual, const atpg::Logic5 expected,
                  const std::string_view what) {
    if (actual != expected) {
        std::cerr << "FAIL " << what << ": expected " << atpg::logic5_name(expected) << ", got "
                  << atpg::logic5_name(actual) << std::endl;
        ++failures;
    }
}

void expect_status(const atpg::PodemResult& result, const atpg::PodemStatus expected,
                   const std::string_view what) {
    if (result.status != expected) {
        std::cerr << "FAIL " << what << ": expected " << atpg::podem_status_name(expected)
                  << ", got " << atpg::podem_status_name(result.status) << std::endl;
        ++failures;
    }
}

void expect_result_value(const atpg::PodemResult& result, const db::NetId net,
                         const atpg::Logic5 expected, const std::string_view what) {
    if (net >= result.values.size()) {
        std::cerr << "FAIL " << what << ": no result value for net " << net << std::endl;
        ++failures;
        return;
    }
    expect_logic(result.values[net], expected, what);
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

db::Design make_design(std::vector<std::string> inputs, std::vector<std::string> outputs,
                       std::vector<db::Gate> gates) {
    db::Design design;
    design.top = "top";
    db::Module top;
    top.name = "top";
    top.inputs = std::move(inputs);
    top.outputs = std::move(outputs);
    top.gates = std::move(gates);
    design.modules = {top};
    return design;
}

struct Fixture {
    db::DesignDb database;

    explicit Fixture(db::Design design) {
        database.design() = std::move(design);
        const db::Status status = database.flatten();
        expect(status.ok(), "the PODEM test design flattens");
        if (!status.ok()) {
            std::cerr << "  " << status.message() << std::endl;
        }
    }

    db::NetId net(const std::string_view name) const {
        const db::FlatNetlist& flat = database.flat();
        for (db::NetId id = 0; id < flat.net_count(); ++id) {
            if (database.strings().at(flat.name_id[id]) == name) {
                return id;
            }
        }
        return db::kInvalidId;
    }

    atpg::PodemResult run(const sim::Fault& fault, const std::vector<db::NetId>& observables,
                          const atpg::PodemBudget budget = {}) const {
        return atpg::run_podem(database.flat(), fault, std::span<const db::NetId>(observables),
                               budget);
    }

    void expect_complete(const std::initializer_list<db::NetId> nets) const {
        for (const db::NetId id : nets) {
            expect(id != db::kInvalidId, "the PODEM fixture has all expected nets");
        }
    }
};

void test_primary_input_fault() {
    Fixture fixture(
        make_design({"a", "b"}, {"y"}, {gate(db::GateType::And2, "g_and", {"a", "b"}, "y")}));
    const db::NetId a = fixture.net("a");
    const db::NetId b = fixture.net("b");
    const db::NetId y = fixture.net("y");
    fixture.expect_complete({a, b, y});

    // a=1, b=X leaves y=X. PODEM must justify b=1; then y becomes D.
    const atpg::PodemResult detected = fixture.run({a, sim::StuckValue::Zero}, {y});
    expect_status(detected, atpg::PodemStatus::Detected, "a sa0 is detected through the AND");
    expect(detected.decisions == 1, "a sa0 needs one decision for b=1");
    expect(detected.backtracks == 0, "the first branch detects a sa0");
    expect_result_value(detected, a, atpg::Logic5::D, "the fault site is D");
    expect_result_value(detected, b, atpg::Logic5::One, "the side input is justified to 1");
    expect_result_value(detected, y, atpg::Logic5::D, "D reaches the output");

    // The opposite stuck value must force Dbar and still require b=1.
    const atpg::PodemResult detected_one = fixture.run({a, sim::StuckValue::One}, {y});
    expect_status(detected_one, atpg::PodemStatus::Detected, "a sa1 is detected through the AND");
    expect_result_value(detected_one, a, atpg::Logic5::DBar, "the fault site is Dbar");
    expect_result_value(detected_one, b, atpg::Logic5::One, "the side input is justified to 1");
    expect_result_value(detected_one, y, atpg::Logic5::DBar, "Dbar reaches the output");

    // A primary input that is itself observable needs no propagation decision.
    const atpg::PodemResult direct = fixture.run({b, sim::StuckValue::Zero}, {b});
    expect_status(direct, atpg::PodemStatus::Detected, "a fault on an observable PI is detected");
    expect(direct.decisions == 0, "a direct observation needs no decision");
    expect_result_value(direct, b, atpg::Logic5::D, "the directly observed PI is D");
}

void test_internal_activation_and_propagation() {
    Fixture fixture(make_design({"a", "b", "c"}, {"y"},
                                {gate(db::GateType::And2, "g_and", {"a", "b"}, "n1"),
                                 gate(db::GateType::Or2, "g_or", {"n1", "c"}, "y")}));
    const db::NetId a = fixture.net("a");
    const db::NetId b = fixture.net("b");
    const db::NetId c = fixture.net("c");
    const db::NetId n1 = fixture.net("n1");
    const db::NetId y = fixture.net("y");
    fixture.expect_complete({a, b, c, n1, y});

    // Activating n1 sa0 requires a=b=1. Propagating D through the OR requires
    // c=0. The three decisions are therefore a, b, c in that order.
    const atpg::PodemResult result = fixture.run({n1, sim::StuckValue::Zero}, {y});
    expect_status(result, atpg::PodemStatus::Detected, "an internal sa0 is detected");
    expect(result.decisions == 3, "activation needs a and b, propagation needs c");
    expect(result.backtracks == 0, "no branch in the internal circuit fails first");
    expect_result_value(result, a, atpg::Logic5::One, "a is justified for n1=1");
    expect_result_value(result, b, atpg::Logic5::One, "b is justified for n1=1");
    expect_result_value(result, c, atpg::Logic5::Zero, "c is justified to propagate D");
    expect_result_value(result, n1, atpg::Logic5::D, "the internal fault site stays D");
    expect_result_value(result, y, atpg::Logic5::D, "D reaches the OR output");
}

void test_nand_activation() {
    Fixture fixture(make_design({"a", "b"}, {"y"},
                                {gate(db::GateType::Nand2, "g_nand", {"a", "b"}, "n1"),
                                 gate(db::GateType::Buf, "g_y", {"n1"}, "y")}));
    const db::NetId a = fixture.net("a");
    const db::NetId b = fixture.net("b");
    const db::NetId n1 = fixture.net("n1");
    const db::NetId y = fixture.net("y");
    fixture.expect_complete({a, b, n1, y});

    // NAND output 1 is controlled by any 0, so one input suffices to activate
    // n1 sa0 as D; the other input may remain X.
    const atpg::PodemResult sa0 = fixture.run({n1, sim::StuckValue::Zero}, {y});
    expect_status(sa0, atpg::PodemStatus::Detected, "a NAND sa0 is activated by an input 0");
    expect(sa0.decisions == 1, "NAND sa0 needs only the chosen input");
    expect_result_value(sa0, a, atpg::Logic5::Zero, "a is 0 for NAND output 1");
    expect_result_value(sa0, b, atpg::Logic5::X, "b stays X under the controlling 0");
    expect_result_value(sa0, n1, atpg::Logic5::D, "the NAND sa0 site is D");
    expect_result_value(sa0, y, atpg::Logic5::D, "the buffer carries D");

    // NAND output 0 requires both inputs 1, so n1 sa1 needs two decisions.
    const atpg::PodemResult sa1 = fixture.run({n1, sim::StuckValue::One}, {y});
    expect_status(sa1, atpg::PodemStatus::Detected, "a NAND sa1 is activated by all-one inputs");
    expect(sa1.decisions == 2, "NAND sa1 needs both inputs assigned");
    expect_result_value(sa1, a, atpg::Logic5::One, "a is 1 for NAND output 0");
    expect_result_value(sa1, b, atpg::Logic5::One, "b is 1 for NAND output 0");
    expect_result_value(sa1, n1, atpg::Logic5::DBar, "the NAND sa1 site is Dbar");
    expect_result_value(sa1, y, atpg::Logic5::DBar, "the buffer carries Dbar");
}

void test_chronological_backtracking() {
    Fixture fixture(make_design({"a", "b"}, {"y"},
                                {gate(db::GateType::Xor2, "g_xor", {"a", "b"}, "n1"),
                                 gate(db::GateType::And2, "g_and", {"n1", "b"}, "y")}));
    const db::NetId a = fixture.net("a");
    const db::NetId b = fixture.net("b");
    const db::NetId n1 = fixture.net("n1");
    const db::NetId y = fixture.net("y");
    fixture.expect_complete({a, b, n1, y});

    // The first side objective is b=0: XOR(D,0)=D, but AND(D,0)=0 hides it.
    // Restoring that branch and trying b=1 gives XOR(D,1)=Dbar and then
    // AND(Dbar,1)=Dbar, which is observable.
    const atpg::PodemResult result = fixture.run({a, sim::StuckValue::Zero}, {y});
    expect_status(result, atpg::PodemStatus::Detected, "the XOR/AND fault is detected");
    expect(result.decisions == 2, "PODEM tries b=0 and then b=1");
    expect(result.backtracks == 1, "the b=0 branch is the one backtrack");
    expect_result_value(result, a, atpg::Logic5::D, "a remains the activated fault");
    expect_result_value(result, b, atpg::Logic5::One, "the second branch selects b=1");
    expect_result_value(result, n1, atpg::Logic5::DBar, "XOR(D,1) is Dbar");
    expect_result_value(result, y, atpg::Logic5::DBar, "AND(Dbar,1) is Dbar");

    // The same circuit under a zero-backtrack budget must stop after the first
    // failed branch rather than silently proving the fault untestable.
    atpg::PodemBudget budget;
    budget.max_backtracks = 0;
    const atpg::PodemResult aborted = fixture.run({a, sim::StuckValue::Zero}, {y}, budget);
    expect_status(aborted, atpg::PodemStatus::Aborted, "a zero backtrack budget aborts");
    expect(aborted.backtracks == 1, "the abort follows the first failed branch");
    expect(aborted.values.empty(), "an aborted search has no assignment snapshot");
}

void test_no_x_path_is_untestable() {
    Fixture fixture(make_design({"a", "b"}, {"y"},
                                {gate(db::GateType::And2, "g_and", {"a", "b"}, "n1"),
                                 gate(db::GateType::Buf, "g_dangling", {"n1"}, "dangling"),
                                 gate(db::GateType::Buf, "g_y", {"a"}, "y")}));
    const db::NetId a = fixture.net("a");
    const db::NetId b = fixture.net("b");
    const db::NetId n1 = fixture.net("n1");
    const db::NetId y = fixture.net("y");
    fixture.expect_complete({a, b, n1, y});

    // n1 can be activated (a=b=1), but its only consumer is dangling and the
    // observable y follows a. No controlling assignment can make n1 observable.
    const atpg::PodemResult result = fixture.run({n1, sim::StuckValue::Zero}, {y});
    expect_status(result, atpg::PodemStatus::Untestable, "a fault with no X-path is untestable");
    expect(result.values.empty(), "an untestable search has no assignment snapshot");
}

void test_constant_site_is_untestable() {
    Fixture fixture(make_design({}, {"y"}, {gate(db::GateType::Tie0, "g_low", {}, "y")}));
    const db::NetId y = fixture.net("y");
    fixture.expect_complete({y});

    // y is already 0 at reset, so the stuck-at-1 site cannot be forced.
    const atpg::PodemResult result = fixture.run({y, sim::StuckValue::One}, {y});
    expect_status(result, atpg::PodemStatus::Untestable, "a constant site is untestable");
}

void test_controlled_path_is_skipped() {
    Fixture fixture(make_design({"a", "b"}, {"y"},
                                {gate(db::GateType::And2, "g_shadow", {"a", "t0"}, "shadow"),
                                 gate(db::GateType::Tie0, "g_low", {}, "t0"),
                                 gate(db::GateType::And2, "g_y", {"a", "b"}, "y")}));
    const db::NetId a = fixture.net("a");
    const db::NetId b = fixture.net("b");
    const db::NetId shadow = fixture.net("shadow");
    const db::NetId y = fixture.net("y");
    fixture.expect_complete({a, b, shadow, y});

    // The first consumer of a is an AND whose other input is tied low, so the
    // fault cannot pass through it. The second consumer is the observable AND;
    // PODEM must skip the blocked path and justify b=1.
    const atpg::PodemResult result = fixture.run({a, sim::StuckValue::Zero}, {y});
    expect_status(result, atpg::PodemStatus::Detected, "a controlled path is skipped");
    expect(result.decisions == 1, "the controlled path costs no decision");
    expect_result_value(result, b, atpg::Logic5::One, "b is justified for the live path");
    expect_result_value(result, shadow, atpg::Logic5::Zero, "the blocked path stays 0");
    expect_result_value(result, y, atpg::Logic5::D, "the live path carries D to y");
}

void test_x_source_path_is_skipped() {
    Fixture fixture(make_design({"a", "b"}, {"y"},
                                {gate(db::GateType::And2, "g_shadow", {"a", "s"}, "shadow"),
                                 gate(db::GateType::Blackbox, "g_state", {}, "s"),
                                 gate(db::GateType::And2, "g_y", {"a", "b"}, "y")}));
    const db::NetId a = fixture.net("a");
    const db::NetId b = fixture.net("b");
    const db::NetId shadow = fixture.net("shadow");
    const db::NetId s = fixture.net("s");
    const db::NetId y = fixture.net("y");
    fixture.expect_complete({a, b, shadow, s, y});

    // shadow's side input s is an opaque X source, so no backtrace can make it
    // 1. That path is infeasible even though the fault reaches it first; the
    // live path through y must still be found.
    const atpg::PodemResult result = fixture.run({a, sim::StuckValue::Zero}, {y});
    expect_status(result, atpg::PodemStatus::Detected, "an X-source path is skipped");
    expect(result.decisions == 1, "the X source costs no decision");
    expect_result_value(result, b, atpg::Logic5::One, "b is justified for the live path");
    expect_result_value(result, s, atpg::Logic5::X, "the X source remains unassigned");
    expect_result_value(result, shadow, atpg::Logic5::X, "the blocked path is not made up");
    expect_result_value(result, y, atpg::Logic5::D, "the live path carries D to y");
}

void test_mux_propagation() {
    Fixture fixture(make_design({"a", "b", "s"}, {"y"},
                                {gate(db::GateType::Mux2, "g_mux", {"a", "b", "s"}, "y")}));
    const db::NetId a = fixture.net("a");
    const db::NetId b = fixture.net("b");
    const db::NetId s = fixture.net("s");
    const db::NetId y = fixture.net("y");
    fixture.expect_complete({a, b, s, y});

    // A fault on the select must separate the two data paths. With s=D the
    // good select is 1 and the faulty select is 0; a=0 (good) and b=1 (faulty)
    // then make y=0 in the good circuit and y=1 in the faulty circuit, i.e. Dbar.
    const atpg::PodemResult result = fixture.run({s, sim::StuckValue::Zero}, {y});
    expect_status(result, atpg::PodemStatus::Detected, "a select fault is detected through MUX2");
    expect(result.decisions == 2, "the MUX needs two data decisions");
    expect_result_value(result, s, atpg::Logic5::D, "the select is D");
    expect_result_value(result, a, atpg::Logic5::Zero, "the then data is 0");
    expect_result_value(result, b, atpg::Logic5::One, "the else data is 1");
    expect_result_value(result, y, atpg::Logic5::DBar, "the MUX output carries Dbar");
}

void test_status_names() {
    expect(atpg::podem_status_name(atpg::PodemStatus::Detected) == "detected",
           "detected has a stable name");
    expect(atpg::podem_status_name(atpg::PodemStatus::Untestable) == "untestable",
           "untestable has a stable name");
    expect(atpg::podem_status_name(atpg::PodemStatus::Aborted) == "aborted",
           "aborted has a stable name");
}

}  // namespace

int main() {
    test_primary_input_fault();
    test_internal_activation_and_propagation();
    test_nand_activation();
    test_chronological_backtracking();
    test_no_x_path_is_untestable();
    test_constant_site_is_untestable();
    test_controlled_path_is_skipped();
    test_x_source_path_is_skipped();
    test_mux_propagation();
    test_status_names();

    if (failures != 0) {
        std::cerr << failures << " PODEM test(s) failed" << std::endl;
        return 1;
    }
    std::cout << "PODEM tests passed" << std::endl;
    return 0;
}
