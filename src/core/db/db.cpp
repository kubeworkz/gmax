#include "core/db/db.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <map>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "core/db/dfdb.h"

namespace gmax::db {

const Module* find_module(const Design& design, const std::string_view name) {
    for (const Module& module : design.modules) {
        if (module.name == name) {
            return &module;
        }
    }
    return nullptr;
}

namespace {

std::string parent_of(const std::string& path) {
    const auto slash = path.rfind('/');
    return slash == std::string::npos ? std::string() : path.substr(0, slash);
}

}  // namespace

Status DesignDb::flatten() {
    flat_ = FlatNetlist{};
    strings_ = StringTable{};

    // Canonical form. Sorting here, in one place, is what makes two designs that
    // differ only in insertion order serialize to the same bytes.
    std::sort(design_.modules.begin(), design_.modules.end(),
              [](const Module& a, const Module& b) { return a.name < b.name; });
    std::sort(design_.instances.begin(), design_.instances.end(),
              [](const Instance& a, const Instance& b) { return a.path < b.path; });
    for (Module& module : design_.modules) {
        std::sort(module.gates.begin(), module.gates.end(),
                  [](const Gate& a, const Gate& b) { return a.name < b.name; });
    }

    // Intern every name the design references, not just net paths. The table is
    // then complete once flatten() returns, so save() adds no ids and load()
    // reproduces the table exactly.
    strings_.intern(design_.top);
    for (const Module& module : design_.modules) {
        strings_.intern(module.name);
        for (const std::string& name : module.inputs) {
            strings_.intern(name);
        }
        for (const std::string& name : module.outputs) {
            strings_.intern(name);
        }
        for (const Gate& gate : module.gates) {
            strings_.intern(gate.name);
            for (const std::string& name : gate.fanins) {
                strings_.intern(name);
            }
            strings_.intern(gate.out);
        }
    }
    for (const Instance& instance : design_.instances) {
        strings_.intern(instance.path);
        strings_.intern(instance.module);
        for (const auto& [port, net] : instance.connections) {
            strings_.intern(port);
            strings_.intern(net);
        }
    }

    const Module* top = find_module(design_, design_.top);
    if (top == nullptr) {
        return Status::failure(Error::BadPayload,
                               "top module '" + design_.top + "' is not defined");
    }

    // Instance paths are relative to the top ("u03", "u03/x"), so grouping by
    // parent keeps the walk depth-first with siblings visited in path order.
    std::map<std::string, std::vector<const Instance*>> children;
    for (const Instance& instance : design_.instances) {
        children[parent_of(instance.path)].push_back(&instance);
    }
    for (auto& entry : children) {
        std::sort(entry.second.begin(), entry.second.end(),
                  [](const Instance* a, const Instance* b) { return a->path < b->path; });
    }

    std::unordered_map<std::string, NetId> net_ids;
    std::uint32_t next_gate = 0;

    // One module instance being walked. `ports` maps this instance's port names
    // to the net ids they are bound to in the parent: that is how a gate inside a
    // leaf resolves a fanin that is really a top-level net.
    struct Walker {
        FlatNetlist& flat;
        StringTable& strings;
        std::unordered_map<std::string, NetId>& net_ids;
        const std::map<std::string, std::vector<const Instance*>>& children;
        const Design& design;
        std::uint32_t& next_gate;

        NetId net_for(const std::string& prefix, const std::map<std::string, NetId>& ports,
                      const std::string& name) {
            if (const auto it = ports.find(name); it != ports.end()) {
                return it->second;
            }
            const std::string path = prefix.empty() ? name : prefix + "/" + name;
            if (const auto it = net_ids.find(path); it != net_ids.end()) {
                return it->second;
            }
            const auto id = static_cast<NetId>(flat.driver.size());
            flat.driver.push_back(kInvalidId);
            flat.name_id.push_back(strings.intern(path));
            flat.source_loc.push_back(kInvalidId);
            net_ids.emplace(path, id);
            return id;
        }

        Status walk(const Module& module, const std::string& prefix,
                    const std::map<std::string, NetId>& ports, bool is_top) {
            // Only the top module's unbound inputs are primary inputs. An
            // instance input bound to a parent net may still be undriven here
            // because the sibling that drives it sorts later; stamping that net
            // as a primary input would make the real driver look like a second
            // one.
            for (const std::string& name : module.inputs) {
                const NetId net = net_for(prefix, ports, name);
                const bool bound = ports.find(name) != ports.end();
                if (is_top && !bound && flat.driver[net] == kInvalidId) {
                    flat.driver[net] = kPrimaryInput;
                }
            }
            for (const std::string& name : module.outputs) {
                net_for(prefix, ports, name);
            }

            for (const Gate& gate : module.gates) {
                const bool has_async = !gate.async_clear.empty() || !gate.async_preset.empty();
                if (has_async && gate.type != GateType::Dff && gate.type != GateType::Dlat) {
                    return Status::failure(
                        Error::BadPayload,
                        "gate '" + gate.name +
                            "' carries an asynchronous control but is not a DFF or DLAT");
                }
                if (!gate.enable.empty() && gate.type != GateType::Dff) {
                    return Status::failure(Error::BadPayload,
                                           "gate '" + gate.name +
                                               "' carries an enable but is not a DFF");
                }
                if (gate.clock_edge == ClockEdge::Falling && gate.type != GateType::Dff) {
                    return Status::failure(Error::BadPayload,
                                           "gate '" + gate.name +
                                               "' has a falling clock edge but is not a DFF");
                }
                for (const std::string* control :
                     {&gate.enable, &gate.async_clear, &gate.async_preset}) {
                    if (!control->empty() && std::find(gate.fanins.begin(), gate.fanins.end(),
                                                       *control) == gate.fanins.end()) {
                        return Status::failure(Error::BadPayload,
                                               "gate '" + gate.name +
                                                   "' names sequential control net '" + *control +
                                                   "', which is not one of its fanins");
                    }
                }
                const auto id = static_cast<GateId>(next_gate++);
                flat.type.push_back(gate.type);
                flat.fanin_begin.push_back(static_cast<std::uint32_t>(flat.fanins.size()));
                for (const std::string& name : gate.fanins) {
                    flat.fanins.push_back(net_for(prefix, ports, name));
                }
                flat.fanout_begin.push_back(static_cast<std::uint32_t>(flat.fanouts.size()));

                NetId out = kInvalidId;
                if (!gate.out.empty()) {
                    out = net_for(prefix, ports, gate.out);
                    if (flat.driver[out] != kInvalidId) {
                        return Status::failure(Error::MultipleDrivers,
                                               "net '" +
                                                   std::string(strings.at(flat.name_id[out])) +
                                                   "' has more than one driver");
                    }
                    flat.driver[out] = id;
                    flat.fanouts.push_back(out);
                }
                flat.out_net.push_back(out);
                flat.flags.push_back(0);
                flat.level.push_back(0);
                flat.lib_cell.push_back(gate.lib_cell);
                flat.clock_edge.push_back(gate.clock_edge);
                flat.enable.push_back(gate.enable.empty() ? kInvalidId
                                                          : net_for(prefix, ports, gate.enable));
                flat.async_clear.push_back(gate.async_clear.empty()
                                               ? kInvalidId
                                               : net_for(prefix, ports, gate.async_clear));
                flat.async_preset.push_back(gate.async_preset.empty()
                                                ? kInvalidId
                                                : net_for(prefix, ports, gate.async_preset));
            }

            if (const auto it = children.find(prefix); it != children.end()) {
                for (const Instance* child : it->second) {
                    const Module* child_module = find_module(design, child->module);
                    if (child_module == nullptr) {
                        return Status::failure(Error::BadPayload,
                                               "instance '" + child->path + "' names module '" +
                                                   child->module + "', which is not defined");
                    }
                    std::map<std::string, NetId> child_ports;
                    for (const auto& [port, net_name] : child->connections) {
                        child_ports[port] = net_for(prefix, ports, net_name);
                    }
                    const Status status = walk(*child_module, child->path, child_ports, false);
                    if (!status) {
                        return status;
                    }
                }
            }
            return Status{};
        }
    };

    Walker walker{flat_, strings_, net_ids, children, design_, next_gate};
    const Status walked = walker.walk(*top, std::string(), {}, true);
    if (!walked) {
        return walked;
    }
    flat_.fanin_begin.push_back(static_cast<std::uint32_t>(flat_.fanins.size()));
    flat_.fanout_begin.push_back(static_cast<std::uint32_t>(flat_.fanouts.size()));

    // Levels fall out of one topological pass over combinational edges. A
    // sequential gate's output is state, not a combinational dependency, so
    // edges out of DFF/DLAT/SCAN_CELL are cut: a flop whose next-state cone
    // reads its own output is a legal register rather than a loop. A gate that
    // never reaches indegree zero is part of a real combinational loop, which is
    // reported rather than silently ordered (architecture 6.3).
    const std::size_t gate_count = flat_.gate_count();
    const auto is_sequential = [](GateType type) {
        return type == GateType::Dff || type == GateType::Dlat || type == GateType::ScanCell;
    };
    std::vector<std::uint32_t> indegree(gate_count, 0);
    std::vector<std::vector<GateId>> consumers(flat_.net_count());
    for (std::size_t gate = 0; gate < gate_count; ++gate) {
        for (std::uint32_t i = flat_.fanin_begin[gate]; i < flat_.fanin_begin[gate + 1]; ++i) {
            const NetId net = flat_.fanins[i];
            const std::uint32_t driver = flat_.driver[net];
            if (driver != kPrimaryInput && driver != kInvalidId &&
                !is_sequential(flat_.type[driver])) {
                consumers[net].push_back(static_cast<GateId>(gate));
                ++indegree[gate];
            }
        }
    }
    std::vector<GateId> ready;
    for (std::size_t gate = 0; gate < gate_count; ++gate) {
        if (indegree[gate] == 0) {
            ready.push_back(static_cast<GateId>(gate));
        }
    }
    std::size_t processed = 0;
    while (!ready.empty()) {
        const GateId gate = ready.back();
        ready.pop_back();
        ++processed;
        for (std::uint32_t i = flat_.fanout_begin[gate]; i < flat_.fanout_begin[gate + 1]; ++i) {
            const NetId net = flat_.fanouts[i];
            for (const GateId consumer : consumers[net]) {
                flat_.level[consumer] = std::max(flat_.level[consumer], flat_.level[gate] + 1);
                if (--indegree[consumer] == 0) {
                    ready.push_back(consumer);
                }
            }
        }
    }
    if (processed != gate_count) {
        return Status::failure(Error::CycleDetected, "the design has a combinational loop; " +
                                                         std::to_string(gate_count - processed) +
                                                         " gate(s) cannot be ordered");
    }
    return Status{};
}

Status DesignDb::save(const std::string& path) const {
    const std::vector<std::uint8_t> bytes =
        dfdb::serialize(design_, flat_, strings_, constraints_.present ? &constraints_ : nullptr);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        return Status::failure(Error::IoError, "cannot open '" + path + "' for writing");
    }
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
    out.close();
    if (!out) {
        return Status::failure(Error::IoError, "cannot write '" + path + "'");
    }
    return Status{};
}

Status DesignDb::load(const std::string& path, DesignDb& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return Status::failure(Error::IoError, "cannot open '" + path + "' for reading");
    }
    in.seekg(0, std::ios::end);
    const std::streamoff size = in.tellg();
    if (size < 0) {
        return Status::failure(Error::IoError, "cannot size '" + path + "'");
    }
    in.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    if (!bytes.empty()) {
        in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    if (!in) {
        return Status::failure(Error::IoError, "cannot read '" + path + "'");
    }
    return dfdb::parse(bytes, out.design_, out.flat_, out.strings_, &out.constraints_);
}

}  // namespace gmax::db
