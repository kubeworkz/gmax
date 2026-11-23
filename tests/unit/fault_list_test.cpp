// WP-006's first slice: checkpoint fault-site generation and deterministic
// ordering over the flat DB. Structural collapse and the .faults writer follow.
// Framework-free for the same reason as the other tests: third_party stays
// empty until TBD-11 closes.

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/atpg/fault_list.h"
#include "core/db/db.h"

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

void test_empty_netlist() {
    const db::FlatNetlist empty;
    expect(atpg::stuck_at_fault_sites(empty).empty(), "an empty netlist has no fault sites");
    expect(atpg::stuck_at_faults(empty).empty(), "an empty netlist has no faults");
}

}  // namespace

int main() {
    test_sites_and_order();
    test_flatten_order_is_deterministic();
    test_empty_netlist();

    if (failures != 0) {
        std::cerr << failures << " expectation(s) failed" << std::endl;
        return 1;
    }
    std::cout << "fault list checks passed" << std::endl;
    return 0;
}
