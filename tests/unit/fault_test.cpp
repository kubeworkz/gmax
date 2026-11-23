// WP-009: the scalar fault reference and the 64-fault word engine. Framework-
// free for the same reason as the other tests: third_party stays empty until
// TBD-11 closes.

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/db/db.h"
#include "core/sim/fault.h"

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

db::Gate gate(db::GateType type, std::string name, std::vector<std::string> fanins,
              std::string out) {
    db::Gate result;
    result.type = type;
    result.name = std::move(name);
    result.fanins = std::move(fanins);
    result.out = std::move(out);
    return result;
}

db::DesignDb flatten(db::Design design) {
    db::DesignDb database;
    database.design() = std::move(design);
    const db::Status status = database.flatten();
    expect(status.ok(), "test design flattens");
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

std::vector<sim::Logic4>
make_pattern(const db::DesignDb& database,
             const std::initializer_list<std::pair<std::string_view, sim::Logic4>> seeds) {
    std::vector<sim::Logic4> values(database.flat().net_count(), sim::Logic4::X);
    for (const auto& [name, value] : seeds) {
        const db::NetId net = net_named(database, name);
        expect(net != db::kInvalidId, "seed net exists");
        if (net != db::kInvalidId) {
            values[net] = value;
        }
    }
    return values;
}

sim::Logic4 bit(const bool value) { return value ? sim::Logic4::One : sim::Logic4::Zero; }

std::vector<sim::Fault> faults_on_every_net(const db::FlatNetlist& flat) {
    std::vector<sim::Fault> faults;
    faults.reserve(static_cast<std::size_t>(flat.net_count()) * 2);
    for (db::NetId net = 0; net < flat.net_count(); ++net) {
        faults.push_back({net, sim::StuckValue::Zero});
        faults.push_back({net, sim::StuckValue::One});
    }
    return faults;
}

void expect_detected(const sim::FaultResult& result, const std::uint32_t pattern,
                     const std::string_view what) {
    if (result.state != sim::FaultState::Detected || result.pattern != pattern) {
        std::cerr << "FAIL " << what << ": expected detected at pattern " << pattern
                  << ", got state " << static_cast<int>(result.state) << " at pattern "
                  << result.pattern << std::endl;
        ++failures;
    }
}

void expect_undetected(const sim::FaultResult& result, const std::string_view what) {
    if (result.state != sim::FaultState::Undetected || result.pattern != db::kInvalidId) {
        std::cerr << "FAIL " << what << ": expected undetected, got state "
                  << static_cast<int>(result.state) << " at pattern " << result.pattern
                  << std::endl;
        ++failures;
    }
}

// y = (a & b) | b = b, so the primary input a is unobservable and the AND
// output is only observable when b is 0. The expected detection sets below are
// derived by hand from that identity, not from either simulator.
void test_hand_checked_detection() {
    db::Design design;
    design.top = "top";
    db::Module top;
    top.name = "top";
    top.inputs = {"a", "b"};
    top.outputs = {"y"};
    top.gates = {
        gate(db::GateType::And2, "g1", {"a", "b"}, "n1"),
        gate(db::GateType::Or2, "g2", {"n1", "b"}, "y"),
    };
    design.modules = {top};
    const db::DesignDb database = flatten(std::move(design));
    const db::FlatNetlist& flat = database.flat();

    const db::NetId a = net_named(database, "a");
    const db::NetId b = net_named(database, "b");
    const db::NetId n1 = net_named(database, "n1");
    const db::NetId y = net_named(database, "y");
    expect(a != db::kInvalidId && b != db::kInvalidId && n1 != db::kInvalidId &&
               y != db::kInvalidId,
           "the combinational test has all of its nets");
    if (a == db::kInvalidId || b == db::kInvalidId || n1 == db::kInvalidId || y == db::kInvalidId) {
        return;
    }

    const std::vector<db::NetId> observables = {y};
    const std::vector<sim::Fault> faults = {
        {a, sim::StuckValue::Zero}, {a, sim::StuckValue::One},   {b, sim::StuckValue::Zero},
        {b, sim::StuckValue::One},  {n1, sim::StuckValue::Zero}, {n1, sim::StuckValue::One},
        {y, sim::StuckValue::Zero}, {y, sim::StuckValue::One},
    };

    // Rows are (a, b) in 00, 01, 10, 11 order; columns follow `faults`.
    const std::array<std::array<bool, 8>, 4> expected = {{
        {false, false, false, true, false, true, false, true},
        {false, false, true, false, false, false, true, false},
        {false, false, false, true, false, true, false, true},
        {false, false, true, false, false, false, true, false},
    }};
    const std::array<std::uint32_t, 8> first_detection = {
        db::kInvalidId, db::kInvalidId, 1, 0, db::kInvalidId, 0, 1, 0,
    };

    sim::FaultSimulator simulator(flat, faults, observables);
    std::vector<sim::FaultResult> scalar_results(faults.size());

    for (std::uint32_t pattern_index = 0; pattern_index < expected.size(); ++pattern_index) {
        const bool a_value = (pattern_index & 2) != 0;
        const bool b_value = (pattern_index & 1) != 0;
        std::vector<sim::Logic4> values =
            make_pattern(database, {{"a", bit(a_value)}, {"b", bit(b_value)}});
        std::vector<sim::Logic4> scalar_values = values;

        const db::Status scalar_status =
            sim::simulate_scalar(flat, scalar_values, observables, faults, scalar_results);
        expect(scalar_status.ok(), "scalar fault simulation succeeds");
        if (!scalar_status.ok()) {
            std::cerr << "  " << scalar_status.message() << std::endl;
            return;
        }

        const db::Status word_status = simulator.simulate(values);
        expect(word_status.ok(), "word fault simulation succeeds");
        if (!word_status.ok()) {
            std::cerr << "  " << word_status.message() << std::endl;
            return;
        }

        std::size_t cumulative = 0;
        for (std::size_t index = 0; index < faults.size(); ++index) {
            const bool scalar_hit = scalar_results[index].state == sim::FaultState::Detected;
            if (scalar_hit != expected[pattern_index][index]) {
                std::cerr << "FAIL scalar detection at pattern " << pattern_index << " fault "
                          << index << std::endl;
                ++failures;
            }
            if (scalar_results[index].pattern != db::kInvalidId) {
                std::cerr << "FAIL scalar results carry no pattern index (fault " << index << ")"
                          << std::endl;
                ++failures;
            }

            const std::uint32_t first = first_detection[index];
            if (first == db::kInvalidId || pattern_index < first) {
                expect_undetected(simulator.result(index), "fault is not detected yet");
            } else {
                expect_detected(simulator.result(index), first, "fault keeps its first pattern");
                ++cumulative;
            }
        }
        expect(simulator.detected_count() == cumulative,
               "the word engine's detected count matches the hand table");
        expect(simulator.active_count() == faults.size() - cumulative,
               "the word engine's active count drops detected faults");
    }

    expect(simulator.pattern_count() == static_cast<std::uint32_t>(expected.size()),
           "the word engine counts its patterns");
}

// A patterned netlist with a flop and a black box: q is a state boundary the
// caller seeds, y stays X because the black-box output is X, and n4 gives the
// faults a second observation point. The scalar reference runs every fault for
// every pattern; the word engine drops faults, so agreement is checked on the
// first detecting pattern.
void test_scalar_and_word_agree_with_x_and_state() {
    db::Design design;
    design.top = "top";
    db::Module top;
    top.name = "top";
    top.inputs = {"a", "b", "s"};
    top.outputs = {"y", "n4"};
    top.gates = {
        gate(db::GateType::And2, "g1", {"a", "b"}, "n1"),
        gate(db::GateType::Xor2, "g2", {"n1", "s"}, "n2"),
        gate(db::GateType::Mux2, "g3", {"n2", "a", "s"}, "n3"),
        gate(db::GateType::Not, "g4", {"n3"}, "n4"),
        gate(db::GateType::Dff, "g5", {"n2"}, "q"),
        gate(db::GateType::Or2, "g6", {"n4", "q"}, "n5"),
        gate(db::GateType::Blackbox, "g7", {}, "n6"),
        gate(db::GateType::And2, "g8", {"n6", "a"}, "y"),
    };
    design.modules = {top};
    const db::DesignDb database = flatten(std::move(design));
    const db::FlatNetlist& flat = database.flat();

    const db::NetId y = net_named(database, "y");
    const db::NetId n4 = net_named(database, "n4");
    expect(y != db::kInvalidId && n4 != db::kInvalidId, "the mixed test has its observable nets");
    if (y == db::kInvalidId || n4 == db::kInvalidId) {
        return;
    }

    const std::vector<db::NetId> observables = {y, n4};
    const std::vector<sim::Fault> faults = faults_on_every_net(flat);
    sim::FaultSimulator simulator(flat, faults, observables);
    std::vector<sim::FaultResult> scalar_results(faults.size());
    std::vector<bool> scalar_seen(faults.size(), false);

    const std::array<sim::Logic4, 3> state_seeds = {sim::Logic4::X, sim::Logic4::Zero,
                                                    sim::Logic4::One};
    std::uint32_t pattern_index = 0;
    for (unsigned inputs = 0; inputs < 8; ++inputs) {
        for (const sim::Logic4 state : state_seeds) {
            std::vector<sim::Logic4> word_values =
                make_pattern(database, {{"a", bit((inputs & 1) != 0)},
                                        {"b", bit((inputs & 2) != 0)},
                                        {"s", bit((inputs & 4) != 0)},
                                        {"q", state}});
            std::vector<sim::Logic4> scalar_values = word_values;

            const db::Status scalar_status =
                sim::simulate_scalar(flat, scalar_values, observables, faults, scalar_results);
            expect(scalar_status.ok(), "scalar simulation of the mixed netlist succeeds");
            if (!scalar_status.ok()) {
                std::cerr << "  " << scalar_status.message() << std::endl;
                return;
            }
            const db::Status word_status = simulator.simulate(word_values);
            expect(word_status.ok(), "word simulation of the mixed netlist succeeds");
            if (!word_status.ok()) {
                std::cerr << "  " << word_status.message() << std::endl;
                return;
            }

            std::size_t cumulative = 0;
            for (std::size_t index = 0; index < faults.size(); ++index) {
                const bool scalar_hit = scalar_results[index].state == sim::FaultState::Detected;
                if (scalar_hit) {
                    if (!scalar_seen[index]) {
                        scalar_seen[index] = true;
                        expect_detected(simulator.result(index), pattern_index,
                                        "the word engine detects a newly detected fault in the "
                                        "same pattern as the scalar reference");
                    } else {
                        expect(simulator.result(index).pattern < pattern_index,
                               "an already dropped fault keeps its earlier pattern");
                    }
                }
                if (scalar_seen[index]) {
                    ++cumulative;
                }
            }
            expect(simulator.detected_count() == cumulative,
                   "the word engine's detected count agrees with the scalar reference");
            ++pattern_index;
        }
    }

    expect(pattern_index == simulator.pattern_count(), "every mixed pattern was graded");
    expect(simulator.detected_count() > 0, "the mixed netlist detects some faults");
    expect(simulator.active_count() > 0, "the mixed netlist leaves faults undetected");
}

// 41 nets with both stuck values is 82 faults: two words, the second one short.
// Every sa1 fault is detected by a = 0 and every sa0 fault by a = 1, so the
// expected drop is exact and the group boundary is exercised.
void test_fault_dropping_across_word_groups() {
    constexpr int kBuffers = 40;

    db::Design design;
    design.top = "top";
    db::Module top;
    top.name = "top";
    top.inputs = {"a"};
    top.outputs = {"y"};
    for (int index = 0; index < kBuffers; ++index) {
        const std::string fanin = index == 0 ? "a" : "n" + std::to_string(index - 1);
        const std::string out = index == kBuffers - 1 ? "y" : "n" + std::to_string(index);
        top.gates.push_back(gate(db::GateType::Buf, "g" + std::to_string(index), {fanin}, out));
    }
    design.modules = {top};
    const db::DesignDb database = flatten(std::move(design));
    const db::FlatNetlist& flat = database.flat();

    const db::NetId a = net_named(database, "a");
    const db::NetId y = net_named(database, "y");
    expect(a != db::kInvalidId && y != db::kInvalidId, "the buffer chain has its pins");
    if (a == db::kInvalidId || y == db::kInvalidId) {
        return;
    }

    const std::vector<db::NetId> observables = {y};
    const std::vector<sim::Fault> faults = faults_on_every_net(flat);
    expect(flat.net_count() == static_cast<std::uint32_t>(kBuffers + 1),
           "the buffer chain has the expected net count");
    expect(faults.size() == 82, "the buffer chain has two words of faults");
    expect(faults.size() > sim::FaultSimulator::kFaultsPerWord,
           "the buffer chain crosses one 64-fault word");

    sim::FaultSimulator simulator(flat, faults, observables);

    std::vector<sim::Logic4> low = make_pattern(database, {{"a", sim::Logic4::Zero}});
    expect(simulator.simulate(low).ok(), "the low pattern is graded");
    expect(simulator.detected_count() == 41, "only the sa1 faults drop on a = 0");
    expect(simulator.active_count() == 41, "the sa0 faults stay active after a = 0");
    for (std::size_t index = 0; index < faults.size(); ++index) {
        if (faults[index].stuck == sim::StuckValue::One) {
            expect_detected(simulator.result(index), 0, "a buffer sa1 drops on a = 0");
        } else {
            expect_undetected(simulator.result(index), "a buffer sa0 survives a = 0");
        }
    }

    std::vector<sim::Logic4> high = make_pattern(database, {{"a", sim::Logic4::One}});
    expect(simulator.simulate(high).ok(), "the high pattern is graded");
    expect(simulator.detected_count() == 82, "all 82 faults are detected after a = 1");
    expect(simulator.active_count() == 0, "the fault list is fully dropped");
    for (std::size_t index = 0; index < faults.size(); ++index) {
        const std::uint32_t expected_pattern = faults[index].stuck == sim::StuckValue::One ? 0 : 1;
        expect_detected(simulator.result(index), expected_pattern,
                        "a buffer fault keeps its first detecting pattern");
    }

    std::vector<sim::Logic4> again = make_pattern(database, {{"a", sim::Logic4::Zero}});
    expect(simulator.simulate(again).ok(), "a pattern after full dropping is still graded");
    expect(simulator.detected_count() == 82, "dropping is permanent and does not double count");
    expect(simulator.pattern_count() == 3, "the simulator counts every graded pattern");
}

// An X on the good side is not a detection, even when the faulty lane is known.
void test_x_observable_is_not_a_detection() {
    db::Design design;
    design.top = "top";
    db::Module top;
    top.name = "top";
    top.inputs = {"x"};
    top.outputs = {"y"};
    top.gates = {
        gate(db::GateType::Buf, "g1", {"x"}, "y"),
    };
    design.modules = {top};
    const db::DesignDb database = flatten(std::move(design));
    const db::FlatNetlist& flat = database.flat();
    const db::NetId x = net_named(database, "x");
    const db::NetId y = net_named(database, "y");
    expect(x != db::kInvalidId && y != db::kInvalidId, "the X test has its nets");
    if (x == db::kInvalidId || y == db::kInvalidId) {
        return;
    }

    const std::vector<db::NetId> observables = {y};
    const std::vector<sim::Fault> faults = {
        {x, sim::StuckValue::Zero},
        {x, sim::StuckValue::One},
        {y, sim::StuckValue::Zero},
        {y, sim::StuckValue::One},
    };
    sim::FaultSimulator simulator(flat, faults, observables);
    std::vector<sim::FaultResult> scalar_results(faults.size());

    std::vector<sim::Logic4> unknown = make_pattern(database, {{"x", sim::Logic4::X}});
    std::vector<sim::Logic4> scalar_unknown = unknown;
    expect(sim::simulate_scalar(flat, scalar_unknown, observables, faults, scalar_results).ok(),
           "the scalar reference grades an X pattern");
    for (std::size_t index = 0; index < faults.size(); ++index) {
        expect_undetected(scalar_results[index], "an X good value is not a scalar detection");
    }
    expect(simulator.simulate(unknown).ok(), "the word engine grades an X pattern");
    expect(simulator.detected_count() == 0, "an X good value is not a word detection");

    std::vector<sim::Logic4> one = make_pattern(database, {{"x", sim::Logic4::One}});
    expect(simulator.simulate(one).ok(), "the word engine grades a known good value");
    expect(simulator.detected_count() == 2,
           "the known-good pattern detects the two 0-driving faults");
    expect_detected(simulator.result(0), 1, "x sa0 is detected against a known one");
    expect_undetected(simulator.result(1), "x sa1 matches the good value");
    expect_detected(simulator.result(2), 1, "y sa0 is detected against a known one");
    expect_undetected(simulator.result(3), "y sa1 matches the good value");
}

void test_bad_payload() {
    db::Design design;
    design.top = "top";
    db::Module top;
    top.name = "top";
    top.inputs = {"a"};
    top.outputs = {"y"};
    top.gates = {
        gate(db::GateType::Buf, "g1", {"a"}, "y"),
    };
    design.modules = {top};
    const db::DesignDb database = flatten(std::move(design));
    const db::FlatNetlist& flat = database.flat();
    const db::NetId a = net_named(database, "a");
    expect(a != db::kInvalidId, "the payload test has its input");
    if (a == db::kInvalidId) {
        return;
    }

    const std::vector<db::NetId> observables = {net_named(database, "y")};
    std::vector<sim::Logic4> values = make_pattern(database, {{"a", sim::Logic4::Zero}});

    const std::vector<sim::Fault> bad_faults = {
        {flat.net_count(), sim::StuckValue::Zero},
    };
    sim::FaultSimulator bad_fault_simulator(flat, bad_faults, observables);
    expect(bad_fault_simulator.simulate(values).error() == db::Error::BadPayload,
           "a fault net outside the netlist is rejected");

    const std::vector<sim::Fault> no_faults;
    const std::vector<db::NetId> bad_observables = {flat.net_count()};
    sim::FaultSimulator bad_observable_simulator(flat, no_faults, bad_observables);
    expect(bad_observable_simulator.simulate(values).error() == db::Error::BadPayload,
           "an observable net outside the netlist is rejected");

    sim::FaultSimulator short_simulator(flat, no_faults, observables);
    std::vector<sim::Logic4> short_values(flat.net_count() - 1, sim::Logic4::X);
    expect(short_simulator.simulate(short_values).error() == db::Error::BadPayload,
           "a short value array is rejected");

    sim::FaultSimulator empty_simulator(flat, no_faults, observables);
    expect(empty_simulator.simulate(values).ok(), "an empty fault list still grades the pattern");
    expect(empty_simulator.detected_count() == 0, "an empty fault list detects nothing");
    expect(empty_simulator.pattern_count() == 1, "an empty fault list still counts the pattern");

    const std::vector<sim::Fault> one_fault = {{a, sim::StuckValue::One}};
    std::vector<sim::FaultResult> short_results;
    expect(sim::simulate_scalar(flat, values, observables, one_fault, short_results).error() ==
               db::Error::BadPayload,
           "the scalar reference needs one result per fault");

    std::vector<sim::FaultResult> results(one_fault.size());
    const std::vector<sim::Fault> bad_scalar_fault = {
        {flat.net_count(), sim::StuckValue::Zero},
    };
    expect(sim::simulate_scalar(flat, values, observables, bad_scalar_fault, results).error() ==
               db::Error::BadPayload,
           "the scalar reference rejects a fault net outside the netlist");

    const std::vector<db::NetId> bad_scalar_observables = {flat.net_count() + 1};
    expect(sim::simulate_scalar(flat, values, bad_scalar_observables, one_fault, results).error() ==
               db::Error::BadPayload,
           "the scalar reference rejects an observable net outside the netlist");

    std::vector<sim::Logic4> short_scalar_values(flat.net_count() - 1, sim::Logic4::X);
    expect(
        sim::simulate_scalar(flat, short_scalar_values, observables, one_fault, results).error() ==
            db::Error::BadPayload,
        "the scalar reference rejects a short value array");
}

}  // namespace

int main() {
    test_hand_checked_detection();
    test_scalar_and_word_agree_with_x_and_state();
    test_fault_dropping_across_word_groups();
    test_x_observable_is_not_a_detection();
    test_bad_payload();

    if (failures != 0) {
        std::cerr << failures << " failure(s)" << std::endl;
        return 1;
    }
    std::cout << "fault: all tests passed" << std::endl;
    return 0;
}
