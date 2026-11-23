// WP-006: checkpoint fault-site generation, deterministic ordering and
// structural equivalence collapsing over the flat DB. The .faults writer
// follows. Framework-free for the same reason as the other tests: third_party
// stays empty until TBD-11 closes.

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/atpg/fault_list.h"
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

db::Gate gate(db::GateType type, std::string name, std::vector<std::string> fanins,
              std::string out) {
    db::Gate result;
    result.type = type;
    result.name = std::move(name);
    result.fanins = std::move(fanins);
    result.out = std::move(out);
    return result;
}

// A small design with the site categories that matter here: primary inputs, a
// combinational output, a flop output, a scan-cell output, two tie outputs and
// an undriven top-level output. `reverse` builds the same gates in the opposite
// order so flattening's canonical order is what makes the result stable.
db::Design make_design(const bool reverse) {
    db::Design design;
    design.top = "top";

    db::Module top;
    top.name = "top";
    top.inputs = {"a", "b", "clock"};
    top.outputs = {"y", "dangling"};
    std::vector<db::Gate> gates = {
        gate(db::GateType::And2, "g1", {"a", "b"}, "n1"),
        gate(db::GateType::Buf, "g2", {"n1"}, "y"),
        gate(db::GateType::Tie0, "g3", {}, "tie_low"),
        gate(db::GateType::Tie1, "g4", {}, "tie_high"),
        gate(db::GateType::Dff, "g5", {"a", "clock"}, "q"),
        gate(db::GateType::ScanCell, "g6", {"b", "clock"}, "scan_q"),
    };
    if (reverse) {
        std::reverse(gates.begin(), gates.end());
    }
    top.gates = std::move(gates);
    design.modules = {top};
    return design;
}

