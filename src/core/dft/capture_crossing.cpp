
#include "core/dft/capture_crossing.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace gmax::dft {
namespace {

using db::ConstraintClock;
using db::ConstraintFalsePath;
using db::FlatNetlist;
using db::GateType;
using db::NetId;
using db::StringTable;

bool state_holding(const GateType type) {
    return type == GateType::Dff || type == GateType::Dlat || type == GateType::ScanCell;
}

// The endpoints of this pass are the edge-triggered gates a procedure can capture
// on. A DLAT is a hold, not a capture: reading it as one would report the lockup
// latch itself as a crossing and reject every design that has one.
bool captures_on_edge(const GateType type) {
    return type == GateType::Dff || type == GateType::ScanCell;
}

NetId net_named(const FlatNetlist& flat, const StringTable& strings, const std::string& name) {
    if (name.empty()) {
        return db::kInvalidId;
    }
    for (NetId net = 0; net < flat.net_count(); ++net) {
        if (strings.at(flat.name_id[net]) == name) {
            return net;
        }
    }
    return db::kInvalidId;
}

std::string net_name(const FlatNetlist& flat, const StringTable& strings, const NetId net) {
    if (net == db::kInvalidId || net >= flat.net_count()) {
        return std::string();
    }
    return std::string(strings.at(flat.name_id[net]));
}

// A DFF's and a DLAT's first fanin is the data and the second is the clock or gate
// input, the order db::clock_gate_counts already reads.
NetId data_input(const FlatNetlist& flat, const std::uint32_t gate) {
    const std::uint32_t begin = flat.fanin_begin[gate];
    return begin < flat.fanin_begin[gate + 1] ? flat.fanins[begin] : db::kInvalidId;
}

NetId sampled_clock(const FlatNetlist& flat, const std::uint32_t gate) {
    const std::uint32_t begin = flat.fanin_begin[gate];
    return begin + 1 < flat.fanin_begin[gate + 1] ? flat.fanins[begin + 1] : db::kInvalidId;
}

// The net a declared clock drives: a clock names the object it drives, so that net
// is the domain key, as in db::clock_gate_counts.
NetId clock_net(const db::Constraints& constraints, const FlatNetlist& flat,
                const StringTable& strings, const std::string& name) {
    for (const ConstraintClock& clock : constraints.clocks) {
        if (clock.name == name) {
            return net_named(flat, strings, clock.source);
        }
    }
    return db::kInvalidId;
}

// True when this side of the false path names `target` without that name coming from
// a [get_clocks] query. A clock endpoint is DFT019's subject, so it excuses nothing;
// an object endpoint is a path the procedure does not claim to grade.
bool object_endpoint(const std::vector<std::string>& names, const std::vector<std::string>& clocks,
                     const std::string& target) {
    if (target.empty()) {
        return false;
    }
    if (std::find(names.begin(), names.end(), target) == names.end()) {
        return false;
    }
    return std::find(clocks.begin(), clocks.end(), target) == clocks.end();
}

// A false path excuses one crossing when it connects the launch flop's output to the
// capture flop's data input as objects.
bool false_path_excuses(const db::Constraints& constraints, const FlatNetlist& flat,
                        const StringTable& strings, const std::uint32_t launch,
                        const std::uint32_t capture) {
    const std::string launch_name = net_name(flat, strings, flat.out_net[launch]);
    const std::string capture_name = net_name(flat, strings, data_input(flat, capture));
    for (const ConstraintFalsePath& path : constraints.false_paths) {
        if (object_endpoint(path.from, path.from_clocks, launch_name) &&
            object_endpoint(path.to, path.to_clocks, capture_name)) {
            return true;
        }
    }
    return false;
}

// True when combinational logic carries a flop of `launch_net` into this capture
// flop's data input without a lockup latch. A DLAT on the route is the lockup latch of
// section 5, so the walk stops there; another flop stops it too, because a path
// through state is a new launch and belongs to another pair.
bool capture_flop_crosses(const db::Constraints& constraints, const FlatNetlist& flat,
                          const StringTable& strings, const NetId launch_net,
                          const std::uint32_t capture) {
    std::vector<NetId> pending;
    pending.push_back(data_input(flat, capture));
    std::vector<char> seen(flat.gate_count(), 0);
    while (!pending.empty()) {
        const NetId net = pending.back();
        pending.pop_back();
        if (net == db::kInvalidId) {
            continue;
        }
        const std::uint32_t driver = flat.driver[net];
        if (driver == db::kPrimaryInput || driver == db::kInvalidId || seen[driver] != 0) {
            continue;
        }
        seen[driver] = 1;
        if (state_holding(flat.type[driver])) {
            if (flat.type[driver] != GateType::Dlat && sampled_clock(flat, driver) == launch_net &&
                !false_path_excuses(constraints, flat, strings, driver, capture)) {
                return true;
            }
            continue;
        }
        for (std::uint32_t fanin = flat.fanin_begin[driver]; fanin < flat.fanin_begin[driver + 1];
             ++fanin) {
            pending.push_back(flat.fanins[fanin]);
        }
    }
    return false;
}

bool crossing_exists(const db::Constraints& constraints, const FlatNetlist& flat,
                     const StringTable& strings, const NetId launch_net, const NetId capture_net) {
    for (std::uint32_t gate = 0; gate < flat.gate_count(); ++gate) {
        if (!captures_on_edge(flat.type[gate]) || sampled_clock(flat, gate) != capture_net) {
            continue;
        }
        if (capture_flop_crosses(constraints, flat, strings, launch_net, gate)) {
            return true;
        }
    }
    return false;
}

// Where the procedure captures `clock`, as a cycle and an inner group index.
bool pulses(const CaptureProcedure& procedure, const std::string& clock, std::size_t& cycle,
            std::size_t& group) {
    for (std::size_t c = 0; c < procedure.cycles.size(); ++c) {
        for (std::size_t g = 0; g < procedure.cycles[c].groups.size(); ++g) {
            const std::vector<std::string>& clocks = procedure.cycles[c].groups[g].timing.clocks;
            if (std::find(clocks.begin(), clocks.end(), clock) != clocks.end()) {
                cycle = c;
                group = g;
                return true;
            }
        }
    }
    return false;
}

std::vector<std::string> sorted_unique(std::vector<std::string> names) {
    std::sort(names.begin(), names.end());
    names.erase(std::unique(names.begin(), names.end()), names.end());
    return names;
}

// The clocks one cycle captures, bytewise sorted and unique, so the pairing and the
// message do not depend on how the profile ordered its groups.
std::vector<std::string> cycle_clocks(const CaptureCycle& cycle) {
    std::vector<std::string> clocks;
    for (const CaptureGroup& group : cycle.groups) {
        clocks.insert(clocks.end(), group.timing.clocks.begin(), group.timing.clocks.end());
    }
    std::sort(clocks.begin(), clocks.end());
    clocks.erase(std::unique(clocks.begin(), clocks.end()), clocks.end());
    return clocks;
}

}  // namespace

