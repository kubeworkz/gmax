// Process-level design fixture for ADR-013 sections 2 and 4:
//
//   no-clock      both declared clocks clock no state-holding gate, so the
//                 clock-level exclusion would leave nothing to pulse and condition
//                 5 refuses the run;
//   boundary      clk_a clocks a flop and clk_b clocks nothing, so the list is not
//                 emptied and the run must grade;
//   partial-scan  eight clk_a elements, three of them scan cells, so condition 2
//                 refuses a sequential procedure that captures clk_a first;
//   full-scan     the same design with all eight scanned, so that procedure grades;
//   masking       the full-scan design plus one clk_b element nothing reads, so a
//                 run can mask clk_a and have a genuinely untestable fault beside
//                 the masked ones.
//
// usage: capture_refusal_fixture
//            <no-clock|boundary|partial-scan|full-scan|masking> <output.dfdb> <output.faults>

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
        std::cerr << "usage: capture_refusal_fixture "
                     "<no-clock|boundary|partial-scan|full-scan|masking> "
                     "<output.dfdb> <output.faults>"
                  << std::endl;
        return 1;
    }
    const std::string mode = argv[1];
    if (mode != "no-clock" && mode != "boundary" && mode != "partial-scan" && mode != "full-scan" &&
        mode != "masking") {
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
    const bool scan_design = mode == "partial-scan" || mode == "full-scan" || mode == "masking";
    if (mode == "boundary") {
        // clk_a clocks the only state-holding gate; clk_b clocks nothing.
        top.gates.push_back(gate(db::GateType::Dff, "ff", {"n1", "clk_a"}, "q"));
        top.gates.push_back(gate(db::GateType::Buf, "g_y", {"q"}, "y"));
    } else if (scan_design) {
        // Eight edge elements on clk_a and one on clk_b, with a lockup latch on the
        // path from a clk_a element: a sequential capture is DFT003-clean, so the
        // only question left is section 2's intermediate state. `full-scan` marks all
        // eight elements as scan cells; `partial-scan` marks three.
        const db::GateType repeated =
            mode == "partial-scan" ? db::GateType::Dff : db::GateType::ScanCell;
        top.inputs = {"clk_a", "clk_b", "d"};
        top.outputs.clear();
        top.gates.clear();
        for (std::size_t i = 0; i < 8; ++i) {
            const std::string name = "a" + std::to_string(i);
            top.gates.push_back(gate(i < 3 ? db::GateType::ScanCell : repeated, "ff_" + name,
                                     {"d", "clk_a"}, "n_" + name));
            top.gates.push_back(gate(db::GateType::Buf, "g_" + name, {"n_" + name}, "q_" + name));
            top.outputs.push_back("q_" + name);
        }
        top.gates.push_back(gate(db::GateType::Dlat, "dl_cross", {"n_a0", "clk_b"}, "n_hold"));
        top.gates.push_back(gate(db::GateType::Dff, "ff_b", {"n_hold", "clk_b"}, "n_b"));
        top.gates.push_back(gate(db::GateType::Buf, "g_b", {"n_b"}, "q_b"));
        top.outputs.push_back("q_b");
        if (mode == "masking") {
            // Nothing reads this element, so its fault is untestable in every run:
            // the masked run's per-fault list carries it beside the masked faults.
            top.gates.push_back(gate(db::GateType::ScanCell, "ff_dead", {"d", "clk_b"}, "n_dead"));
        }
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

    std::vector<db::NetId> nets;
    if (scan_design) {
        nets = {net_named(database, "q_a0"), net_named(database, "n_a0"),
                net_named(database, "q_b")};
        if (mode == "masking") {
            // `n_a0` is the observation the mask removes from use, `n_b` another
            // domain's element that stays observable, `n_dead` an element nothing
            // reads, and `q_a0` a primary output whose own fault no run can detect,
            // so the masked list is distinguishable from every other outcome.
            nets = {net_named(database, "n_a0"), net_named(database, "n_b"),
                    net_named(database, "n_dead"), net_named(database, "q_a0")};
        }
    } else {
        nets = {net_named(database, "y"), net_named(database, "n1")};
    }
    for (const db::NetId net : nets) {
        if (net == db::kInvalidId) {
            std::cerr << "fixture is missing an expected net" << std::endl;
            return 1;
        }
    }

    std::vector<sim::Fault> raw;
    std::vector<std::uint32_t> mapping;
    for (std::size_t i = 0; i < nets.size(); ++i) {
        raw.push_back({nets[i], sim::StuckValue::Zero});
        mapping.push_back(static_cast<std::uint32_t>(i));
    }
    atpg::CollapsedFaultList collapsed;
    collapsed.faults = raw;
    collapsed.raw_to_collapsed = mapping;
    const db::Status written = atpg::write_faults(argv[3], database, raw, collapsed);
    if (!written.ok()) {
        std::cerr << "fixture write_faults failed: " << written.message() << std::endl;
        return 1;
    }
    return 0;
}