db::DesignDb flatten(db::Design design) {
    db::DesignDb database;
    database.design() = std::move(design);
    const db::Status status = database.flatten();
    expect(status.ok(), "the fault-list test design flattens");
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

std::vector<db::NetId> expected_sites(const db::DesignDb& database) {
    return {
        net_named(database, "a"),      net_named(database, "b"),  net_named(database, "clock"),
        net_named(database, "y"),      net_named(database, "n1"), net_named(database, "q"),
        net_named(database, "scan_q"),
    };
}

void test_sites_and_order() {
    const db::DesignDb database = flatten(make_design(false));
    const std::vector<db::NetId> expected = expected_sites(database);
    expect(std::find(expected.begin(), expected.end(), db::kInvalidId) == expected.end(),
           "every expected fault site has a net");

    const std::vector<db::NetId> sites = atpg::stuck_at_fault_sites(database.flat());
    expect(sites == expected, "checkpoint sites follow ascending net id");

    const db::NetId dangling = net_named(database, "dangling");
    const db::NetId tie_low = net_named(database, "tie_low");
    const db::NetId tie_high = net_named(database, "tie_high");
    expect(std::find(sites.begin(), sites.end(), dangling) == sites.end(),
           "an undriven output is not a fault site");
    expect(std::find(sites.begin(), sites.end(), tie_low) == sites.end(),
           "a tie-low output is not a fault site");
    expect(std::find(sites.begin(), sites.end(), tie_high) == sites.end(),
           "a tie-high output is not a fault site");

    const std::vector<sim::Fault> faults = atpg::stuck_at_faults(database.flat());
    expect(faults.size() == expected.size() * 2, "each checkpoint site gets SA0 and SA1");
    for (std::size_t index = 0; index < faults.size(); ++index) {
        const sim::StuckValue want = index % 2 == 0 ? sim::StuckValue::Zero : sim::StuckValue::One;
        expect(faults[index].net == expected[index / 2] && faults[index].stuck == want,
               "faults are ordered by net id, SA0 before SA1");
    }
}

void test_flatten_order_is_deterministic() {
    const db::DesignDb forward = flatten(make_design(false));
    const db::DesignDb reverse = flatten(make_design(true));
    expect(atpg::stuck_at_fault_sites(forward.flat()) == atpg::stuck_at_fault_sites(reverse.flat()),
           "gate insertion order does not change the site list");
    expect(atpg::stuck_at_faults(forward.flat()) == atpg::stuck_at_faults(reverse.flat()),
           "gate insertion order does not change the fault list");
}

db::DesignDb buffer_chain() {
    db::Design design;
    design.top = "top";

    db::Module top;
    top.name = "top";
    top.inputs = {"a"};
    top.outputs = {"y"};
    top.gates = {
        gate(db::GateType::Buf, "g1", {"a"}, "n1"),
        gate(db::GateType::Buf, "g2", {"n1"}, "y"),
    };
    design.modules = {top};
    return flatten(std::move(design));
}

void expect_mapping(const atpg::CollapsedFaultList& collapsed, const std::vector<sim::Fault>& raw) {
    expect(collapsed.raw_count() == raw.size(), "the raw count matches the ordered list");
    std::vector<std::uint32_t> members(collapsed.collapsed_count(), 0);
    for (std::size_t index = 0; index < raw.size(); ++index) {
        const std::uint32_t id = collapsed.raw_to_collapsed[index];
        expect(id < collapsed.collapsed_count(), "every raw fault maps to a collapsed class");
        if (id >= collapsed.collapsed_count()) {
            continue;
        }
        ++members[id];
        bool first = true;
        for (std::size_t earlier = 0; earlier < index; ++earlier) {
            if (collapsed.raw_to_collapsed[earlier] == id) {
                first = false;
                break;
            }
        }
        if (first) {
            expect(collapsed.faults[id] == raw[index],
                   "a collapsed class keeps its first raw fault as representative");
        }
    }
    for (const std::uint32_t count : members) {
        expect(count != 0, "every collapsed class has at least one raw fault");
    }
}

void test_buffer_chain_collapses() {
    const db::DesignDb database = buffer_chain();
    const db::NetId a = net_named(database, "a");
    const db::NetId y = net_named(database, "y");
    const std::vector<sim::Fault> raw = atpg::stuck_at_faults(database.flat());
    const std::vector<db::NetId> observables = {y};
    const atpg::CollapsedFaultList collapsed =
        atpg::collapse_stuck_at_faults(database.flat(), observables);

    expect(raw.size() == 6, "the buffer chain has three sites and six faults");
    expect(collapsed.raw_count() == 6 && collapsed.collapsed_count() == 2,
           "a two-buffer chain collapses to one class per stuck value");
    expect(collapsed.faults ==
               std::vector<sim::Fault>{{a, sim::StuckValue::Zero}, {a, sim::StuckValue::One}},
           "the first buffer input represents both classes");
    expect(collapsed.raw_to_collapsed == std::vector<std::uint32_t>{0, 1, 0, 1, 0, 1},
           "both buffer edges map to the same two classes");
    expect_mapping(collapsed, raw);
}

void test_inverter_chain_swaps_polarity() {
    db::Design design;
    design.top = "top";

    db::Module top;
    top.name = "top";
    top.inputs = {"b"};
    top.outputs = {"y"};
    top.gates = {
        gate(db::GateType::Not, "g1", {"b"}, "n1"),
        gate(db::GateType::Buf, "g2", {"n1"}, "y"),
    };
    design.modules = {top};
    const db::DesignDb database = flatten(std::move(design));

    const db::NetId b = net_named(database, "b");
    const db::NetId y = net_named(database, "y");
    const std::vector<sim::Fault> raw = atpg::stuck_at_faults(database.flat());
    const std::vector<db::NetId> observables = {y};
    const atpg::CollapsedFaultList collapsed =
        atpg::collapse_stuck_at_faults(database.flat(), observables);

    expect(raw.size() == 6, "the inverter chain has three sites and six faults");
    expect(collapsed.collapsed_count() == 2,
           "an inverter followed by a buffer still collapses to two classes");
    expect(collapsed.faults ==
               std::vector<sim::Fault>{{b, sim::StuckValue::Zero}, {b, sim::StuckValue::One}},
           "the inverter chain keeps the input as its representative");
    expect(collapsed.raw_to_collapsed == std::vector<std::uint32_t>{0, 1, 1, 0, 1, 0},
           "an inverter edge swaps SA0 and SA1 while the class ids stay ordered");
    expect_mapping(collapsed, raw);
}

void test_fanout_blocks_collapse() {
    db::Design design;
    design.top = "top";

    db::Module top;
    top.name = "top";
    top.inputs = {"c", "x"};
    top.outputs = {"y1", "y2"};
    top.gates = {
        gate(db::GateType::Buf, "g1", {"c"}, "y1"),
        gate(db::GateType::And2, "g2", {"c", "x"}, "y2"),
    };
    design.modules = {top};
    const db::DesignDb database = flatten(std::move(design));

    const std::vector<sim::Fault> raw = atpg::stuck_at_faults(database.flat());
    const std::vector<db::NetId> observables = {net_named(database, "y1"),
                                                net_named(database, "y2")};
    const atpg::CollapsedFaultList collapsed =
        atpg::collapse_stuck_at_faults(database.flat(), observables);

    expect(raw.size() == 8, "the fanout design has four sites and eight faults");
    expect(collapsed.collapsed_count() == 8,
           "a buffered net with another load is not equivalent to its buffer output");
    for (std::size_t index = 0; index < raw.size(); ++index) {
        expect(collapsed.raw_to_collapsed[index] == index,
               "the fanout design collapses to the identity mapping");
    }
    expect(collapsed.faults == raw, "the fanout design keeps every raw fault");
    expect_mapping(collapsed, raw);
}

void test_observable_input_blocks_only_its_edge() {
    const db::DesignDb database = buffer_chain();
    const db::NetId a = net_named(database, "a");
    const db::NetId y = net_named(database, "y");
    const db::NetId n1 = net_named(database, "n1");
    const std::vector<sim::Fault> raw = atpg::stuck_at_faults(database.flat());

    const std::vector<db::NetId> visible_input = {a};
    const atpg::CollapsedFaultList partial =
        atpg::collapse_stuck_at_faults(database.flat(), visible_input);
    expect(partial.collapsed_count() == 4,
           "an observable buffer input is not collapsed, but its downstream edge still is");
    expect(partial.faults == std::vector<sim::Fault>{{a, sim::StuckValue::Zero},
                                                     {a, sim::StuckValue::One},
                                                     {y, sim::StuckValue::Zero},
                                                     {y, sim::StuckValue::One}},
           "the observable input and the downstream class stay separate");
    expect(partial.raw_to_collapsed == std::vector<std::uint32_t>{0, 1, 2, 3, 2, 3},
           "only the edge that starts at the observable net is blocked");
    expect_mapping(partial, raw);

    const std::vector<db::NetId> visible_chain = {a, n1};
    const atpg::CollapsedFaultList none =
        atpg::collapse_stuck_at_faults(database.flat(), visible_chain);
    expect(none.collapsed_count() == raw.size(),
           "observing both buffer nets leaves every fault separate");
    expect(none.faults == raw, "an all-observable chain keeps the raw list");
    expect_mapping(none, raw);
}

std::vector<sim::Logic4> make_pattern(const db::DesignDb& database, const bool a, const bool b) {
    std::vector<sim::Logic4> values(database.flat().net_count(), sim::Logic4::X);
    const db::NetId a_net = net_named(database, "a");
    const db::NetId b_net = net_named(database, "b");
    expect(a_net != db::kInvalidId && b_net != db::kInvalidId, "the pattern inputs exist");
    if (a_net != db::kInvalidId) {
        values[a_net] = a ? sim::Logic4::One : sim::Logic4::Zero;
    }
    if (b_net != db::kInvalidId) {
        values[b_net] = b ? sim::Logic4::One : sim::Logic4::Zero;
    }
    return values;
}

void test_collapsed_detection_matches_raw() {
    db::Design design;
    design.top = "top";

    db::Module top;
    top.name = "top";
    top.inputs = {"a", "b"};
    top.outputs = {"y", "z"};
    top.gates = {
        gate(db::GateType::Buf, "g1", {"a"}, "n1"),
        gate(db::GateType::Buf, "g2", {"n1"}, "y"),
        gate(db::GateType::Not, "g3", {"b"}, "m1"),
        gate(db::GateType::Buf, "g4", {"m1"}, "z"),
    };
    design.modules = {top};
    const db::DesignDb database = flatten(std::move(design));
    const db::FlatNetlist& flat = database.flat();
    const db::NetId y = net_named(database, "y");
    const db::NetId z = net_named(database, "z");
    const std::vector<db::NetId> observables = {y, z};

    const std::vector<sim::Fault> raw = atpg::stuck_at_faults(flat);
    const atpg::CollapsedFaultList collapsed = atpg::collapse_stuck_at_faults(flat, observables);
    expect(raw.size() == 12, "the differential design has twelve raw faults");
    expect(collapsed.collapsed_count() == 4, "the two buffer chains collapse to two classes each");
    expect(collapsed.raw_to_collapsed ==
               std::vector<std::uint32_t>{0, 1, 2, 3, 0, 1, 3, 2, 0, 1, 3, 2},
           "the differential design has the expected class mapping");

    sim::FaultSimulator raw_simulator(flat, raw, observables);
    sim::FaultSimulator collapsed_simulator(flat, collapsed.faults, observables);
    struct Pattern {
        bool a;
        bool b;
    };
    const Pattern patterns[] = {{false, false}, {false, true}, {true, false}, {true, true}};
    for (const Pattern& pattern : patterns) {
        std::vector<sim::Logic4> raw_values = make_pattern(database, pattern.a, pattern.b);
        std::vector<sim::Logic4> collapsed_values = raw_values;
        expect(raw_simulator.simulate(raw_values).ok(), "the raw faults simulate");
        expect(collapsed_simulator.simulate(collapsed_values).ok(),
               "the collapsed faults simulate");
    }

    // The patterns detect all four classes, so an all-undetected result cannot
    // make the comparison below pass vacuously.
    const std::uint32_t expected_patterns[] = {2, 0, 1, 0};
    for (std::size_t id = 0; id < collapsed.collapsed_count(); ++id) {
        const sim::FaultResult& result = collapsed_simulator.result(id);
        expect(result.state == sim::FaultState::Detected, "every class is detected");
        expect(result.pattern == expected_patterns[id],
               "each class is first detected by the expected pattern");
    }
    for (std::size_t index = 0; index < raw.size(); ++index) {
        const sim::FaultResult& raw_result = raw_simulator.result(index);
        const sim::FaultResult& class_result =
            collapsed_simulator.result(collapsed.raw_to_collapsed[index]);
        expect(raw_result.state == class_result.state,
               "a raw fault and its class agree on detection");
        expect(raw_result.pattern == class_result.pattern,
               "a raw fault and its class are first detected by the same pattern");
    }
}

void test_empty_netlist() {
    const db::FlatNetlist empty;
    expect(atpg::stuck_at_fault_sites(empty).empty(), "an empty netlist has no fault sites");
    expect(atpg::stuck_at_faults(empty).empty(), "an empty netlist has no faults");
    const atpg::CollapsedFaultList collapsed = atpg::collapse_stuck_at_faults(empty, {});
    expect(collapsed.raw_count() == 0 && collapsed.collapsed_count() == 0,
           "an empty netlist has nothing to collapse");
}

}  // namespace

int main() {
    test_sites_and_order();
    test_flatten_order_is_deterministic();
    test_buffer_chain_collapses();
    test_inverter_chain_swaps_polarity();
    test_fanout_blocks_collapse();
    test_observable_input_blocks_only_its_edge();
    test_collapsed_detection_matches_raw();
    test_empty_netlist();

    if (failures != 0) {
        std::cerr << failures << " expectation(s) failed" << std::endl;
        return 1;
    }
    std::cout << "fault list checks passed" << std::endl;
    return 0;
}
