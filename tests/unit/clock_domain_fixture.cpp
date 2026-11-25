// Process-level CLI fixture: save a known two-clock sequential design as
// .dfdb. The sdc_clocks test needs real state-holding gates with two declared
// clock ports, and building them here keeps the test independent of Yosys:
// clk_a clocks ff_a, clk_b clocks ff_b, and each flop drives one output.

#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "core/db/db.h"

namespace db = gmax::db;

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

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: clock_domain_fixture <output.dfdb>" << std::endl;
        return 1;
    }

    db::Design design;
    design.top = "top";

    db::Module top;
    top.name = "top";
    top.inputs = {"clk_a", "clk_b", "d_a", "d_b"};
    top.outputs = {"q_a", "q_b"};
    top.gates = {
        gate(db::GateType::Dff, "ff_a", {"d_a", "clk_a"}, "ra"),
        gate(db::GateType::Dff, "ff_b", {"d_b", "clk_b"}, "rb"),
        gate(db::GateType::Buf, "g_a", {"ra"}, "q_a"),
        gate(db::GateType::Buf, "g_b", {"rb"}, "q_b"),
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
    return 0;
}
