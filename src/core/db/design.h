#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "core/db/ids.h"

namespace gmax::db {

// One primitive inside a module (architecture 6.2). Nets are named, not
// numbered, at this level: numbering is a property of the flattened view.
struct Gate {
    GateType type = GateType::Buf;
    std::string name;
    std::vector<std::string> fanins;
    std::string out;
    std::uint32_t lib_cell = kInvalidId;

    // Sequential controls (architecture 6.2). enable holds the output while it
    // is low; async_clear and async_preset force the output to 0 or 1 without a
    // clock edge. All are normalized to active high during liberty import, so
    // the polarity of the library pin never reaches ATPG. Empty means the gate
    // has no such control. Each also appears in fanins, which keeps reachability
    // and levelization from missing the control; these fields say which role it
    // has. enable is set only on a DFF: a DLAT's transparency control is its
    // second fanin. A DFF's fanins are data, clock, enable, clear, preset and a
    // DLAT's are data, enable, clear, preset, each control only when present;
    // consumers should read these fields rather than infer roles from positions.
    std::string enable;
    std::string async_clear;
    std::string async_preset;

    friend bool operator==(const Gate&, const Gate&) = default;
};

// Design -> Module -> Instance -> Port/Net (architecture 6.1). The module keeps
// declaration order; the writer sorts modules by name before serializing.
struct Module {
    std::string name;
    std::vector<std::string> inputs;
    std::vector<std::string> outputs;
    std::vector<Gate> gates;

    friend bool operator==(const Module&, const Module&) = default;
};

// Connections are keyed by port name in a std::map, so sorted-key serialization
// (architecture 6.6) holds by construction rather than by remembering to sort.
struct Instance {
    std::string path;  // relative to top, e.g. "u03"
    std::string module;
    std::map<std::string, std::string> connections;

    friend bool operator==(const Instance&, const Instance&) = default;
};

struct Design {
    std::string top;
    std::vector<Module> modules;
    std::vector<Instance> instances;

    friend bool operator==(const Design&, const Design&) = default;
};

const Module* find_module(const Design& design, std::string_view name);

}  // namespace gmax::db
