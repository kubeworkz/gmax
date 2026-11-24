// WP-011: the SAT fallback is a complete miter check, so it must distinguish a
// testable fault from a structurally redundant one and must decline sequential
// designs rather than inventing a combinational proof.

#include <chrono>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/atpg/sat.h"
#include "core/db/db.h"
#include "core/sim/fault.h"

namespace db = gmax::db;
namespace atpg = gmax::atpg;
namespace sim = gmax::sim;

namespace {

using TestClock = std::chrono::steady_clock;

int failures = 0;
TestClock::time_point fake_epoch;
std::size_t fake_calls = 0;

// The first four clock reads are the miter start and its small build checks. The
// fifth read happens in the solver and the sixth trips its loop deadline, so
// this deterministically tests the solver's own timeout. If the solver ignored
// time, its post-solve check would be read five and remain inside the budget.
TestClock::time_point timeout_now() {
    ++fake_calls;
    return fake_calls < 6 ? fake_epoch : fake_epoch + std::chrono::milliseconds(2);
}

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
        expect(status.ok(), "the SAT test design flattens");
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

    atpg::SatFaultResult check(const sim::Fault& fault, const db::NetId observable) const {
        return atpg::check_fault(database.flat(), fault,
                                 std::span<const db::NetId>(&observable, 1));
    }
};

void test_testable_and_redundant() {
    Fixture fixture(make_design({"a", "b"}, {"y", "tied"},
                                {gate(db::GateType::And2, "g_and", {"a", "b"}, "n"),
                                 gate(db::GateType::Buf, "g_y", {"n"}, "y"),
                                 gate(db::GateType::Tie0, "g_zero", {}, "zero"),
                                 gate(db::GateType::Buf, "g_tied", {"zero"}, "tied")}));
    const db::NetId n = fixture.net("n");
    const db::NetId y = fixture.net("y");
    const db::NetId tied = fixture.net("tied");
    expect(n != db::kInvalidId && y != db::kInvalidId && tied != db::kInvalidId,
           "the SAT fixture has its nets");

    const atpg::SatFaultResult sa0 = fixture.check({n, sim::StuckValue::Zero}, y);
    expect(sa0.status == atpg::SatFaultStatus::Detected, "an internal AND sa0 is testable");
    const atpg::SatFaultResult sa1 = fixture.check({n, sim::StuckValue::One}, y);
    expect(sa1.status == atpg::SatFaultStatus::Detected, "an internal AND sa1 is testable");

    // The tied buffer is always zero; forcing it to zero cannot change any output.
    const atpg::SatFaultResult redundant = fixture.check({tied, sim::StuckValue::Zero}, tied);
    expect(redundant.status == atpg::SatFaultStatus::Untestable,
           "a tied-net sa0 is proven untestable");
    expect(redundant.variables > 0 && redundant.clauses > 0,
           "the untestable proof built a SAT miter");
}

void test_xor_encoding() {
    Fixture fixture(make_design({"a", "b"}, {"y"},
                                {gate(db::GateType::Xor2, "g_xor", {"a", "b"}, "n"),
                                 gate(db::GateType::Buf, "g_y", {"n"}, "y")}));
    const db::NetId n = fixture.net("n");
    const db::NetId y = fixture.net("y");
    expect(n != db::kInvalidId && y != db::kInvalidId, "the XOR fixture has its nets");

    const atpg::SatFaultResult result = fixture.check({n, sim::StuckValue::Zero}, y);
    expect(result.status == atpg::SatFaultStatus::Detected, "an XOR sa0 is testable");
}

void test_solver_boundary() {
    const std::unique_ptr<atpg::ISatSolver> solver = atpg::make_internal_sat_solver();
    expect(solver != nullptr, "the internal SAT adapter is constructible through the boundary");
    const int a = solver->new_variable();
    const int b = solver->new_variable();
    expect(solver->add_clause({a}), "the boundary accepts a unit clause");
    expect(solver->add_clause({-a, b}), "the boundary accepts a binary clause");
    const atpg::ISatSolver::Result sat = solver->solve(1000, 0, TestClock::now(), nullptr);
    expect(sat == atpg::ISatSolver::Result::Sat, "the boundary returns SAT for a satisfiable CNF");
    expect(solver->model_value(a) && solver->model_value(b), "the boundary exposes the SAT model");
    expect(solver->model_is_consistent(), "the boundary model is consistent");
    expect(solver->variables() == 2 && solver->clauses() == 2,
           "the boundary reports its formula size");

    const std::unique_ptr<atpg::ISatSolver> unsat = atpg::make_internal_sat_solver();
    const int c = unsat->new_variable();
    expect(unsat->add_clause({c}), "the UNSAT boundary accepts a unit clause");
    expect(unsat->add_clause({-c}), "the UNSAT boundary accepts the contradictory clause");
    expect(unsat->solve(1000, 0, TestClock::now(), nullptr) == atpg::ISatSolver::Result::Unsat,
           "the boundary returns UNSAT for a contradictory CNF");
}

void test_sat_timeout() {
    Fixture fixture(make_design({"a", "b"}, {"y"},
                                {gate(db::GateType::And2, "g_and", {"a", "b"}, "n"),
                                 gate(db::GateType::Buf, "g_y", {"n"}, "y")}));
    const db::NetId n = fixture.net("n");
    const db::NetId y = fixture.net("y");
    expect(n != db::kInvalidId && y != db::kInvalidId, "the timeout fixture has its nets");

    fake_epoch = TestClock::now();
    fake_calls = 0;
    const atpg::SatFaultResult result =
        atpg::check_fault(fixture.database.flat(), {n, sim::StuckValue::Zero},
                          std::span<const db::NetId>(&y, 1), 1000000, 1, &timeout_now);
    expect(result.status == atpg::SatFaultStatus::Unknown, "SAT reports a timeout as unknown");
    expect(result.solver_timed_out, "the timeout occurred inside the SAT solver");
    expect(fake_calls >= 6, "the solver observed the injected deadline");
    expect(result.variables > 0 && result.clauses > 0, "the timed-out miter was built");
}

void test_sequential_is_unsupported() {
    Fixture fixture(
        make_design({"d", "clk"}, {"q"}, {gate(db::GateType::Dff, "g_dff", {"d", "clk"}, "q")}));
    const db::NetId q = fixture.net("q");
    expect(q != db::kInvalidId, "the sequential fixture has its output");

    const atpg::SatFaultResult result = fixture.check({q, sim::StuckValue::Zero}, q);
    expect(result.status == atpg::SatFaultStatus::Unsupported,
           "the SAT fallback declines a sequential design");
}

}  // namespace

int main() {
    test_testable_and_redundant();
    test_xor_encoding();
    test_solver_boundary();
    test_sat_timeout();
    test_sequential_is_unsupported();

    if (failures != 0) {
        std::cerr << failures << " SAT test(s) failed" << std::endl;
        return 1;
    }
    std::cout << "SAT tests passed" << std::endl;
    return 0;
}
