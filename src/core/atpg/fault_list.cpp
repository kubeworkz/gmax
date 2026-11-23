#include "core/atpg/fault_list.h"

namespace gmax::atpg {
namespace {

bool is_tie(const db::FlatNetlist& netlist, const std::uint32_t driver) {
    return netlist.type[driver] == db::GateType::Tie0 || netlist.type[driver] == db::GateType::Tie1;
}

}  // namespace

std::vector<db::NetId> stuck_at_fault_sites(const db::FlatNetlist& netlist) {
    std::vector<db::NetId> sites;
    sites.reserve(netlist.net_count());
    for (db::NetId net = 0; net < netlist.net_count(); ++net) {
        const std::uint32_t driver = netlist.driver[net];
        if (driver == db::kInvalidId) {
            continue;
        }
        if (driver != db::kPrimaryInput &&
            (driver >= netlist.gate_count() || is_tie(netlist, driver))) {
            continue;
        }
        sites.push_back(net);
    }
    return sites;
}

std::vector<sim::Fault> stuck_at_faults(const db::FlatNetlist& netlist) {
    const std::vector<db::NetId> sites = stuck_at_fault_sites(netlist);
    std::vector<sim::Fault> faults;
    faults.reserve(sites.size() * 2);
    for (const db::NetId net : sites) {
        faults.push_back(sim::Fault{net, sim::StuckValue::Zero});
        faults.push_back(sim::Fault{net, sim::StuckValue::One});
    }
    return faults;
}

}  // namespace gmax::atpg
