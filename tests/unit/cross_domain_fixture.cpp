
// Process-level design fixture for ADR-013 section 5: two declared clocks, one flop
// each, and the first domain feeding the second across combinational logic.
//
//   crossing  clk_a's flop drives a buffer into clk_b's flop's data input, so a
//             sequential procedure that captures clk_a before clk_b crosses a clock
//             boundary with nothing holding the value;
//   lockup    a DLAT sits on that path, which is the lockup latch section 5 accepts.
//   independent
//             three clocks with one flop each and no combinational path between
//             them, so a procedure can group or repeat clocks without creating a
//             crossing, which leaves section 4's conditions 1, 3 and 4 as the only
//             findings a run over this design can reach.
//
// The fixture writes no constraints: the process test attaches the clocks and the
// false paths through read_sdc, so the attached database carries the false-path clock
// provenance the crossing evaluation reads.
//
// usage: cross_domain_fixture <crossing|lockup> <output.dfdb> <output.faults>

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
    if (argc != 4) {
        std::cerr << "usage: cross_domain_fixture <crossing|lockup|independent> <output.dfdb> "
                     "<output.faults>"
                  << std::endl;
        return 1;
    }
    const std::string mode = argv[1];
    if (mode != "crossing" && mode != "lockup" && mode != "independent") {
        std::cerr << "unknown mode '" << mode << "'" << std::endl;
        return 1;
    }

    db::Design design;
    design.top = "top";

    db::Module top;
    top.name = "top";
    top.inputs = {"clk_a", "clk_b", "d_a", "d_b"};
    top.outputs = {"q_a", "q_b"};
    top.gates = {
        gate(db::GateType::Dff, "ff_a", {"d_a", "clk_a"}, "q_a"),
        gate(db::GateType::Dff, "ff_b", {"n_cross", "clk_b"}, "q_b"),
    };
    if (mode == "independent") {
        // Three domains with nothing between them, so the group, the timing and the
        // repetition findings are section 4's alone.
        top.inputs = {"clk_a", "clk_b", "clk_c", "d_a", "d_b", "d_c"};
        top.outputs = {"q_a", "q_b", "q_c"};
        top.gates = {
            gate(db::GateType::Dff, "ff_a", {"d_a", "clk_a"}, "q_a"),
            gate(db::GateType::Dff, "ff_b", {"d_b", "clk_b"}, "q_b"),
            gate(db::GateType::Dff, "ff_c", {"d_c", "clk_c"}, "q_c"),
        };
    } else if (mode == "lockup") {
        // clk_a's flop holds in a latch before clk_b samples it: the lockup latch.
        top.gates.push_back(gate(db::GateType::Dlat, "dl_lockup", {"q_a", "clk_b"}, "n_hold"));
        top.gates.push_back(gate(db::GateType::Buf, "g_cross", {"n_hold"}, "n_cross"));
    } else {
        top.gates.push_back(gate(db::GateType::Buf, "g_cross", {"q_a"}, "n_cross"));
    }
    design.modules = {top};

    db::DesignDb database;
    database.design() = std::move(design);
    const db::Status flattened = database.flatten();
    if (!flattened.ok()) {
        std::cerr << "fixture flatten failed: " << flattened.message() << std::endl;
        return 1;
    }

    const db::Status saved = database.save(argv[2]);
    if (!saved.ok()) {
        std::cerr << "fixture save failed: " << saved.message() << std::endl;
        return 1;
    }

    const db::NetId q_a = net_named(database, "q_a");
    const db::NetId q_b = net_named(database, "q_b");
    const db::NetId q_c = net_named(database, "q_c");
    const db::NetId n_cross = net_named(database, "n_cross");
    const db::NetId third = mode == "independent" ? q_c : n_cross;
    if (q_a == db::kInvalidId || q_b == db::kInvalidId || third == db::kInvalidId) {
        std::cerr << "fixture is missing an expected net" << std::endl;
        return 1;
    }

    const std::vector<sim::Fault> raw = {
        {q_a, sim::StuckValue::Zero},
        {q_b, sim::StuckValue::Zero},
        {third, sim::StuckValue::Zero},
    };
    atpg::CollapsedFaultList collapsed;
    collapsed.faults = raw;
    collapsed.raw_to_collapsed = {0, 1, 2};
    const db::Status written = atpg::write_faults(argv[3], database, raw, collapsed);
    if (!written.ok()) {
        std::cerr << "fixture write_faults failed: " << written.message() << std::endl;
        return 1;
    }
    return 0;
}
