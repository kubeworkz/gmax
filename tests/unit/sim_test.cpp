// WP-005: the scalar four-state reference and its 64-lane encoding. Framework-
// free for the same reason as the other tests: third_party stays empty until
// TBD-11 closes.

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include "core/db/db.h"
#include "core/sim/logic.h"

namespace db = gmax::db;
namespace sim = gmax::sim;

namespace {

int failures = 0;

void expect(bool ok, std::string_view what) {
    if (!ok) {
        std::cerr << "FAIL " << what << std::endl;
        ++failures;
    }
}

void expect_logic(sim::Logic4 actual, sim::Logic4 expected, std::string_view what) {
    if (actual != expected) {
        std::cerr << "FAIL " << what << ": expected " << sim::logic4_name(expected) << ", got "
                  << sim::logic4_name(actual) << std::endl;
        ++failures;
    }
}

constexpr std::array<sim::Logic4, 4> kValues = {sim::Logic4::Zero, sim::Logic4::One, sim::Logic4::X,
                                                sim::Logic4::Z};

bool known(const sim::Logic4 value) { return sim::logic4_known(value); }
bool one(const sim::Logic4 value) { return value == sim::Logic4::One; }

sim::Logic4 ref_not(sim::Logic4 value) {
    value = sim::logic4_normalized(value);
    if (value == sim::Logic4::Zero) {
        return sim::Logic4::One;
    }
    if (value == sim::Logic4::One) {
        return sim::Logic4::Zero;
    }
    return sim::Logic4::X;
}

sim::Logic4 ref_and(sim::Logic4 left, sim::Logic4 right) {
    left = sim::logic4_normalized(left);
    right = sim::logic4_normalized(right);
    if ((known(left) && !one(left)) || (known(right) && !one(right))) {
        return sim::Logic4::Zero;
    }
    if (known(left) && known(right)) {
        return sim::Logic4::One;
    }
    return sim::Logic4::X;
}

sim::Logic4 ref_or(sim::Logic4 left, sim::Logic4 right) {
    left = sim::logic4_normalized(left);
    right = sim::logic4_normalized(right);
    if ((known(left) && one(left)) || (known(right) && one(right))) {
        return sim::Logic4::One;
    }
    if (known(left) && known(right)) {
        return sim::Logic4::Zero;
    }
    return sim::Logic4::X;
}

sim::Logic4 ref_xor(sim::Logic4 left, sim::Logic4 right) {
    left = sim::logic4_normalized(left);
    right = sim::logic4_normalized(right);
    if (!known(left) || !known(right)) {
        return sim::Logic4::X;
    }
    return left == right ? sim::Logic4::Zero : sim::Logic4::One;
}

sim::Logic4 ref_mux(sim::Logic4 then_value, sim::Logic4 else_value, sim::Logic4 select) {
    then_value = sim::logic4_normalized(then_value);
    else_value = sim::logic4_normalized(else_value);
    select = sim::logic4_normalized(select);
    if (!known(select)) {
        return sim::Logic4::X;
    }
    return one(select) ? then_value : else_value;
}

struct MuxXRow {
    sim::Logic4 then_value;
    sim::Logic4 else_value;
    sim::Logic4 expected;
};

// An unknown select is conservative X for every data pair, including equal data.
// This table is explicit rather than derived from ref_mux.
constexpr std::array<MuxXRow, 16> kMuxXTable = {{
    {sim::Logic4::Zero, sim::Logic4::Zero, sim::Logic4::X},
    {sim::Logic4::Zero, sim::Logic4::One, sim::Logic4::X},
    {sim::Logic4::Zero, sim::Logic4::X, sim::Logic4::X},
    {sim::Logic4::Zero, sim::Logic4::Z, sim::Logic4::X},
    {sim::Logic4::One, sim::Logic4::Zero, sim::Logic4::X},
    {sim::Logic4::One, sim::Logic4::One, sim::Logic4::X},
    {sim::Logic4::One, sim::Logic4::X, sim::Logic4::X},
    {sim::Logic4::One, sim::Logic4::Z, sim::Logic4::X},
    {sim::Logic4::X, sim::Logic4::Zero, sim::Logic4::X},
    {sim::Logic4::X, sim::Logic4::One, sim::Logic4::X},
    {sim::Logic4::X, sim::Logic4::X, sim::Logic4::X},
    {sim::Logic4::X, sim::Logic4::Z, sim::Logic4::X},
    {sim::Logic4::Z, sim::Logic4::Zero, sim::Logic4::X},
    {sim::Logic4::Z, sim::Logic4::One, sim::Logic4::X},
    {sim::Logic4::Z, sim::Logic4::X, sim::Logic4::X},
    {sim::Logic4::Z, sim::Logic4::Z, sim::Logic4::X},
}};

sim::Logic4 ref_unary(const db::GateType type, const sim::Logic4 input) {
    switch (type) {
    case db::GateType::Not:
        return ref_not(input);
    case db::GateType::Buf:
    case db::GateType::Po:
        return sim::logic4_normalized(input);
    default:
        return sim::Logic4::X;
    }
}

sim::Logic4 ref_binary(const db::GateType type, const sim::Logic4 left, const sim::Logic4 right) {
    switch (type) {
    case db::GateType::And2:
        return ref_and(left, right);
    case db::GateType::Or2:
        return ref_or(left, right);
    case db::GateType::Xor2:
        return ref_xor(left, right);
    case db::GateType::Nand2:
        return ref_not(ref_and(left, right));
    case db::GateType::Nor2:
        return ref_not(ref_or(left, right));
    case db::GateType::Xnor2:
        return ref_not(ref_xor(left, right));
    default:
        return sim::Logic4::X;
    }
}

void test_scalar_gates() {
    constexpr std::array<db::GateType, 6> binary_types = {
        db::GateType::And2,  db::GateType::Or2,  db::GateType::Xor2,
        db::GateType::Nand2, db::GateType::Nor2, db::GateType::Xnor2,
    };
    for (const db::GateType type : binary_types) {
        for (const sim::Logic4 left : kValues) {
            for (const sim::Logic4 right : kValues) {
                const std::array<sim::Logic4, 2> fanins = {left, right};
                expect_logic(sim::eval_gate(type, std::span<const sim::Logic4>(fanins)),
                             ref_binary(type, left, right), "binary gate truth table");
            }
        }
    }

    constexpr std::array<db::GateType, 3> unary_types = {
        db::GateType::Not,
        db::GateType::Buf,
        db::GateType::Po,
    };
    for (const db::GateType type : unary_types) {
        for (const sim::Logic4 input : kValues) {
            const std::array<sim::Logic4, 1> fanins = {input};
            expect_logic(sim::eval_gate(type, std::span<const sim::Logic4>(fanins)),
                         ref_unary(type, input), "unary gate truth table");
        }
    }

    for (const sim::Logic4 then_value : kValues) {
        for (const sim::Logic4 else_value : kValues) {
            for (const sim::Logic4 select : kValues) {
                const std::array<sim::Logic4, 3> fanins = {then_value, else_value, select};
                expect_logic(
                    sim::eval_gate(db::GateType::Mux2, std::span<const sim::Logic4>(fanins)),
                    ref_mux(then_value, else_value, select), "mux truth table");
            }
        }
    }

    const std::array<sim::Logic4, 0> no_fanins = {};
    expect_logic(sim::eval_gate(db::GateType::Tie0, std::span<const sim::Logic4>(no_fanins)),
                 sim::Logic4::Zero, "TIE0 is zero");
    expect_logic(sim::eval_gate(db::GateType::Tie1, std::span<const sim::Logic4>(no_fanins)),
                 sim::Logic4::One, "TIE1 is one");

    constexpr std::array<db::GateType, 5> stateful_types = {
        db::GateType::Pi,       db::GateType::Dff,      db::GateType::Dlat,
        db::GateType::ScanCell, db::GateType::Blackbox,
    };
    for (const db::GateType type : stateful_types) {
        expect_logic(sim::eval_gate(type, std::span<const sim::Logic4>(no_fanins)), sim::Logic4::X,
                     "stateful and boundary gates are not combinational");
    }

    const std::array<sim::Logic4, 1> one_input = {sim::Logic4::Zero};
    const std::array<sim::Logic4, 2> two_inputs = {sim::Logic4::Zero, sim::Logic4::One};
    expect_logic(sim::eval_gate(db::GateType::And2, std::span<const sim::Logic4>(one_input)),
                 sim::Logic4::X, "AND2 with one fanin is malformed");
    expect_logic(sim::eval_gate(db::GateType::Mux2, std::span<const sim::Logic4>(two_inputs)),
                 sim::Logic4::X, "MUX2 with two fanins is malformed");
    expect_logic(sim::eval_gate(db::GateType::Not, std::span<const sim::Logic4>(no_fanins)),
                 sim::Logic4::X, "NOT without a fanin is malformed");
    const std::array<sim::Logic4, 1> tie_input = {sim::Logic4::One};
    expect_logic(sim::eval_gate(db::GateType::Tie0, std::span<const sim::Logic4>(tie_input)),
                 sim::Logic4::X, "TIE0 with a fanin is malformed");

    const std::array<sim::Logic4, 2> and_zero_unknown = {sim::Logic4::Zero, sim::Logic4::X};
    const std::array<sim::Logic4, 2> and_one_unknown = {sim::Logic4::One, sim::Logic4::X};
    const std::array<sim::Logic4, 2> or_one_unknown = {sim::Logic4::One, sim::Logic4::X};
    const std::array<sim::Logic4, 2> or_zero_unknown = {sim::Logic4::Zero, sim::Logic4::X};
    expect_logic(sim::eval_gate(db::GateType::And2, std::span<const sim::Logic4>(and_zero_unknown)),
                 sim::Logic4::Zero, "AND with a zero is known zero");
    expect_logic(sim::eval_gate(db::GateType::And2, std::span<const sim::Logic4>(and_one_unknown)),
                 sim::Logic4::X, "AND with one and X stays X");
    expect_logic(sim::eval_gate(db::GateType::Or2, std::span<const sim::Logic4>(or_one_unknown)),
                 sim::Logic4::One, "OR with a one is known one");
    expect_logic(sim::eval_gate(db::GateType::Or2, std::span<const sim::Logic4>(or_zero_unknown)),
                 sim::Logic4::X, "OR with zero and X stays X");

    const std::array<sim::Logic4, 3> mux_equal = {sim::Logic4::One, sim::Logic4::One,
                                                  sim::Logic4::X};
    const std::array<sim::Logic4, 3> mux_different = {sim::Logic4::Zero, sim::Logic4::One,
                                                      sim::Logic4::X};
    expect_logic(sim::eval_gate(db::GateType::Mux2, std::span<const sim::Logic4>(mux_equal)),
                 sim::Logic4::X, "mux is X when select is X even if data agree");
    expect_logic(sim::eval_gate(db::GateType::Mux2, std::span<const sim::Logic4>(mux_different)),
                 sim::Logic4::X, "mux is X when data disagree under X select");

    const std::array<sim::Logic4, 1> z_input = {sim::Logic4::Z};
    expect_logic(sim::eval_gate(db::GateType::Not, std::span<const sim::Logic4>(z_input)),
                 sim::Logic4::X, "Z is treated as X by a gate");
}

void test_mux_x_select_table() {
    for (const MuxXRow& row : kMuxXTable) {
        const std::array<sim::Logic4, 3> fanins = {row.then_value, row.else_value, sim::Logic4::X};
        expect_logic(sim::eval_gate(db::GateType::Mux2, std::span<const sim::Logic4>(fanins)),
                     row.expected, "MUX2 X-select truth table");
    }

    const std::array<sim::Logic4, 3> z_select = {sim::Logic4::One, sim::Logic4::One,
                                                 sim::Logic4::Z};
    expect_logic(sim::eval_gate(db::GateType::Mux2, std::span<const sim::Logic4>(z_select)),
                 sim::Logic4::X, "MUX2 Z select is X");
}

void test_word_lanes() {
    sim::LogicWord word;
    for (std::size_t lane = 0; lane < sim::LogicWord::kLanes; ++lane) {
        word.set_lane(lane, kValues[lane % kValues.size()]);
    }
    for (std::size_t lane = 0; lane < sim::LogicWord::kLanes; ++lane) {
        const sim::Logic4 expected = sim::logic4_normalized(kValues[lane % kValues.size()]);
        expect_logic(word.lane(lane), expected, "word lane round trip");
    }

    for (const sim::Logic4 value : kValues) {
        const sim::LogicWord splatted = sim::LogicWord::splat(value);
        for (std::size_t lane = 0; lane < sim::LogicWord::kLanes; ++lane) {
            expect_logic(splatted.lane(lane), sim::logic4_normalized(value), "splatted word lane");
        }
    }

    word.set_lane(sim::LogicWord::kLanes, sim::Logic4::One);
    expect_logic(word.lane(sim::LogicWord::kLanes), sim::Logic4::X, "out-of-range lane reads X");
    expect_logic(word.lane(1000), sim::Logic4::X, "far out-of-range lane reads X");
}

void test_word_gates() {
    constexpr std::array<db::GateType, 3> unary_types = {
        db::GateType::Not,
        db::GateType::Buf,
        db::GateType::Po,
    };
    for (const db::GateType type : unary_types) {
        std::array<sim::LogicWord, 1> fanins;
        for (std::size_t lane = 0; lane < kValues.size(); ++lane) {
            fanins[0].set_lane(lane, kValues[lane]);
        }
        const sim::LogicWord out = sim::eval_gate(type, std::span<const sim::LogicWord>(fanins));
        for (std::size_t lane = 0; lane < sim::LogicWord::kLanes; ++lane) {
            const sim::Logic4 input = lane < kValues.size() ? kValues[lane] : sim::Logic4::X;
            expect_logic(out.lane(lane), ref_unary(type, input), "word unary gate lane");
        }
    }

    constexpr std::array<db::GateType, 6> binary_types = {
        db::GateType::And2,  db::GateType::Or2,  db::GateType::Xor2,
        db::GateType::Nand2, db::GateType::Nor2, db::GateType::Xnor2,
    };
    for (const db::GateType type : binary_types) {
        std::array<sim::LogicWord, 2> fanins;
        for (std::size_t lane = 0; lane < 16; ++lane) {
            fanins[0].set_lane(lane, kValues[lane / 4]);
            fanins[1].set_lane(lane, kValues[lane % 4]);
        }
        const sim::LogicWord out = sim::eval_gate(type, std::span<const sim::LogicWord>(fanins));
        for (std::size_t lane = 0; lane < sim::LogicWord::kLanes; ++lane) {
            const sim::Logic4 left = lane < 16 ? kValues[lane / 4] : sim::Logic4::X;
            const sim::Logic4 right = lane < 16 ? kValues[lane % 4] : sim::Logic4::X;
            expect_logic(out.lane(lane), ref_binary(type, left, right), "word binary gate lane");
        }
    }

    std::array<sim::LogicWord, 3> mux_fanins;
    for (std::size_t lane = 0; lane < sim::LogicWord::kLanes; ++lane) {
        mux_fanins[0].set_lane(lane, kValues[lane / 16]);
        mux_fanins[1].set_lane(lane, kValues[(lane / 4) % 4]);
        mux_fanins[2].set_lane(lane, kValues[lane % 4]);
    }
    const sim::LogicWord mux_out =
        sim::eval_gate(db::GateType::Mux2, std::span<const sim::LogicWord>(mux_fanins));
    for (std::size_t lane = 0; lane < sim::LogicWord::kLanes; ++lane) {
        const sim::Logic4 then_value = kValues[lane / 16];
        const sim::Logic4 else_value = kValues[(lane / 4) % 4];
        const sim::Logic4 select = kValues[lane % 4];
        expect_logic(mux_out.lane(lane), ref_mux(then_value, else_value, select), "word mux lane");
    }

    const std::array<sim::LogicWord, 0> no_fanins = {};
    for (const db::GateType type : {db::GateType::Tie0, db::GateType::Tie1}) {
        const sim::LogicWord out = sim::eval_gate(type, std::span<const sim::LogicWord>(no_fanins));
        const sim::Logic4 expected =
            type == db::GateType::Tie0 ? sim::Logic4::Zero : sim::Logic4::One;
        for (std::size_t lane = 0; lane < sim::LogicWord::kLanes; ++lane) {
            expect_logic(out.lane(lane), expected, "word constant gate lane");
        }
    }

    constexpr std::array<db::GateType, 5> stateful_types = {
        db::GateType::Pi,       db::GateType::Dff,      db::GateType::Dlat,
        db::GateType::ScanCell, db::GateType::Blackbox,
    };
    for (const db::GateType type : stateful_types) {
        const sim::LogicWord out = sim::eval_gate(type, std::span<const sim::LogicWord>(no_fanins));
        for (std::size_t lane = 0; lane < sim::LogicWord::kLanes; ++lane) {
            expect_logic(out.lane(lane), sim::Logic4::X, "word stateful gate lane is unknown");
        }
    }

    std::array<sim::LogicWord, 1> one_fanin;
    std::array<sim::LogicWord, 2> two_fanins;
    expect_logic(
        sim::eval_gate(db::GateType::And2, std::span<const sim::LogicWord>(one_fanin)).lane(0),
        sim::Logic4::X, "word AND2 with one fanin is malformed");
    expect_logic(
        sim::eval_gate(db::GateType::Mux2, std::span<const sim::LogicWord>(two_fanins)).lane(0),
        sim::Logic4::X, "word MUX2 with two fanins is malformed");
    expect_logic(
        sim::eval_gate(db::GateType::Not, std::span<const sim::LogicWord>(no_fanins)).lane(0),
        sim::Logic4::X, "word NOT without a fanin is malformed");
    std::array<sim::LogicWord, 1> tie_input;
    expect_logic(
        sim::eval_gate(db::GateType::Tie0, std::span<const sim::LogicWord>(tie_input)).lane(0),
        sim::Logic4::X, "word TIE0 with a fanin is malformed");
}

void test_netlist_differential() {
    db::Design design;
    design.top = "top";
    db::Module top;
    top.name = "top";
    top.inputs = {"a", "b", "s"};
    top.outputs = {"y"};
    top.gates = {
        {db::GateType::And2,
         "g1",
         {"a", "b"},
         "n1",
         db::kInvalidId,
         "",
         "",
         "",
         db::ClockEdge::Rising},
        {db::GateType::Xor2,
         "g2",
         {"a", "b"},
         "n2",
         db::kInvalidId,
         "",
         "",
         "",
         db::ClockEdge::Rising},
        {db::GateType::Not, "g3", {"n2"}, "n3", db::kInvalidId, "", "", "", db::ClockEdge::Rising},
        {db::GateType::Mux2,
         "g4",
         {"n1", "n3", "s"},
         "y",
         db::kInvalidId,
         "",
         "",
         "",
         db::ClockEdge::Rising},
    };
    design.modules = {top};

    db::DesignDb database;
    database.design() = std::move(design);
    const db::Status flattened = database.flatten();
    expect(flattened.ok(), "the WP-005 test netlist flattens");
    if (!flattened.ok()) {
        std::cerr << "  " << flattened.message() << std::endl;
        return;
    }

    const db::FlatNetlist& flat = database.flat();
    const auto net_named = [&database, &flat](const std::string_view name) {
        for (db::NetId net = 0; net < flat.net_count(); ++net) {
            if (database.strings().at(flat.name_id[net]) == name) {
                return net;
            }
        }
        return static_cast<db::NetId>(db::kInvalidId);
    };

    const db::NetId pi_a = net_named("a");
    const db::NetId pi_b = net_named("b");
    const db::NetId pi_s = net_named("s");
    const db::NetId po_y = net_named("y");
    expect(pi_a != db::kInvalidId && pi_b != db::kInvalidId && pi_s != db::kInvalidId &&
               po_y != db::kInvalidId,
           "the test netlist has its primary inputs and output");
    if (pi_a == db::kInvalidId || pi_b == db::kInvalidId || pi_s == db::kInvalidId ||
        po_y == db::kInvalidId) {
        return;
    }

    std::vector<std::vector<sim::Logic4>> scalar_values;
    scalar_values.reserve(64);
    for (std::size_t pattern = 0; pattern < 64; ++pattern) {
        const std::array<sim::Logic4, 3> input = {
            (pattern & 1) != 0 ? sim::Logic4::One : sim::Logic4::Zero,
            (pattern & 2) != 0 ? sim::Logic4::One : sim::Logic4::Zero,
            (pattern & 4) != 0 ? sim::Logic4::One : sim::Logic4::Zero,
        };
        std::vector<sim::Logic4> values(flat.net_count(), sim::Logic4::X);
        values[pi_a] = input[0];
        values[pi_b] = input[1];
        values[pi_s] = input[2];

        const db::Status evaluated = sim::evaluate(flat, values);
        expect(evaluated.ok(), "scalar netlist evaluation succeeds");
        if (!evaluated.ok()) {
            std::cerr << "  " << evaluated.message() << std::endl;
            return;
        }

        const sim::Logic4 expected =
            ref_mux(ref_and(input[0], input[1]), ref_not(ref_xor(input[0], input[1])), input[2]);
        expect_logic(values[po_y], expected, "scalar netlist output");
        scalar_values.push_back(std::move(values));
    }

    std::vector<sim::LogicWord> word_values(flat.net_count());
    for (std::size_t pattern = 0; pattern < 64; ++pattern) {
        const std::array<sim::Logic4, 3> input = {
            (pattern & 1) != 0 ? sim::Logic4::One : sim::Logic4::Zero,
            (pattern & 2) != 0 ? sim::Logic4::One : sim::Logic4::Zero,
            (pattern & 4) != 0 ? sim::Logic4::One : sim::Logic4::Zero,
        };
        word_values[pi_a].set_lane(pattern, input[0]);
        word_values[pi_b].set_lane(pattern, input[1]);
        word_values[pi_s].set_lane(pattern, input[2]);
    }

    const db::Status word_evaluated = sim::evaluate(flat, word_values);
    expect(word_evaluated.ok(), "64-lane netlist evaluation succeeds");
    if (!word_evaluated.ok()) {
        std::cerr << "  " << word_evaluated.message() << std::endl;
        return;
    }

    for (std::size_t pattern = 0; pattern < 64; ++pattern) {
        for (db::NetId net = 0; net < flat.net_count(); ++net) {
            expect_logic(word_values[net].lane(pattern), scalar_values[pattern][net],
                         "64-lane netlist matches the scalar evaluator");
        }
    }

    std::vector<sim::Logic4> short_values;
    expect(sim::evaluate(flat, short_values).error() == db::Error::BadPayload,
           "a short scalar value array is rejected");
    std::vector<sim::LogicWord> short_words;
    expect(sim::evaluate(flat, short_words).error() == db::Error::BadPayload,
           "a short word value array is rejected");

    const db::GateId y_driver = flat.driver[po_y];
    if (y_driver < flat.gate_count()) {
        db::FlatNetlist bad_levels = flat;
        bad_levels.level[y_driver] = 0;
        std::vector<sim::Logic4> cycle_values(flat.net_count(), sim::Logic4::X);
        expect(sim::evaluate(bad_levels, cycle_values).error() == db::Error::CycleDetected,
               "a combinational fanin at the consumer's level is rejected");
    }
}

}  // namespace

int main() {
    test_scalar_gates();
    test_mux_x_select_table();
    test_word_lanes();
    test_word_gates();
    test_netlist_differential();

    if (failures != 0) {
        std::cerr << failures << " failure(s)" << std::endl;
        return 1;
    }
    std::cout << "sim: all tests passed" << std::endl;
    return 0;
}
