// Process-level design fixture for ADR-013 section 4. Two modes:
//
//   no-clock  both declared clocks clock no state-holding gate, so the
//             clock-level exclusion would leave nothing to pulse and condition 5
//             refuses the run;
//   boundary  clk_a clocks a flop and clk_b clocks nothing, so the list is not
//             emptied and the run must grade.
//
// usage: capture_refusal_fixture <no-clock|boundary> <output.dfdb> <output.faults>

#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/atpg/fault_file.h"
#include "core/db/constraints.h"
#include "core/db/db.h"
#include "core/sim/fault.h"

namespace db = gmax::db;
namespace atpg = gmax::atpg;
namespace sim = gmax::sim;

namespace {

db::Gate gate(db::GateType type, std::string name, std::vector<std::string> fanins,
              std::string out) {
    db::Gate result;
    result.type = type;
    result.name = std::move(name);
    result.fanins = std::move(fanins);
    result.out = std::move(out);
    return result;
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

db::ConstraintClock clock(const std::string& name, const double period) {
    db::ConstraintClock result;
    result.name = name;
    result.period = period;
    result.source = name;
    return result;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 4) {
        std::cerr << "usage: capture_refusal_fixture <no-clock|boundary> <output.dfdb> "
                     "<output.faults>"
                  << std::endl;
        return 1;
    }
    const std::string mode = argv[1];
    if (mode != "no-clock" && mode != "boundary") {
        std::cerr << "unknown mode '" << mode << "'" << std::endl;
        return 1;
    }

    db::Design design;
    design.top = "top";

    db::Module top;
    top.name = "top";
    top.inputs = {"a", "b", "clk_a", "clk_b"};
    top.outputs = {"y"};
    top.gates = {
        gate(db::GateType::And2, "g_and", {"a", "b"}, "n1"),
    };
    if (mode == "boundary") {
        // clk_a clocks the only state-holding gate; clk_b clocks nothing.
        top.gates.push_back(gate(db::GateType::Dff, "ff", {"n1", "clk_a"}, "q"));
        top.gates.push_back(gate(db::GateType::Buf, "g_y", {"q"}, "y"));
    } else {
        top.gates.push_back(gate(db::GateType::Buf, "g_y", {"n1"}, "y"));
    }
    design.modules = {top};

    db::DesignDb database;
    database.design() = std::move(design);
    const db::Status flattened = database.flatten();
    if (!flattened.ok()) {
        std::cerr << "fixture flatten failed: " << flattened.message() << std::endl;
        return 1;
    }

    db::Constraints constraints;
    constraints.present = true;
    constraints.complete = true;
    constraints.clocks = {clock("clk_a", 10.0), clock("clk_b", 10.0)};
    database.constraints() = std::move(constraints);

    const db::Status saved = database.save(argv[2]);
    if (!saved.ok()) {
        std::cerr << "fixture save failed: " << saved.message() << std::endl;
        return 1;
    }

    const db::NetId y = net_named(database, "y");
    const db::NetId n1 = net_named(database, "n1");
    if (y == db::kInvalidId || n1 == db::kInvalidId) {
        std::cerr << "fixture is missing an expected net" << std::endl;
        return 1;
    }

    const std::vector<sim::Fault> raw = {
        {y, sim::StuckValue::Zero},
        {n1, sim::StuckValue::Zero},
    };
    atpg::CollapsedFaultList collapsed;
    collapsed.faults = raw;
    collapsed.raw_to_collapsed = {0, 1};
    const db::Status written = atpg::write_faults(argv[3], database, raw, collapsed);
    if (!written.ok()) {
        std::cerr << "fixture write_faults failed: " << written.message() << std::endl;
        return 1;
    }
    return 0;
}
