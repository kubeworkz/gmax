#include "core/atpg/fault_list.h"

namespace gmax::atpg {
namespace {

bool is_tie(const db::FlatNetlist& netlist, const std::uint32_t driver) {
    return netlist.type[driver] == db::GateType::Tie0 || netlist.type[driver] == db::GateType::Tie1;
}

class DisjointSet {
  public:
    explicit DisjointSet(const std::size_t count) : parent_(count) {
        for (std::size_t index = 0; index < count; ++index) {
            parent_[index] = static_cast<std::uint32_t>(index);
        }
    }

    std::uint32_t find(std::uint32_t item) {
        std::uint32_t root = item;
        while (parent_[root] != root) {
            root = parent_[root];
        }
        while (parent_[item] != item) {
            const std::uint32_t next = parent_[item];
            parent_[item] = root;
            item = next;
        }
        return root;
    }

    void unite(const std::uint32_t a, const std::uint32_t b) {
        const std::uint32_t root_a = find(a);
        const std::uint32_t root_b = find(b);
        if (root_a != root_b) {
            parent_[root_b] = root_a;
        }
    }

  private:
    std::vector<std::uint32_t> parent_;
};

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

CollapsedFaultList collapse_stuck_at_faults(const db::FlatNetlist& netlist,
                                            const std::span<const db::NetId> observables) {
    const std::vector<sim::Fault> raw = stuck_at_faults(netlist);
    CollapsedFaultList result;
    result.raw_to_collapsed.resize(raw.size());
    if (raw.empty()) {
        return result;
    }

    const std::size_t net_count = netlist.net_count();
    std::vector<std::uint32_t> fault_index(net_count * 2, db::kInvalidId);
    for (std::size_t index = 0; index < raw.size(); ++index) {
        if (raw[index].net < net_count) {
            const std::size_t slot = static_cast<std::size_t>(raw[index].net) * 2 +
                                     (raw[index].stuck == sim::StuckValue::One ? 1 : 0);
            fault_index[slot] = static_cast<std::uint32_t>(index);
        }
    }

    std::vector<std::uint32_t> loads(net_count, 0);
    for (db::GateId gate = 0; gate < netlist.gate_count(); ++gate) {
        for (std::uint32_t i = netlist.fanin_begin[gate]; i < netlist.fanin_begin[gate + 1]; ++i) {
            const db::NetId net = netlist.fanins[i];
            if (net < net_count) {
                ++loads[net];
            }
        }
    }

    std::vector<std::uint8_t> observable(net_count, 0);
    for (const db::NetId net : observables) {
        if (net < net_count) {
            observable[net] = 1;
        }
    }

    DisjointSet classes(raw.size());
    const auto index_of = [&fault_index](const db::NetId net, const sim::StuckValue stuck) {
        const std::size_t slot =
            static_cast<std::size_t>(net) * 2 + (stuck == sim::StuckValue::One ? 1 : 0);
        return slot < fault_index.size() ? fault_index[slot] : db::kInvalidId;
    };
    const auto unite = [&classes, &index_of](const db::NetId a, const sim::StuckValue a_stuck,
                                             const db::NetId b, const sim::StuckValue b_stuck) {
        const std::uint32_t first = index_of(a, a_stuck);
        const std::uint32_t second = index_of(b, b_stuck);
        if (first != db::kInvalidId && second != db::kInvalidId) {
            classes.unite(first, second);
        }
    };

    for (db::GateId gate = 0; gate < netlist.gate_count(); ++gate) {
        const db::GateType type = netlist.type[gate];
        if (type != db::GateType::Buf && type != db::GateType::Not) {
            continue;
        }
        if (netlist.fanin_begin[gate + 1] - netlist.fanin_begin[gate] != 1) {
            continue;
        }
        const db::NetId input = netlist.fanins[netlist.fanin_begin[gate]];
        const db::NetId output = netlist.out_net[gate];
        if (input >= net_count || output >= net_count || input == output) {
            continue;
        }
        if (observable[input] != 0 || loads[input] != 1) {
            continue;
        }
        if (type == db::GateType::Buf) {
            unite(input, sim::StuckValue::Zero, output, sim::StuckValue::Zero);
            unite(input, sim::StuckValue::One, output, sim::StuckValue::One);
        } else {
            unite(input, sim::StuckValue::Zero, output, sim::StuckValue::One);
            unite(input, sim::StuckValue::One, output, sim::StuckValue::Zero);
        }
    }

    std::vector<std::uint32_t> class_id(raw.size(), db::kInvalidId);
    for (std::size_t index = 0; index < raw.size(); ++index) {
        const std::uint32_t root = classes.find(static_cast<std::uint32_t>(index));
        if (class_id[root] == db::kInvalidId) {
            class_id[root] = static_cast<std::uint32_t>(result.faults.size());
            result.faults.push_back(raw[index]);
        }
        result.raw_to_collapsed[index] = class_id[root];
    }
    return result;
}

}  // namespace gmax::atpg
