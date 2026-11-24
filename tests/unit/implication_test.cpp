// WP-007: the five-valued algebra and implication engine. The search loop,
// budgets, and CLI wiring are intentionally absent from this slice. Framework-
// free for the same reason as the other tests: third_party stays empty until
// TBD-11 closes.

#include <initializer_list>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/atpg/implication.h"
#include "core/atpg/logic5.h"
#include "core/db/db.h"

namespace db = gmax::db;
namespace atpg = gmax::atpg;

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

void expect_gate(const db::GateType type, const std::initializer_list<atpg::Logic5> inputs,
                 const atpg::Logic5 expected, const std::string_view what) {
    const std::vector<atpg::Logic5> fanins(inputs);
    expect_logic(atpg::eval_gate(type, std::span<const atpg::Logic5>(fanins)), expected, what);
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

db::Design make_design() {
    db::Design design;
    design.top = "top";

    db::Module top;
    top.name = "top";
    top.inputs = {"a", "b", "c"};
    top.outputs = {"y", "z"};
    top.gates = {
        gate(db::GateType::And2, "g_and", {"a", "b"}, "n1"),
        gate(db::GateType::Nand2, "g_nand", {"a", "b"}, "n2"),
        gate(db::GateType::Nor2, "g_nor", {"a", "b"}, "n3"),
        gate(db::GateType::Or2, "g_or", {"n1", "c"}, "y"),
        gate(db::GateType::Not, "g_not", {"y"}, "z"),
        gate(db::GateType::Tie0, "g_low", {}, "t0"),
        gate(db::GateType::Tie1, "g_high", {}, "t1"),
        gate(db::GateType::Buf, "g_buf", {"t0"}, "tb"),
    };
    design.modules = {top};
    return design;
}

db::DesignDb flatten(db::Design design) {
    db::DesignDb database;
    database.design() = std::move(design);
    const db::Status status = database.flatten();
    expect(status.ok(), "the implication test design flattens");
    if (!status.ok()) {
        std::cerr << "  " << status.message() << std::endl;
    }
    return database;
}

db::NetId net_named(const db::DesignDb& database, const std::string_view name) {
    const db::FlatNetlist& flat = database.flat();
    for (db::NetId net = 0; net < flat.net_count(); ++net) {
        if (database.strings().at(flat.name_id[net]) == name) {
            return net;
        }
    }
    return db::kInvalidId;
}

struct Fixture {
    db::DesignDb database;
    db::NetId a = db::kInvalidId;
    db::NetId b = db::kInvalidId;
    db::NetId c = db::kInvalidId;
    db::NetId n1 = db::kInvalidId;
    db::NetId n2 = db::kInvalidId;
    db::NetId n3 = db::kInvalidId;
    db::NetId y = db::kInvalidId;
    db::NetId z = db::kInvalidId;
    db::NetId t0 = db::kInvalidId;
    db::NetId t1 = db::kInvalidId;
    db::NetId tb = db::kInvalidId;

    Fixture() : database(flatten(make_design())) {
        a = net_named(database, "a");
        b = net_named(database, "b");
        c = net_named(database, "c");
        n1 = net_named(database, "n1");
        n2 = net_named(database, "n2");
        n3 = net_named(database, "n3");
        y = net_named(database, "y");
        z = net_named(database, "z");
        t0 = net_named(database, "t0");
        t1 = net_named(database, "t1");
        tb = net_named(database, "tb");
        expect(complete(), "the implication fixture has all expected nets");
    }

    bool complete() const {
        return a != db::kInvalidId && b != db::kInvalidId && c != db::kInvalidId &&
               n1 != db::kInvalidId && n2 != db::kInvalidId && n3 != db::kInvalidId &&
               y != db::kInvalidId && z != db::kInvalidId && t0 != db::kInvalidId &&
               t1 != db::kInvalidId && tb != db::kInvalidId;
    }
};

bool reset_ok(atpg::ImplicationEngine& engine) {
    const db::Status status = engine.reset();
    expect(status.ok(), "the implication engine resets");
    if (!status.ok()) {
        std::cerr << "  " << status.message() << std::endl;
    }
    return status.ok();
}

void assign_ok(atpg::ImplicationEngine& engine, const db::NetId net, const atpg::Logic5 value,
               const std::string_view what) {
    const db::Status status = engine.assign(net, value);
    expect(status.ok(), what);
    if (!status.ok()) {
        std::cerr << "  " << status.message() << std::endl;
    }
}

void force_ok(atpg::ImplicationEngine& engine, const db::NetId net, const atpg::Logic5 value,
              const std::string_view what) {
    const db::Status status = engine.force_fault(net, value);
    expect(status.ok(), what);
    if (!status.ok()) {
        std::cerr << "  " << status.message() << std::endl;
    }
}

void imply_ok(atpg::ImplicationEngine& engine) {
    const db::Status status = engine.imply();
    expect(status.ok(), "implication reaches a fixpoint");
    if (!status.ok()) {
        std::cerr << "  " << status.message() << std::endl;
    }
}

void expect_value(const atpg::ImplicationEngine& engine, const db::NetId net,
                  const atpg::Logic5 expected, const std::string_view what) {
    expect_logic(engine.value(net), expected, what);
}

// Each row is written from the good/faulty pair definition, not from the
// implementation: D=(1,0), Dbar=(0,1), and a controlling 0 fixes AND no matter
// what the other input is.
void test_primitive_evaluation() {
    using L = atpg::Logic5;
    constexpr L Z = L::Zero;
    constexpr L O = L::One;
    constexpr L X = L::X;
    constexpr L D = L::D;
    constexpr L DB = L::DBar;

    expect_logic(atpg::logic5_invert(Z), O, "NOT 0 is 1");
    expect_logic(atpg::logic5_invert(O), Z, "NOT 1 is 0");
    expect_logic(atpg::logic5_invert(X), X, "NOT X is X");
    expect_logic(atpg::logic5_invert(D), DB, "NOT D is Dbar");
    expect_logic(atpg::logic5_invert(DB), D, "NOT Dbar is D");
    expect(atpg::logic5_known(D), "D carries information");
    expect(!atpg::logic5_known(X), "X carries no information");
    expect(!atpg::logic5_binary(D), "D needs direct evaluation");

    expect_gate(db::GateType::Not, {Z}, O, "primitive NOT 0");
    expect_gate(db::GateType::Not, {O}, Z, "primitive NOT 1");
    expect_gate(db::GateType::Not, {D}, DB, "primitive NOT D");
    expect_gate(db::GateType::Not, {DB}, D, "primitive NOT Dbar");
    expect_gate(db::GateType::Buf, {D}, D, "BUF preserves D");
    expect_gate(db::GateType::Po, {DB}, DB, "PO preserves Dbar");

    expect_gate(db::GateType::And2, {Z, Z}, Z, "AND 0,0");
    expect_gate(db::GateType::And2, {Z, X}, Z, "AND controlling 0 beats X");
    expect_gate(db::GateType::And2, {X, Z}, Z, "AND controlling 0 beats X on the right");
    expect_gate(db::GateType::And2, {O, O}, O, "AND 1,1");
    expect_gate(db::GateType::And2, {O, X}, X, "AND 1,X");
    expect_gate(db::GateType::And2, {D, O}, D, "AND D,1");
    expect_gate(db::GateType::And2, {D, Z}, Z, "AND D,0");
    expect_gate(db::GateType::And2, {D, DB}, Z, "AND D,Dbar");
    expect_gate(db::GateType::And2, {D, X}, X, "AND D,X is conservative");

    expect_gate(db::GateType::Or2, {O, O}, O, "OR 1,1");
    expect_gate(db::GateType::Or2, {O, X}, O, "OR controlling 1 beats X");
    expect_gate(db::GateType::Or2, {X, O}, O, "OR controlling 1 beats X on the right");
    expect_gate(db::GateType::Or2, {Z, Z}, Z, "OR 0,0");
    expect_gate(db::GateType::Or2, {Z, X}, X, "OR 0,X");
    expect_gate(db::GateType::Or2, {D, Z}, D, "OR D,0");
    expect_gate(db::GateType::Or2, {D, O}, O, "OR D,1");
    expect_gate(db::GateType::Or2, {D, DB}, O, "OR D,Dbar");
    expect_gate(db::GateType::Or2, {D, X}, X, "OR D,X is conservative");

    expect_gate(db::GateType::Nand2, {Z, X}, O, "NAND controlling 0");
    expect_gate(db::GateType::Nand2, {O, O}, Z, "NAND 1,1");
    expect_gate(db::GateType::Nand2, {D, O}, DB, "NAND D,1");
    expect_gate(db::GateType::Nor2, {O, X}, Z, "NOR controlling 1");
    expect_gate(db::GateType::Nor2, {Z, Z}, O, "NOR 0,0");
    expect_gate(db::GateType::Nor2, {D, Z}, DB, "NOR D,0");

    expect_gate(db::GateType::Xor2, {D, Z}, D, "XOR D,0");
    expect_gate(db::GateType::Xor2, {D, O}, DB, "XOR D,1");
    expect_gate(db::GateType::Xor2, {D, D}, Z, "XOR D,D");
    expect_gate(db::GateType::Xor2, {D, DB}, O, "XOR D,Dbar");
    expect_gate(db::GateType::Xor2, {D, X}, X, "XOR D,X");
    expect_gate(db::GateType::Xnor2, {D, Z}, DB, "XNOR D,0");
    expect_gate(db::GateType::Xnor2, {D, O}, D, "XNOR D,1");

    expect_gate(db::GateType::Mux2, {O, Z, O}, O, "MUX selects then");
    expect_gate(db::GateType::Mux2, {Z, O, Z}, O, "MUX selects else");
    expect_gate(db::GateType::Mux2, {O, Z, X}, X, "MUX X select is X");
    expect_gate(db::GateType::Mux2, {O, O, X}, X, "MUX X select is X even when data agree");
    expect_gate(db::GateType::Mux2, {O, Z, D}, D, "MUX D select chooses opposite data");
    expect_gate(db::GateType::Mux2, {Z, O, D}, DB, "MUX D select chooses opposite data 2");
    expect_gate(db::GateType::Mux2, {O, O, D}, O, "MUX equal data hides D select");
    expect_gate(db::GateType::Mux2, {D, Z, O}, D, "MUX selects D data");
    expect_gate(db::GateType::Mux2, {Z, D, Z}, D, "MUX selects D data on else");

    expect_gate(db::GateType::Tie0, {}, Z, "TIE0 is 0");
    expect_gate(db::GateType::Tie1, {}, O, "TIE1 is 1");
    expect_gate(db::GateType::Pi, {}, X, "PI is seeded, not evaluated");
    expect_gate(db::GateType::Dff, {Z}, X, "DFF is a state source");
    expect_gate(db::GateType::Dlat, {Z}, X, "DLAT is a state source");
    expect_gate(db::GateType::ScanCell, {Z}, X, "SCAN_CELL is a state source");
    expect_gate(db::GateType::Blackbox, {}, X, "BLACKBOX is opaque");

    expect_gate(db::GateType::And2, {Z}, X, "AND rejects wrong arity");
    expect_gate(db::GateType::Not, {Z, O}, X, "NOT rejects wrong arity");
    expect_gate(db::GateType::Mux2, {Z, O}, X, "MUX rejects wrong arity");
    expect_gate(db::GateType::Tie0, {Z}, X, "TIE0 rejects a fanin");
}

// The expected values below are hand-calculated. In particular, a=0 fixes n1
// even with b=X, while c=1 fixes y even with n1=X.
void test_controlling_propagation() {
    Fixture fixture;
    if (!fixture.complete()) {
        return;
    }

    atpg::ImplicationEngine engine(fixture.database.flat());
    if (!reset_ok(engine)) {
        return;
    }
    expect_value(engine, fixture.t0, atpg::Logic5::Zero, "tie low is implied on reset");
    expect_value(engine, fixture.t1, atpg::Logic5::One, "tie high is implied on reset");
    expect_value(engine, fixture.tb, atpg::Logic5::Zero, "tie low propagates through BUF");
    expect_value(engine, fixture.n1, atpg::Logic5::X, "n1 starts X");
    expect_value(engine, fixture.y, atpg::Logic5::X, "y starts X");

    assign_ok(engine, fixture.a, atpg::Logic5::Zero, "assign a=0");
    imply_ok(engine);
    expect_value(engine, fixture.n1, atpg::Logic5::Zero, "AND controlling 0 fixes n1");
    expect_value(engine, fixture.y, atpg::Logic5::X, "OR with c=X stays X");

    assign_ok(engine, fixture.c, atpg::Logic5::Zero, "assign c=0");
    imply_ok(engine);
    expect_value(engine, fixture.y, atpg::Logic5::Zero, "n1=0 and c=0 fixes y");
    expect_value(engine, fixture.z, atpg::Logic5::One, "NOT propagates y=0");

    atpg::ImplicationEngine or_engine(fixture.database.flat());
    if (!reset_ok(or_engine)) {
        return;
    }
    assign_ok(or_engine, fixture.c, atpg::Logic5::One, "assign c=1");
    imply_ok(or_engine);
    expect_value(or_engine, fixture.n1, atpg::Logic5::X, "n1 is untouched");
    expect_value(or_engine, fixture.y, atpg::Logic5::One, "OR controlling 1 fixes y");
    expect_value(or_engine, fixture.z, atpg::Logic5::Zero, "NOT propagates y=1");

    atpg::ImplicationEngine inverted_engine(fixture.database.flat());
    if (!reset_ok(inverted_engine)) {
        return;
    }
    assign_ok(inverted_engine, fixture.a, atpg::Logic5::Zero, "assign a=0 for NAND/NOR");
    imply_ok(inverted_engine);
    expect_value(inverted_engine, fixture.n2, atpg::Logic5::One, "NAND controlling 0 fixes n2");
    expect_value(inverted_engine, fixture.n3, atpg::Logic5::X, "NOR with b=X stays X");
    assign_ok(inverted_engine, fixture.b, atpg::Logic5::Zero, "assign b=0 for NOR");
    imply_ok(inverted_engine);
    expect_value(inverted_engine, fixture.n3, atpg::Logic5::One,
                 "NOR all non-controlling 0s fixes n3");

    atpg::ImplicationEngine inverted_one_engine(fixture.database.flat());
    if (!reset_ok(inverted_one_engine)) {
        return;
    }
    assign_ok(inverted_one_engine, fixture.a, atpg::Logic5::One, "assign a=1 for NAND/NOR");
    assign_ok(inverted_one_engine, fixture.b, atpg::Logic5::One, "assign b=1 for NAND/NOR");
    imply_ok(inverted_one_engine);
    expect_value(inverted_one_engine, fixture.n2, atpg::Logic5::Zero,
                 "NAND all non-controlling 1s fixes n2");
    expect_value(inverted_one_engine, fixture.n3, atpg::Logic5::Zero, "NOR controlling 1 fixes n3");

    atpg::ImplicationEngine and_one_engine(fixture.database.flat());
    if (!reset_ok(and_one_engine)) {
        return;
    }
    assign_ok(and_one_engine, fixture.a, atpg::Logic5::One, "assign a=1");
    assign_ok(and_one_engine, fixture.b, atpg::Logic5::One, "assign b=1");
    imply_ok(and_one_engine);
    expect_value(and_one_engine, fixture.n1, atpg::Logic5::One, "all non-controlling fixes n1");
    assign_ok(and_one_engine, fixture.c, atpg::Logic5::Zero, "assign c=0");
    imply_ok(and_one_engine);
    expect_value(and_one_engine, fixture.y, atpg::Logic5::One, "n1=1 fixes OR y");
    expect_value(and_one_engine, fixture.z, atpg::Logic5::Zero, "NOT propagates y=1");

    atpg::ImplicationEngine d_engine(fixture.database.flat());
    if (!reset_ok(d_engine)) {
        return;
    }
    force_ok(d_engine, fixture.a, atpg::Logic5::D, "force primary input a to D");
    assign_ok(d_engine, fixture.b, atpg::Logic5::One, "assign b=1");
    imply_ok(d_engine);
    expect_value(d_engine, fixture.n1, atpg::Logic5::D, "AND D,1 remains D");
    assign_ok(d_engine, fixture.c, atpg::Logic5::Zero, "assign c=0");
    imply_ok(d_engine);
    expect_value(d_engine, fixture.y, atpg::Logic5::D, "OR D,0 remains D");
    expect_value(d_engine, fixture.z, atpg::Logic5::DBar, "NOT turns D into Dbar");

    atpg::ImplicationEngine and_control_engine(fixture.database.flat());
    if (!reset_ok(and_control_engine)) {
        return;
    }
    force_ok(and_control_engine, fixture.a, atpg::Logic5::D, "force primary input a to D");
    assign_ok(and_control_engine, fixture.b, atpg::Logic5::Zero, "assign b=0");
    imply_ok(and_control_engine);
    expect_value(and_control_engine, fixture.n1, atpg::Logic5::Zero,
                 "AND controlling 0 overrides D");
    assign_ok(and_control_engine, fixture.c, atpg::Logic5::Zero, "assign c=0");
    imply_ok(and_control_engine);
    expect_value(and_control_engine, fixture.y, atpg::Logic5::Zero, "y follows n1=0");
    expect_value(and_control_engine, fixture.z, atpg::Logic5::One, "z follows y=0");

    atpg::ImplicationEngine or_control_engine(fixture.database.flat());
    if (!reset_ok(or_control_engine)) {
        return;
    }
    force_ok(or_control_engine, fixture.a, atpg::Logic5::D, "force primary input a to D");
    assign_ok(or_control_engine, fixture.b, atpg::Logic5::One, "assign b=1");
    assign_ok(or_control_engine, fixture.c, atpg::Logic5::One, "assign c=1");
    imply_ok(or_control_engine);
    expect_value(or_control_engine, fixture.n1, atpg::Logic5::D, "AND D,1 remains D");
    expect_value(or_control_engine, fixture.y, atpg::Logic5::One, "OR controlling 1 overrides D");
    expect_value(or_control_engine, fixture.z, atpg::Logic5::Zero, "z follows y=1");
}

// n1 is driven by g_and. Forcing n1 before a and b are assigned must survive
// the driver's later implication: with a=b=1 the driver's good value is 1,
// which is compatible with D=(1,0). The D value still propagates downstream.
void test_internal_fault_force() {
    Fixture fixture;
    if (!fixture.complete()) {
        return;
    }

    {
        atpg::ImplicationEngine engine(fixture.database.flat());
        if (!reset_ok(engine)) {
            return;
        }
        force_ok(engine, fixture.n1, atpg::Logic5::D, "force internal n1 to D");
        expect_value(engine, fixture.n1, atpg::Logic5::D, "forced n1 starts D");

        assign_ok(engine, fixture.a, atpg::Logic5::One, "assign a=1");
        assign_ok(engine, fixture.b, atpg::Logic5::One, "assign b=1");
        imply_ok(engine);
        expect(!engine.conflict(), "compatible driver does not conflict with forced D");
        expect_value(engine, fixture.n1, atpg::Logic5::D, "driver 1 does not overwrite forced D");
        expect_value(engine, fixture.y, atpg::Logic5::X, "OR D,X stays X");

        assign_ok(engine, fixture.c, atpg::Logic5::Zero, "assign c=0");
        imply_ok(engine);
        expect_value(engine, fixture.y, atpg::Logic5::D, "forced D propagates through OR");
        expect_value(engine, fixture.z, atpg::Logic5::DBar, "forced D reaches NOT as Dbar");
    }

    {
        atpg::ImplicationEngine engine(fixture.database.flat());
        if (!reset_ok(engine)) {
            return;
        }
        force_ok(engine, fixture.n1, atpg::Logic5::DBar, "force internal n1 to Dbar");
        assign_ok(engine, fixture.a, atpg::Logic5::Zero, "assign a=0");
        assign_ok(engine, fixture.b, atpg::Logic5::One, "assign b=1");
        imply_ok(engine);
        expect(!engine.conflict(), "compatible driver does not conflict with forced Dbar");
        expect_value(engine, fixture.n1, atpg::Logic5::DBar,
                     "driver 0 does not overwrite forced Dbar");
        assign_ok(engine, fixture.c, atpg::Logic5::Zero, "assign c=0");
        imply_ok(engine);
        expect_value(engine, fixture.y, atpg::Logic5::DBar, "forced Dbar propagates through OR");
        expect_value(engine, fixture.z, atpg::Logic5::D, "forced Dbar reaches NOT as D");
    }

    {
        atpg::ImplicationEngine engine(fixture.database.flat());
        if (!reset_ok(engine)) {
            return;
        }
        force_ok(engine, fixture.n1, atpg::Logic5::D, "force internal n1 to D");
        assign_ok(engine, fixture.a, atpg::Logic5::Zero, "assign a=0");
        assign_ok(engine, fixture.b, atpg::Logic5::One, "assign b=1");
        imply_ok(engine);
        expect(engine.conflict(), "driver good value 0 contradicts forced D");
        expect_value(engine, fixture.n1, atpg::Logic5::D, "conflict leaves forced D intact");
    }

    {
        atpg::ImplicationEngine engine(fixture.database.flat());
        if (!reset_ok(engine)) {
            return;
        }
        const db::Status rejected = engine.force_fault(fixture.n1, atpg::Logic5::One);
        expect(!rejected.ok(), "forcing a binary value is rejected");
    }
}

void test_assign_rejects_defect_values() {
    Fixture fixture;
    if (!fixture.complete()) {
        return;
    }

    atpg::ImplicationEngine engine(fixture.database.flat());
    if (!reset_ok(engine)) {
        return;
    }

    const db::Status assign_d = engine.assign(fixture.n1, atpg::Logic5::D);
    expect(!assign_d.ok(), "assign rejects D");
    const db::Status assign_dbar = engine.assign(fixture.n1, atpg::Logic5::DBar);
    expect(!assign_dbar.ok(), "assign rejects Dbar");
    expect(!engine.conflict(), "rejected defect assignments do not set conflict");
    expect_value(engine, fixture.n1, atpg::Logic5::X, "rejected assignments leave n1 X");
}

void test_force_checkpoint_restore() {
    Fixture fixture;
    if (!fixture.complete()) {
        return;
    }

    atpg::ImplicationEngine engine(fixture.database.flat());
    if (!reset_ok(engine)) {
        return;
    }
    const atpg::ImplicationEngine::Checkpoint before = engine.checkpoint();
    force_ok(engine, fixture.n1, atpg::Logic5::D, "force n1 before checkpoint");
    const atpg::ImplicationEngine::Checkpoint forced = engine.checkpoint();

    assign_ok(engine, fixture.a, atpg::Logic5::One, "branch assigns a=1");
    assign_ok(engine, fixture.b, atpg::Logic5::One, "branch assigns b=1");
    imply_ok(engine);
    assign_ok(engine, fixture.c, atpg::Logic5::Zero, "branch assigns c=0");
    imply_ok(engine);
    expect_value(engine, fixture.n1, atpg::Logic5::D, "branch keeps forced D");
    expect_value(engine, fixture.y, atpg::Logic5::D, "branch propagates forced D");
    expect_value(engine, fixture.z, atpg::Logic5::DBar, "branch propagates Dbar");

    const db::Status restored_forced = engine.restore(forced);
    expect(restored_forced.ok(), "restore the forced checkpoint");
    if (!restored_forced.ok()) {
        return;
    }
    expect_value(engine, fixture.a, atpg::Logic5::X, "forced restore removes a");
    expect_value(engine, fixture.b, atpg::Logic5::X, "forced restore removes b");
    expect_value(engine, fixture.c, atpg::Logic5::X, "forced restore removes c");
    expect_value(engine, fixture.n1, atpg::Logic5::D, "forced restore keeps D");

    assign_ok(engine, fixture.a, atpg::Logic5::One, "post-restore a=1");
    assign_ok(engine, fixture.b, atpg::Logic5::One, "post-restore b=1");
    imply_ok(engine);
    expect(!engine.conflict(), "post-restore driver is compatible with forced D");
    expect_value(engine, fixture.n1, atpg::Logic5::D, "forced restore keeps the force flag");

    const db::Status restored_before = engine.restore(before);
    expect(restored_before.ok(), "restore the pre-force checkpoint");
    if (!restored_before.ok()) {
        return;
    }
    expect_value(engine, fixture.n1, atpg::Logic5::X, "pre-force restore clears n1");
    assign_ok(engine, fixture.a, atpg::Logic5::One, "unforced a=1");
    assign_ok(engine, fixture.b, atpg::Logic5::One, "unforced b=1");
    imply_ok(engine);
    expect(!engine.conflict(), "unforced driver can set n1");
    expect_value(engine, fixture.n1, atpg::Logic5::One, "pre-force restore clears the force flag");
}

void test_checkpoint_restore() {
    Fixture fixture;
    if (!fixture.complete()) {
        return;
    }

    // The checkpoint is taken with g_and still queued. Restoring it must put
    // back the pending queue as well as the a=0 assignment.
    {
        atpg::ImplicationEngine engine(fixture.database.flat());
        if (!reset_ok(engine)) {
            return;
        }
        assign_ok(engine, fixture.a, atpg::Logic5::Zero, "assign a=0 before checkpoint");
        const atpg::ImplicationEngine::Checkpoint checkpoint = engine.checkpoint();

        assign_ok(engine, fixture.b, atpg::Logic5::One, "branch assigns b=1");
        imply_ok(engine);
        assign_ok(engine, fixture.c, atpg::Logic5::Zero, "branch assigns c=0");
        imply_ok(engine);
        expect_value(engine, fixture.n1, atpg::Logic5::Zero, "branch fixes n1");
        expect_value(engine, fixture.y, atpg::Logic5::Zero, "branch fixes y");
        expect_value(engine, fixture.z, atpg::Logic5::One, "branch fixes z");

        const db::Status restored = engine.restore(checkpoint);
        expect(restored.ok(), "restore a checkpoint with a pending queue");
        if (!restored.ok()) {
            std::cerr << "  " << restored.message() << std::endl;
            return;
        }
        expect(!engine.conflict(), "restore clears the conflict flag");
        expect_value(engine, fixture.b, atpg::Logic5::X, "restore removes b");
        expect_value(engine, fixture.c, atpg::Logic5::X, "restore removes c");
        expect_value(engine, fixture.n1, atpg::Logic5::X, "restore removes n1");
        expect_value(engine, fixture.y, atpg::Logic5::X, "restore removes y");
        expect_value(engine, fixture.z, atpg::Logic5::X, "restore removes z");

        imply_ok(engine);
        expect_value(engine, fixture.n1, atpg::Logic5::Zero, "restored queue fixes n1");
        expect_value(engine, fixture.y, atpg::Logic5::X, "c is still X after restore");
        assign_ok(engine, fixture.c, atpg::Logic5::Zero, "assign c=0 after restore");
        imply_ok(engine);
        expect_value(engine, fixture.y, atpg::Logic5::Zero, "restored path fixes y");
        expect_value(engine, fixture.z, atpg::Logic5::One, "restored path fixes z");
    }

    // a=1 makes b non-controlling for the AND, so this also checks that restore
    // puts the occurrence counts back: b=0 in the branch would otherwise leave
    // a stale controlling count that makes b=1 produce 0.
    {
        atpg::ImplicationEngine engine(fixture.database.flat());
        if (!reset_ok(engine)) {
            return;
        }
        assign_ok(engine, fixture.a, atpg::Logic5::One, "assign a=1");
        imply_ok(engine);
        const atpg::ImplicationEngine::Checkpoint checkpoint = engine.checkpoint();

        assign_ok(engine, fixture.b, atpg::Logic5::Zero, "branch assigns b=0");
        imply_ok(engine);
        expect_value(engine, fixture.n1, atpg::Logic5::Zero, "branch controlling b fixes n1");

        assign_ok(engine, fixture.n1, atpg::Logic5::One, "force a conflicting n1");
        expect(engine.conflict(), "opposite assignment sets the conflict flag");
        expect_value(engine, fixture.n1, atpg::Logic5::Zero, "conflict does not overwrite n1");

        const db::Status restored = engine.restore(checkpoint);
        expect(restored.ok(), "restore a conflicting branch");
        if (!restored.ok()) {
            std::cerr << "  " << restored.message() << std::endl;
            return;
        }
        expect(!engine.conflict(), "restore clears the branch conflict");
        expect_value(engine, fixture.b, atpg::Logic5::X, "restore removes branch b");
        expect_value(engine, fixture.n1, atpg::Logic5::X, "restore removes branch n1");
        expect_value(engine, fixture.t0, atpg::Logic5::Zero, "restore keeps tie constants");

        assign_ok(engine, fixture.b, atpg::Logic5::One, "assign b=1 after restore");
        imply_ok(engine);
        expect_value(engine, fixture.n1, atpg::Logic5::One,
                     "restored counts make a=1,b=1 produce 1");
        expect_value(engine, fixture.y, atpg::Logic5::One, "n1=1 fixes OR y");
        expect_value(engine, fixture.z, atpg::Logic5::Zero, "z follows y=1");
    }
}

}  // namespace

int main() {
    test_primitive_evaluation();
    test_controlling_propagation();
    test_internal_fault_force();
    test_assign_rejects_defect_values();
    test_force_checkpoint_restore();
    test_checkpoint_restore();

    if (failures != 0) {
        std::cerr << failures << " implication test(s) failed" << std::endl;
        return 1;
    }
    std::cout << "implication tests passed" << std::endl;
    return 0;
}