std::vector<CaptureRefusal> find_crossing_refusals(const CaptureProcedure& procedure,
                                                   const db::Constraints& constraints,
                                                   const db::FlatNetlist& flat,
                                                   const db::StringTable& strings) {
    std::vector<CaptureRefusal> refusals;
    for (std::size_t earlier = 0; earlier < procedure.cycles.size(); ++earlier) {
        const std::vector<std::string> launch_clocks = cycle_clocks(procedure.cycles[earlier]);
        for (std::size_t later = earlier + 1; later < procedure.cycles.size(); ++later) {
            const std::vector<std::string> capture_clocks = cycle_clocks(procedure.cycles[later]);
            for (const std::string& launch : launch_clocks) {
                const NetId launch_net = clock_net(constraints, flat, strings, launch);
                if (launch_net == db::kInvalidId) {
                    continue;
                }
                for (const std::string& capture : capture_clocks) {
                    if (capture == launch) {
                        continue;
                    }
                    const NetId capture_net = clock_net(constraints, flat, strings, capture);
                    if (capture_net == db::kInvalidId ||
                        !crossing_exists(constraints, flat, strings, launch_net, capture_net)) {
                        continue;
                    }
                    CaptureRefusal refusal;
                    refusal.code = CaptureRefusalCode::CrossingWithoutLockup;
                    refusal.detail = "cycle " + std::to_string(earlier) + " captures '" + launch +
                                     "' before cycle " + std::to_string(later) + " captures '" +
                                     capture +
                                     "', and the path between them has no lockup latch or false "
                                     "path";
                    refusals.push_back(std::move(refusal));
                }
            }
        }
    }
    sort_capture_refusals(refusals);
    return refusals;
}

std::vector<FalsePathWarning> find_false_path_warnings(const CaptureProcedure& procedure,
                                                       const db::Constraints& constraints) {
    std::vector<FalsePathWarning> warnings;
    for (const db::ConstraintFalsePath& path : constraints.false_paths) {
        for (const std::string& from : sorted_unique(path.from_clocks)) {
            std::size_t from_cycle = 0;
            std::size_t from_group = 0;
            if (!pulses(procedure, from, from_cycle, from_group)) {
                continue;
            }
            for (const std::string& to : sorted_unique(path.to_clocks)) {
                std::size_t to_cycle = 0;
                std::size_t to_group = 0;
                if (!pulses(procedure, to, to_cycle, to_group)) {
                    continue;
                }
                // One group of one cycle is one capture edge, so a path between clocks
                // captured together hides no edge of this procedure.
                if (from_cycle == to_cycle && from_group == to_group) {
                    continue;
                }
                FalsePathWarning warning;
                warning.from_clock = from;
                warning.to_clock = to;
                warning.line = path.line;
                warnings.push_back(std::move(warning));
            }
        }
    }
    std::sort(warnings.begin(), warnings.end(),
              [](const FalsePathWarning& left, const FalsePathWarning& right) {
                  if (left.line != right.line) {
                      return left.line < right.line;
                  }
                  if (left.from_clock != right.from_clock) {
                      return left.from_clock < right.from_clock;
                  }
                  return left.to_clock < right.to_clock;
              });
    warnings.erase(std::unique(warnings.begin(), warnings.end(),
                               [](const FalsePathWarning& left, const FalsePathWarning& right) {
                                   return left.line == right.line &&
                                          left.from_clock == right.from_clock &&
                                          left.to_clock == right.to_clock;
                               }),
                   warnings.end());
    return warnings;
}

}  // namespace gmax::dft
