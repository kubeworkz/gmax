// Process-level CLI fixture for run_atpg. The design has one direct-detection
// fault, one fault that traverses a hiding branch before it can be observed,
// and one fault on a tied net that cannot be activated. With a zero-backtrack
// budget the middle fault aborts, giving the process test all three outcomes.

#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/atpg/fault_file.h"
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

}  // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "usage: atpg_cli_fixture <output.dfdb> <output.faults>" << std::endl;
        return 1;
    }

    db::Design design;
    design.top = "top";

    db::Module top;
    top.name = "top";
    top.inputs = {"a", "b", "c"};
    top.outputs = {"y", "z"};
    top.gates = {
        gate(db::GateType::Xor2, "g_xor", {"a", "b"}, "n1"),
        gate(db::GateType::And2, "g_and", {"n1", "b"}, "y"),
        gate(db::GateType::Buf, "g_z", {"c"}, "z"),
        gate(db::GateType::Tie0, "g_tie", {}, "t0"),
        gate(db::GateType::Buf, "g_dead", {"t0"}, "dead"),
    };
    design.modules = {top};

    db::DesignDb database;
    database.design() = std::move(design);
    const db::Status flattened = database.flatten();
    if (!flattened.ok()) {
        std::cerr << "fixture flatten failed: " << flattened.message() << std::endl;
        return 1;
    }
    const db::Status saved = database.save(argv[1]);
    if (!saved.ok()) {
        std::cerr << "fixture save failed: " << saved.message() << std::endl;
        return 1;
    }

    const db::NetId a = net_named(database, "a");
    const db::NetId c = net_named(database, "c");
    const db::NetId dead = net_named(database, "dead");
    if (a == db::kInvalidId || c == db::kInvalidId || dead == db::kInvalidId) {
        std::cerr << "fixture is missing an expected net" << std::endl;
        return 1;
    }

    const std::vector<sim::Fault> raw = {
        {c, sim::StuckValue::Zero},
        {a, sim::StuckValue::Zero},
        {dead, sim::StuckValue::Zero},
    };
    atpg::CollapsedFaultList collapsed;
    collapsed.faults = raw;
    collapsed.raw_to_collapsed = {0, 1, 2};
    const db::Status written = atpg::write_faults(argv[2], database, raw, collapsed);
    if (!written.ok()) {
        std::cerr << "fixture write_faults failed: " << written.message() << std::endl;
        return 1;
    }
    return 0;
}
