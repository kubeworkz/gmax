// WP-004's exit check: Yosys JSON becomes a design, malformed or unsupported
// constructs become structured diagnostics, and a reference cell-count report
// reconciles against the JSON. The live tests use the pinned Yosys when it is
// available and skip cleanly otherwise.

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/db/db.h"
#include "core/frontend/diagnostics.h"
#include "core/frontend/yosys.h"

namespace {

int failures = 0;

void expect(const bool ok, const std::string_view what) {
    if (!ok) {
        std::cerr << "FAIL " << what << std::endl;
        ++failures;
    }
}

std::string quote(const std::string_view value) {
    std::string quoted = "\"";
    for (const char c : value) {
        if (c == '"' || c == '\\') {
            quoted.push_back('\\');
        }
        quoted.push_back(c);
    }
    quoted.push_back('"');
    return quoted;
}

std::string object(std::initializer_list<std::pair<std::string, std::string>> members) {
    std::string out = "{";
    bool first = true;
    for (const auto& [key, value] : members) {
        if (!first) {
            out += ",";
        }
        first = false;
        out += quote(key);
        out += ":";
        out += value;
    }
    out += "}";
    return out;
}

std::string bits(std::initializer_list<int> values) {
    std::string out = "[";
    bool first = true;
    for (const int value : values) {
        if (!first) {
            out += ",";
        }
        first = false;
        out += std::to_string(value);
    }
    out += "]";
    return out;
}

std::string binary(const std::uint32_t value) {
    std::string out(32, '0');
    for (std::uint32_t bit = 0; bit < 32; ++bit) {
        out[31 - bit] = ((value >> bit) & 1u) != 0 ? '1' : '0';
    }
    return out;
}

std::string width_params(const std::uint32_t a, const std::uint32_t b, const std::uint32_t y) {
    return object({{"A_WIDTH", quote(binary(a))},
                   {"B_WIDTH", quote(binary(b))},
                   {"Y_WIDTH", quote(binary(y))}});
}

std::string unary_params(const std::uint32_t a, const std::uint32_t y) {
    return object({{"A_WIDTH", quote(binary(a))}, {"Y_WIDTH", quote(binary(y))}});
}

std::string mux_params(const std::uint32_t width) {
    return object({{"WIDTH", quote(binary(width))}});
}

std::string dff_params(const std::uint32_t width, const std::uint32_t clock_polarity = 1) {
    return object(
        {{"WIDTH", quote(binary(width))}, {"CLK_POLARITY", quote(binary(clock_polarity))}});
}

std::string dffe_params(const std::uint32_t width, const std::uint32_t clock_polarity,
                        const std::uint32_t enable_polarity) {
    return object({{"WIDTH", quote(binary(width))},
                   {"CLK_POLARITY", quote(binary(clock_polarity))},
                   {"EN_POLARITY", quote(binary(enable_polarity))}});
}

std::string adff_params(const std::uint32_t width, const std::uint32_t clock_polarity,
                        const std::uint32_t reset_polarity, const std::uint32_t reset_value) {
    return object({{"WIDTH", quote(binary(width))},
                   {"CLK_POLARITY", quote(binary(clock_polarity))},
                   {"ARST_POLARITY", quote(binary(reset_polarity))},
                   {"ARST_VALUE", quote(binary(reset_value))}});
}

std::string dffsr_params(const std::uint32_t width, const std::uint32_t clock_polarity,
                         const std::uint32_t set_polarity, const std::uint32_t clear_polarity) {
    return object({{"WIDTH", quote(binary(width))},
                   {"CLK_POLARITY", quote(binary(clock_polarity))},
                   {"SET_POLARITY", quote(binary(set_polarity))},
                   {"CLR_POLARITY", quote(binary(clear_polarity))}});
}

std::string dlatch_params(const std::uint32_t width, const std::uint32_t enable_polarity) {
    return object(
        {{"WIDTH", quote(binary(width))}, {"EN_POLARITY", quote(binary(enable_polarity))}});
}

std::string port_json(const std::string_view direction, std::initializer_list<int> values) {
    return object({{"direction", quote(direction)}, {"bits", bits(values)}});
}

std::string cell_json(const std::string_view type, const std::string& parameters,
                      const std::string& connections, const std::string& source = "") {
    std::string cell = "{\"type\":" + quote(type);
    if (!parameters.empty()) {
        cell += ",\"parameters\":" + parameters;
    }
    if (!connections.empty()) {
        cell += ",\"connections\":" + connections;
    }
    if (!source.empty()) {
        cell += ",\"attributes\":" + object({{"src", quote(source)}});
    }
    cell += "}";
    return cell;
}

std::string design_json(std::initializer_list<std::pair<std::string, std::string>> ports,
                        std::initializer_list<std::pair<std::string, std::string>> cells,
                        std::initializer_list<std::pair<std::string, std::string>> netnames = {}) {
    std::string module = "{\"ports\":" + object(ports) + ",\"cells\":" + object(cells);
    if (netnames.size() != 0) {
        module += ",\"netnames\":" + object(netnames);
    }
    module += "}";
    return object({{"modules", object({{"top", module}})}});
}

bool has_code(const std::vector<gmax::frontend::Diagnostic>& diagnostics,
              const gmax::frontend::DiagnosticCode code) {
    for (const auto& diagnostic : diagnostics) {
        if (diagnostic.code == code) {
            return true;
        }
    }
    return false;
}

bool has_width_message(const std::vector<gmax::frontend::Diagnostic>& diagnostics,
                       const std::string_view message) {
    for (const auto& diagnostic : diagnostics) {
        if (diagnostic.code == gmax::frontend::DiagnosticCode::WidthMismatch &&
            diagnostic.message == message) {
            return true;
        }
    }
    return false;
}

void print_diagnostics(const std::vector<gmax::frontend::Diagnostic>& diagnostics,
                       const std::string_view context) {
    for (const auto& diagnostic : diagnostics) {
        std::cerr << context << ": " << gmax::frontend::diagnostic_code_name(diagnostic.code)
                  << ": " << diagnostic.message;
        if (!diagnostic.source.file.empty()) {
            std::cerr << " (" << diagnostic.source.file << ":" << diagnostic.source.line;
            if (diagnostic.source.column != 0) {
                std::cerr << "." << diagnostic.source.column;
            }
            std::cerr << ")";
        }
        std::cerr << std::endl;
    }
}

const std::string kSource = "top.v:7.2-7.9";

std::string smoke_design() {
    return design_json(
        {{"a", port_json("input", {2})},
         {"b", port_json("input", {3})},
         {"y", port_json("output", {4})}},
        {{"$and$1", cell_json("$and", width_params(1, 1, 1),
                              object({{"A", bits({2})}, {"B", bits({3})}, {"Y", bits({4})}}),
                              "smoke.v:10.16-10.21")}},
        {{"a", object({{"bits", bits({2})}, {"hide_name", "0"}})},
         {"b", object({{"bits", bits({3})}, {"hide_name", "0"}})},
         {"y", object({{"bits", bits({4})}, {"hide_name", "0"}})}});
}

void test_smoke() {
    const auto result = gmax::frontend::import_yosys_json(smoke_design(), "top");
    expect(result.ok, "the smoke JSON imports");
    if (!result.ok) {
        print_diagnostics(result.diagnostics, "smoke");
    }
    expect(result.design.top == "top", "the imported design names top");
    expect(result.design.modules.size() == 1, "the smoke design has one module");
    expect(result.design.modules.size() == 1 && result.design.modules[0].gates.size() == 1,
           "the smoke design has one gate");
    expect(result.counts.yosys_cells.size() == 1 && result.counts.yosys_cells.at("$and") == 1,
           "the Yosys cell count sees one $and");
    expect(result.counts.primitives.size() == 1 && result.counts.primitives.at("AND2") == 1,
           "the primitive count sees one AND2");
}

// ADR-012 addressing: one net can carry both the Verilog name and a Yosys alias
// such as `$flatten$...`. An SDC file or a fault list can only name the former,
// so the source name must win even though '$' sorts before letters.
void test_source_name_beats_generated_alias() {
    const std::string json = design_json(
        {{"a", port_json("input", {2})},
         {"b", port_json("input", {3})},
         {"y", port_json("output", {5})}},
        {{"$and$1", cell_json("$and", width_params(1, 1, 1),
                              object({{"A", bits({2})}, {"B", bits({3})}, {"Y", bits({4})}}))},
         {"$and$2", cell_json("$and", width_params(1, 1, 1),
                              object({{"A", bits({4})}, {"B", bits({4})}, {"Y", bits({5})}}))}},
        {{"$flatten$n4", object({{"bits", bits({4})}, {"hide_name", "0"}})},
         {"n4", object({{"bits", bits({4})}, {"hide_name", "0"}})}});
    const auto result = gmax::frontend::import_yosys_json(json, "top");
    expect(result.ok, "a design with an aliased internal net imports");
    if (!result.ok || result.design.modules.empty()) {
        print_diagnostics(result.diagnostics, "net name preference");
        return;
    }
    bool named_source = false;
    bool named_alias = false;
    for (const auto& gate : result.design.modules[0].gates) {
        named_source = named_source || gate.out == "n4";
        named_alias = named_alias || gate.out == "$flatten$n4";
    }
    expect(named_source, "an internal net keeps the name its source gave it");
    expect(!named_alias, "a generated alias does not become the net name");
}

void test_mux() {
    const std::string json = design_json(
        {{"a", port_json("input", {2})},
         {"b", port_json("input", {3})},
         {"s", port_json("input", {4})},
         {"y", port_json("output", {5})}},
        {{"$mux$1",
          cell_json(
              "$mux", mux_params(1),
              object({{"A", bits({3})}, {"B", bits({2})}, {"S", bits({4})}, {"Y", bits({5})}}))}});
    const auto result = gmax::frontend::import_yosys_json(json, "top");
    expect(result.ok, "a scalar $mux imports");
    if (!result.ok) {
        print_diagnostics(result.diagnostics, "mux");
    }
    expect(result.design.modules.size() == 1 && result.design.modules[0].gates.size() == 1,
           "the mux imports one gate");
    if (result.design.modules.size() == 1 && result.design.modules[0].gates.size() == 1) {
        const auto& gate = result.design.modules[0].gates[0];
        expect(gate.type == gmax::db::GateType::Mux2, "the mux maps to MUX2");
        expect(gate.fanins == std::vector<std::string>{"a", "b", "s"},
               "Yosys mux A/B become the false/true inputs in Mux2 order");
        expect(gate.out == "y", "the mux drives its Y net");
    }
}

void test_bus_and() {
    const std::string json =
        design_json({{"a", port_json("input", {2, 3, 4, 5})},
                     {"b", port_json("input", {6, 7, 8, 9})},
                     {"y", port_json("output", {10, 11, 12, 13})}},
                    {{"$and$1", cell_json("$and", width_params(4, 4, 4),
                                          object({{"A", bits({2, 3, 4, 5})},
                                                  {"B", bits({6, 7, 8, 9})},
                                                  {"Y", bits({10, 11, 12, 13})}}))}});
    const auto result = gmax::frontend::import_yosys_json(json, "top");
    expect(result.ok, "a four-bit $and imports");
    if (!result.ok) {
        print_diagnostics(result.diagnostics, "bus and");
    }
    expect(result.design.modules.size() == 1 && result.design.modules[0].gates.size() == 4,
           "the four-bit $and becomes four gates");
    if (result.design.modules.size() == 1) {
        bool saw[4] = {false, false, false, false};
        for (const auto& gate : result.design.modules[0].gates) {
            expect(gate.type == gmax::db::GateType::And2, "each $and lane is AND2");
            for (int lane = 0; lane < 4; ++lane) {
                if (gate.out == "y[" + std::to_string(lane) + "]") {
                    saw[lane] = true;
                    expect(gate.fanins ==
                               std::vector<std::string>{"a[" + std::to_string(lane) + "]",
                                                        "b[" + std::to_string(lane) + "]"},
                           "each $and lane reads the matching A/B bits");
                }
            }
        }
        for (const bool lane : saw) {
            expect(lane, "the four-bit $and drives every y lane");
        }
    }
    expect(result.counts.yosys_cells.size() == 1 && result.counts.yosys_cells.at("$and") == 1,
           "the bus $and is one Yosys cell");
    expect(result.counts.primitives.size() == 1 && result.counts.primitives.at("AND2") == 4,
           "the bus $and becomes four AND2 primitives");
}

void test_dff() {
    const std::string json = design_json(
        {{"d", port_json("input", {2})},
         {"ck", port_json("input", {3})},
         {"q", port_json("output", {4})}},
        {{"$dff$1", cell_json("$_DFF_P_", "",
                              object({{"D", bits({2})}, {"C", bits({3})}, {"Q", bits({4})}}))}});
    const auto result = gmax::frontend::import_yosys_json(json, "top");
    expect(result.ok, "a gate-level DFF imports");
    if (!result.ok) {
        print_diagnostics(result.diagnostics, "dff");
    }
    expect(result.design.modules.size() == 1 && result.design.modules[0].gates.size() == 1,
           "the DFF imports one gate");
    if (result.design.modules.size() == 1 && result.design.modules[0].gates.size() == 1) {
        const auto& gate = result.design.modules[0].gates[0];
        expect(gate.type == gmax::db::GateType::Dff, "the DFF maps to DFF");
        expect(gate.clock_edge == gmax::db::ClockEdge::Rising, "P is a rising-edge flop");
        expect(gate.fanins == std::vector<std::string>{"d", "ck"}, "the DFF keeps D and C");
        expect(gate.out == "q", "the DFF drives Q");
    }
}

void test_bus_dff() {
    const std::string json =
        design_json({{"d", port_json("input", {2, 3, 4, 5})},
                     {"clk", port_json("input", {6})},
                     {"q", port_json("output", {7, 8, 9, 10})}},
                    {{"$dff$1", cell_json("$dff", dff_params(4),
                                          object({{"D", bits({2, 3, 4, 5})},
                                                  {"CLK", bits({6})},
                                                  {"Q", bits({7, 8, 9, 10})}}))}});
    const auto result = gmax::frontend::import_yosys_json(json, "top");
    expect(result.ok, "a four-bit $dff imports");
    if (!result.ok) {
        print_diagnostics(result.diagnostics, "bus dff");
    }
    expect(result.design.modules.size() == 1 && result.design.modules[0].gates.size() == 4,
           "the four-bit $dff becomes four gates");
    if (result.design.modules.size() == 1) {
        bool saw[4] = {false, false, false, false};
        for (const auto& gate : result.design.modules[0].gates) {
            expect(gate.type == gmax::db::GateType::Dff, "each bus-DFF lane is a DFF");
            expect(gate.clock_edge == gmax::db::ClockEdge::Rising,
                   "each bus-DFF lane keeps the rising edge");
            for (int lane = 0; lane < 4; ++lane) {
                if (gate.out == "q[" + std::to_string(lane) + "]") {
                    saw[lane] = true;
                    expect(gate.fanins ==
                               std::vector<std::string>{"d[" + std::to_string(lane) + "]", "clk"},
                           "each bus-DFF lane reads the matching D bit");
                }
            }
        }
        for (const bool lane : saw) {
            expect(lane, "the four-bit $dff drives every q lane");
        }
    }
    expect(result.counts.yosys_cells.size() == 1 && result.counts.yosys_cells.at("$dff") == 1,
           "the bus DFF is one Yosys cell");
    expect(result.counts.primitives.size() == 1 && result.counts.primitives.at("DFF") == 4,
           "the bus DFF becomes four DFF primitives");
}

void test_bus_mux() {
    const std::string json =
        design_json({{"a", port_json("input", {2, 3, 4, 5})},
                     {"b", port_json("input", {6, 7, 8, 9})},
                     {"s", port_json("input", {10})},
                     {"y", port_json("output", {11, 12, 13, 14})}},
                    {{"$mux$1", cell_json("$mux", mux_params(4),
                                          object({{"A", bits({2, 3, 4, 5})},
                                                  {"B", bits({6, 7, 8, 9})},
                                                  {"S", bits({10})},
                                                  {"Y", bits({11, 12, 13, 14})}}))}});
    const auto result = gmax::frontend::import_yosys_json(json, "top");
    expect(result.ok, "a four-bit $mux imports");
    if (!result.ok) {
        print_diagnostics(result.diagnostics, "bus mux");
    }
    expect(result.design.modules.size() == 1 && result.design.modules[0].gates.size() == 4,
           "the four-bit $mux becomes four gates");
    if (result.design.modules.size() == 1) {
        bool saw[4] = {false, false, false, false};
        for (const auto& gate : result.design.modules[0].gates) {
            expect(gate.type == gmax::db::GateType::Mux2, "each $mux lane is MUX2");
            for (int lane = 0; lane < 4; ++lane) {
                if (gate.out == "y[" + std::to_string(lane) + "]") {
                    saw[lane] = true;
                    expect(gate.fanins ==
                               std::vector<std::string>{"b[" + std::to_string(lane) + "]",
                                                        "a[" + std::to_string(lane) + "]", "s"},
                           "each $mux lane selects the matching A/B bit");
                }
            }
        }
        for (const bool lane : saw) {
            expect(lane, "the four-bit $mux drives every y lane");
        }
    }
    expect(result.counts.yosys_cells.size() == 1 && result.counts.yosys_cells.at("$mux") == 1,
           "the bus $mux is one Yosys cell");
    expect(result.counts.primitives.size() == 1 && result.counts.primitives.at("MUX2") == 4,
           "the bus $mux becomes four MUX2 primitives");
}

void test_bus_bitwise() {
    struct Case {
        const char* cell;
        gmax::db::GateType gate;
        const char* primitive;
    };
    const Case cases[] = {{"$or", gmax::db::GateType::Or2, "OR2"},
                          {"$xor", gmax::db::GateType::Xor2, "XOR2"},
                          {"$xnor", gmax::db::GateType::Xnor2, "XNOR2"}};
    for (const Case& test : cases) {
        const std::string json =
            design_json({{"a", port_json("input", {2, 3, 4, 5})},
                         {"b", port_json("input", {6, 7, 8, 9})},
                         {"y", port_json("output", {10, 11, 12, 13})}},
                        {{"$cell$1", cell_json(test.cell, width_params(4, 4, 4),
                                               object({{"A", bits({2, 3, 4, 5})},
                                                       {"B", bits({6, 7, 8, 9})},
                                                       {"Y", bits({10, 11, 12, 13})}}))}});
        const auto result = gmax::frontend::import_yosys_json(json, "top");
        const std::string label = std::string("four-bit ") + test.cell;
        expect(result.ok, label + " imports");
        if (!result.ok) {
            print_diagnostics(result.diagnostics, label);
        }
        expect(result.design.modules.size() == 1 && result.design.modules[0].gates.size() == 4,
               label + " becomes four gates");
        if (result.design.modules.size() == 1) {
            bool saw[4] = {false, false, false, false};
            for (const auto& gate : result.design.modules[0].gates) {
                expect(gate.type == test.gate, label + " lanes keep the gate type");
                for (int lane = 0; lane < 4; ++lane) {
                    if (gate.out == "y[" + std::to_string(lane) + "]") {
                        saw[lane] = true;
                        expect(gate.fanins ==
                                   std::vector<std::string>{"a[" + std::to_string(lane) + "]",
                                                            "b[" + std::to_string(lane) + "]"},
                               label + " lanes read the matching A/B bits");
                    }
                }
            }
            for (const bool lane : saw) {
                expect(lane, label + " drives every y lane");
            }
        }
        expect(result.counts.yosys_cells.size() == 1 &&
                   result.counts.yosys_cells.at(test.cell) == 1,
               label + " is one Yosys cell");
        expect(result.counts.primitives.size() == 1 &&
                   result.counts.primitives.at(test.primitive) == 4,
               label + " becomes four primitives");
    }
}

void test_bus_unary() {
    struct Case {
        const char* cell;
        gmax::db::GateType gate;
        const char* primitive;
    };
    const Case cases[] = {{"$not", gmax::db::GateType::Not, "NOT"},
                          {"$buf", gmax::db::GateType::Buf, "BUF"}};
    for (const Case& test : cases) {
        const std::string json = design_json(
            {{"a", port_json("input", {2, 3, 4, 5})}, {"y", port_json("output", {6, 7, 8, 9})}},
            {{"$cell$1",
              cell_json(test.cell, unary_params(4, 4),
                        object({{"A", bits({2, 3, 4, 5})}, {"Y", bits({6, 7, 8, 9})}}))}});
        const auto result = gmax::frontend::import_yosys_json(json, "top");
        const std::string label = std::string("four-bit ") + test.cell;
        expect(result.ok, label + " imports");
        if (!result.ok) {
            print_diagnostics(result.diagnostics, label);
        }
        expect(result.design.modules.size() == 1 && result.design.modules[0].gates.size() == 4,
               label + " becomes four gates");
        if (result.design.modules.size() == 1) {
            bool saw[4] = {false, false, false, false};
            for (const auto& gate : result.design.modules[0].gates) {
                expect(gate.type == test.gate, label + " lanes keep the gate type");
                for (int lane = 0; lane < 4; ++lane) {
                    if (gate.out == "y[" + std::to_string(lane) + "]") {
                        saw[lane] = true;
                        expect(gate.fanins ==
                                   std::vector<std::string>{"a[" + std::to_string(lane) + "]"},
                               label + " lanes read the matching A bit");
                    }
                }
            }
            for (const bool lane : saw) {
                expect(lane, label + " drives every y lane");
            }
        }
        expect(result.counts.yosys_cells.size() == 1 &&
                   result.counts.yosys_cells.at(test.cell) == 1,
               label + " is one Yosys cell");
        expect(result.counts.primitives.size() == 1 &&
                   result.counts.primitives.at(test.primitive) == 4,
               label + " becomes four primitives");
    }
}

void test_bus_dffe() {
    const std::string json =
        design_json({{"d", port_json("input", {2, 3, 4, 5})},
                     {"clk", port_json("input", {6})},
                     {"en", port_json("input", {7})},
                     {"q", port_json("output", {8, 9, 10, 11})}},
                    {{"$dffe$1", cell_json("$dffe", dffe_params(4, 0, 0),
                                           object({{"D", bits({2, 3, 4, 5})},
                                                   {"CLK", bits({6})},
                                                   {"EN", bits({7})},
                                                   {"Q", bits({8, 9, 10, 11})}}))}});
    const auto result = gmax::frontend::import_yosys_json(json, "top");
    expect(result.ok, "a four-bit $dffe imports");
    if (!result.ok) {
        print_diagnostics(result.diagnostics, "bus dffe");
    }
    std::string enable;
    std::uint32_t inverters = 0;
    if (result.design.modules.size() == 1) {
        for (const auto& gate : result.design.modules[0].gates) {
            if (gate.type == gmax::db::GateType::Not) {
                ++inverters;
                expect(gate.fanins == std::vector<std::string>{"en"},
                       "the active-low enable is inverted from en");
                enable = gate.out;
            }
        }
    }
    expect(inverters == 1, "an active-low $dffe enable adds one inverter");
    expect(result.design.modules.size() == 1 && result.design.modules[0].gates.size() == 5,
           "the four-bit $dffe and its inverter become five gates");
    if (result.design.modules.size() == 1) {
        bool saw[4] = {false, false, false, false};
        for (const auto& gate : result.design.modules[0].gates) {
            if (gate.type != gmax::db::GateType::Dff) {
                continue;
            }
            expect(gate.clock_edge == gmax::db::ClockEdge::Falling,
                   "each $dffe lane keeps the falling clock edge");
            expect(gate.enable == enable, "each $dffe lane shares the inverted enable");
            for (int lane = 0; lane < 4; ++lane) {
                if (gate.out == "q[" + std::to_string(lane) + "]") {
                    saw[lane] = true;
                    expect(gate.fanins ==
                               std::vector<std::string>{"d[" + std::to_string(lane) + "]", "clk",
                                                        enable},
                           "each $dffe lane reads the matching D bit");
                }
            }
        }
        for (const bool lane : saw) {
            expect(lane, "the four-bit $dffe drives every q lane");
        }
    }
    expect(result.counts.yosys_cells.size() == 1 && result.counts.yosys_cells.at("$dffe") == 1,
           "the bus DFFE is one Yosys cell");
    expect(result.counts.primitives.at("DFF") == 4, "the bus DFFE becomes four DFF primitives");
    expect(result.counts.primitives.at("NOT") == 1, "the inverted enable is one NOT primitive");
}

void test_bus_adff() {
    const std::string json =
        design_json({{"d", port_json("input", {2, 3, 4, 5})},
                     {"clk", port_json("input", {6})},
                     {"rst", port_json("input", {7})},
                     {"q", port_json("output", {8, 9, 10, 11})}},
                    {{"$adff$1", cell_json("$adff", adff_params(4, 1, 1, 5),
                                           object({{"D", bits({2, 3, 4, 5})},
                                                   {"CLK", bits({6})},
                                                   {"ARST", bits({7})},
                                                   {"Q", bits({8, 9, 10, 11})}}))}});
    const auto result = gmax::frontend::import_yosys_json(json, "top");
    expect(result.ok, "a four-bit $adff imports");
    if (!result.ok) {
        print_diagnostics(result.diagnostics, "bus adff");
    }
    expect(result.design.modules.size() == 1 && result.design.modules[0].gates.size() == 4,
           "the four-bit $adff becomes four gates");
    if (result.design.modules.size() == 1) {
        bool saw[4] = {false, false, false, false};
        for (const auto& gate : result.design.modules[0].gates) {
            expect(gate.type == gmax::db::GateType::Dff, "each $adff lane is a DFF");
            for (int lane = 0; lane < 4; ++lane) {
                if (gate.out == "q[" + std::to_string(lane) + "]") {
                    saw[lane] = true;
                    const bool reset_one = (lane == 0 || lane == 2);
                    expect(gate.async_clear.empty() == reset_one,
                           "the reset value selects clear/preset per $adff lane");
                    expect(gate.async_preset.empty() == !reset_one,
                           "the reset value selects preset/clear per $adff lane");
                    expect((reset_one ? gate.async_preset : gate.async_clear) == "rst",
                           "each $adff lane resets from rst");
                    expect(gate.fanins ==
                               std::vector<std::string>{"d[" + std::to_string(lane) + "]", "clk",
                                                        "rst"},
                           "each $adff lane reads the matching D bit and reset");
                }
            }
        }
        for (const bool lane : saw) {
            expect(lane, "the four-bit $adff drives every q lane");
        }
    }
    expect(result.counts.yosys_cells.size() == 1 && result.counts.yosys_cells.at("$adff") == 1,
           "the bus ADFF is one Yosys cell");
    expect(result.counts.primitives.size() == 1 && result.counts.primitives.at("DFF") == 4,
           "the bus ADFF becomes four DFF primitives");
}

void test_bus_dffsr() {
    const std::string json =
        design_json({{"d", port_json("input", {2, 3, 4, 5})},
                     {"clk", port_json("input", {6})},
                     {"set", port_json("input", {7, 8, 9, 10})},
                     {"clr", port_json("input", {11, 12, 13, 14})},
                     {"q", port_json("output", {15, 16, 17, 18})}},
                    {{"$dffsr$1", cell_json("$dffsr", dffsr_params(4, 1, 1, 0),
                                            object({{"D", bits({2, 3, 4, 5})},
                                                    {"CLK", bits({6})},
                                                    {"SET", bits({7, 8, 9, 10})},
                                                    {"CLR", bits({11, 12, 13, 14})},
                                                    {"Q", bits({15, 16, 17, 18})}}))}});
    const auto result = gmax::frontend::import_yosys_json(json, "top");
    expect(result.ok, "a four-bit $dffsr imports");
    if (!result.ok) {
        print_diagnostics(result.diagnostics, "bus dffsr");
    }
    std::map<std::string, std::string> inverted;
    std::uint32_t inverters = 0;
    if (result.design.modules.size() == 1) {
        for (const auto& gate : result.design.modules[0].gates) {
            if (gate.type != gmax::db::GateType::Not) {
                continue;
            }
            ++inverters;
            expect(gate.fanins.size() == 1, "each $dffsr inverter has one input");
            if (gate.fanins.size() == 1) {
                inverted[gate.fanins[0]] = gate.out;
            }
        }
    }
    expect(inverters == 4, "the active-low $dffsr CLR bits add four inverters");
    expect(inverted.size() == 4, "each $dffsr CLR lane is inverted once");
    expect(result.design.modules.size() == 1 && result.design.modules[0].gates.size() == 8,
           "the four-bit $dffsr and its inverters become eight gates");
    if (result.design.modules.size() == 1) {
        bool saw[4] = {false, false, false, false};
        for (const auto& gate : result.design.modules[0].gates) {
            if (gate.type != gmax::db::GateType::Dff) {
                continue;
            }
            for (int lane = 0; lane < 4; ++lane) {
                if (gate.out == "q[" + std::to_string(lane) + "]") {
                    saw[lane] = true;
                    const std::string set_net = "set[" + std::to_string(lane) + "]";
                    const auto clear = inverted.find("clr[" + std::to_string(lane) + "]");
                    expect(clear != inverted.end(), "each $dffsr CLR lane has an inverter");
                    const std::string clear_net = clear == inverted.end() ? "" : clear->second;
                    expect(gate.async_clear == clear_net,
                           "each $dffsr lane uses its inverted CLR bit");
                    expect(gate.async_preset == set_net, "each $dffsr lane uses its SET bit");
                    expect(gate.fanins ==
                               std::vector<std::string>{"d[" + std::to_string(lane) + "]", "clk",
                                                        clear_net, set_net},
                           "each $dffsr lane reads the matching D and control bits");
                }
            }
        }
        for (const bool lane : saw) {
            expect(lane, "the four-bit $dffsr drives every q lane");
        }
    }
    expect(result.counts.yosys_cells.size() == 1 && result.counts.yosys_cells.at("$dffsr") == 1,
           "the bus DFFSR is one Yosys cell");
    expect(result.counts.primitives.at("DFF") == 4, "the bus DFFSR becomes four DFF primitives");
    expect(result.counts.primitives.at("NOT") == 4, "the CLR bits are four NOT primitives");
}

void test_bus_dffsr_broadcast() {
    const std::string json =
        design_json({{"d", port_json("input", {2, 3, 4, 5})},
                     {"clk", port_json("input", {6})},
                     {"set", port_json("input", {7})},
                     {"clr", port_json("input", {8})},
                     {"q", port_json("output", {9, 10, 11, 12})}},
                    {{"$dffsr$1", cell_json("$dffsr", dffsr_params(4, 1, 1, 1),
                                            object({{"D", bits({2, 3, 4, 5})},
                                                    {"CLK", bits({6})},
                                                    {"SET", bits({7})},
                                                    {"CLR", bits({8})},
                                                    {"Q", bits({9, 10, 11, 12})}}))}});
    const auto result = gmax::frontend::import_yosys_json(json, "top");
    expect(result.ok, "a four-bit $dffsr accepts single-bit SET and CLR");
    if (!result.ok) {
        print_diagnostics(result.diagnostics, "bus dffsr broadcast");
    }
    expect(result.design.modules.size() == 1 && result.design.modules[0].gates.size() == 4,
           "the broadcast $dffsr becomes four gates");
    if (result.design.modules.size() == 1) {
        bool saw[4] = {false, false, false, false};
        for (const auto& gate : result.design.modules[0].gates) {
            expect(gate.type == gmax::db::GateType::Dff, "each broadcast $dffsr lane is a DFF");
            expect(gate.async_clear == "clr", "each lane broadcasts the shared CLR bit");
            expect(gate.async_preset == "set", "each lane broadcasts the shared SET bit");
            for (int lane = 0; lane < 4; ++lane) {
                if (gate.out == "q[" + std::to_string(lane) + "]") {
                    saw[lane] = true;
                    expect(gate.fanins ==
                               std::vector<std::string>{"d[" + std::to_string(lane) + "]", "clk",
                                                        "clr", "set"},
                           "each broadcast lane reads its D bit and the shared controls");
                }
            }
        }
        for (const bool lane : saw) {
            expect(lane, "the broadcast $dffsr drives every q lane");
        }
    }
    expect(result.counts.yosys_cells.size() == 1 && result.counts.yosys_cells.at("$dffsr") == 1,
           "the broadcast DFFSR is one Yosys cell");
    expect(result.counts.primitives.size() == 1 && result.counts.primitives.at("DFF") == 4,
           "the broadcast DFFSR becomes four DFF primitives and no inverters");
}

void test_bus_dffsr_shared_clear_low() {
    const std::string json =
        design_json({{"d", port_json("input", {2, 3, 4, 5})},
                     {"clk", port_json("input", {6})},
                     {"set", port_json("input", {7})},
                     {"clr", port_json("input", {8})},
                     {"q", port_json("output", {9, 10, 11, 12})}},
                    {{"$dffsr$1", cell_json("$dffsr", dffsr_params(4, 1, 1, 0),
                                            object({{"D", bits({2, 3, 4, 5})},
                                                    {"CLK", bits({6})},
                                                    {"SET", bits({7})},
                                                    {"CLR", bits({8})},
                                                    {"Q", bits({9, 10, 11, 12})}}))}});
    const auto result = gmax::frontend::import_yosys_json(json, "top");
    expect(result.ok, "a four-bit $dffsr accepts a shared active-low CLR");
    if (!result.ok) {
        print_diagnostics(result.diagnostics, "bus dffsr shared clear low");
    }
    std::string inverted_clear;
    std::uint32_t inverters = 0;
    if (result.design.modules.size() == 1) {
        for (const auto& gate : result.design.modules[0].gates) {
            if (gate.type != gmax::db::GateType::Not) {
                continue;
            }
            ++inverters;
            expect(gate.fanins == std::vector<std::string>{"clr"},
                   "the shared active-low CLR is inverted once from clr");
            inverted_clear = gate.out;
        }
    }
    expect(inverters == 1, "the shared active-low CLR adds one inverter");
    expect(result.design.modules.size() == 1 && result.design.modules[0].gates.size() == 5,
           "the shared active-low $dffsr and its inverter become five gates");
    if (result.design.modules.size() == 1) {
        bool saw[4] = {false, false, false, false};
        for (const auto& gate : result.design.modules[0].gates) {
            if (gate.type != gmax::db::GateType::Dff) {
                continue;
            }
            expect(!inverted_clear.empty() && gate.async_clear == inverted_clear,
                   "all four lanes use the one shared inverted clear");
            expect(gate.async_preset == "set", "each lane keeps the shared SET bit");
            for (int lane = 0; lane < 4; ++lane) {
                if (gate.out == "q[" + std::to_string(lane) + "]") {
                    saw[lane] = true;
                    expect(gate.fanins ==
                               std::vector<std::string>{"d[" + std::to_string(lane) + "]", "clk",
                                                        inverted_clear, "set"},
                           "each lane reads its D bit, clock, shared clear and set");
                }
            }
        }
        for (const bool lane : saw) {
            expect(lane, "the shared active-low $dffsr drives every q lane");
        }
    }
    expect(result.counts.yosys_cells.size() == 1 && result.counts.yosys_cells.at("$dffsr") == 1,
           "the active-low broadcast DFFSR is one Yosys cell");
    expect(result.counts.primitives.size() == 2 && result.counts.primitives.at("DFF") == 4 &&
               result.counts.primitives.at("NOT") == 1,
           "the broadcast DFFSR becomes four DFFs and one shared NOT");
}

void test_bus_dlatch() {
    const std::string json =
        design_json({{"d", port_json("input", {2, 3, 4, 5})},
                     {"en", port_json("input", {6})},
                     {"q", port_json("output", {7, 8, 9, 10})}},
                    {{"$dlatch$1", cell_json("$dlatch", dlatch_params(4, 1),
                                             object({{"D", bits({2, 3, 4, 5})},
                                                     {"EN", bits({6})},
                                                     {"Q", bits({7, 8, 9, 10})}}))}});
    const auto result = gmax::frontend::import_yosys_json(json, "top");
    expect(result.ok, "a four-bit $dlatch imports");
    if (!result.ok) {
        print_diagnostics(result.diagnostics, "bus dlatch");
    }
    expect(result.design.modules.size() == 1 && result.design.modules[0].gates.size() == 4,
           "the four-bit $dlatch becomes four gates");
    if (result.design.modules.size() == 1) {
        bool saw[4] = {false, false, false, false};
        for (const auto& gate : result.design.modules[0].gates) {
            expect(gate.type == gmax::db::GateType::Dlat, "each $dlatch lane is a DLAT");
            for (int lane = 0; lane < 4; ++lane) {
                if (gate.out == "q[" + std::to_string(lane) + "]") {
                    saw[lane] = true;
                    expect(gate.fanins ==
                               std::vector<std::string>{"d[" + std::to_string(lane) + "]", "en"},
                           "each $dlatch lane reads the matching D bit and en");
                }
            }
        }
        for (const bool lane : saw) {
            expect(lane, "the four-bit $dlatch drives every q lane");
        }
    }
    expect(result.counts.yosys_cells.size() == 1 && result.counts.yosys_cells.at("$dlatch") == 1,
           "the bus DLATCH is one Yosys cell");
    expect(result.counts.primitives.size() == 1 && result.counts.primitives.at("DLAT") == 4,
           "the bus DLATCH becomes four DLAT primitives");
}

void test_bus_width_mismatches() {
    {
        const std::string json =
            design_json({{"a", port_json("input", {2, 3, 4, 5})},
                         {"b", port_json("input", {6, 7, 8})},
                         {"s", port_json("input", {9})},
                         {"y", port_json("output", {10, 11, 12, 13})}},
                        {{"$mux$1", cell_json("$mux", mux_params(4),
                                              object({{"A", bits({2, 3, 4, 5})},
                                                      {"B", bits({6, 7, 8})},
                                                      {"S", bits({9})},
                                                      {"Y", bits({10, 11, 12, 13})}}))}});
        const auto result = gmax::frontend::import_yosys_json(json, "top");
        expect(!result.ok, "a four-bit $mux with a short B lane fails");
        expect(
            has_width_message(result.diagnostics, "cell '$mux$1' port 'B' is 3 bits, expected 4"),
            "the short $mux lane is a width mismatch");
    }
    {
        const std::string json =
            design_json({{"d", port_json("input", {2, 3, 4, 5})},
                         {"clk", port_json("input", {6})},
                         {"en", port_json("input", {7, 8})},
                         {"q", port_json("output", {9, 10, 11, 12})}},
                        {{"$dffe$1", cell_json("$dffe", dffe_params(4, 1, 1),
                                               object({{"D", bits({2, 3, 4, 5})},
                                                       {"CLK", bits({6})},
                                                       {"EN", bits({7, 8})},
                                                       {"Q", bits({9, 10, 11, 12})}}))}});
        const auto result = gmax::frontend::import_yosys_json(json, "top");
        expect(!result.ok, "a four-bit $dffe with a two-bit enable fails");
        expect(
            has_width_message(result.diagnostics, "cell '$dffe$1' port 'EN' is 2 bits, expected 1"),
            "the malformed $dffe enable is a width mismatch");
    }
    {
        const std::string json =
            design_json({{"d", port_json("input", {2, 3, 4, 5})},
                         {"clk", port_json("input", {6})},
                         {"set", port_json("input", {7, 8})},
                         {"clr", port_json("input", {9, 10, 11, 12})},
                         {"q", port_json("output", {13, 14, 15, 16})}},
                        {{"$dffsr$1", cell_json("$dffsr", dffsr_params(4, 1, 1, 1),
                                                object({{"D", bits({2, 3, 4, 5})},
                                                        {"CLK", bits({6})},
                                                        {"SET", bits({7, 8})},
                                                        {"CLR", bits({9, 10, 11, 12})},
                                                        {"Q", bits({13, 14, 15, 16})}}))}});
        const auto result = gmax::frontend::import_yosys_json(json, "top");
        expect(!result.ok, "a four-bit $dffsr with a two-bit SET bus fails");
        expect(has_width_message(result.diagnostics,
                                 "cell '$dffsr$1' port 'SET' is 2 bits, expected 4 or 1"),
               "the malformed $dffsr set bus is a width mismatch");
    }
    {
        const std::string json =
            design_json({{"d", port_json("input", {2, 3, 4, 5})},
                         {"clk", port_json("input", {6})},
                         {"set", port_json("input", {7, 8, 9, 10})},
                         {"clr", port_json("input", {11, 12})},
                         {"q", port_json("output", {13, 14, 15, 16})}},
                        {{"$dffsr$1", cell_json("$dffsr", dffsr_params(4, 1, 1, 1),
                                                object({{"D", bits({2, 3, 4, 5})},
                                                        {"CLK", bits({6})},
                                                        {"SET", bits({7, 8, 9, 10})},
                                                        {"CLR", bits({11, 12})},
                                                        {"Q", bits({13, 14, 15, 16})}}))}});
        const auto result = gmax::frontend::import_yosys_json(json, "top");
        expect(!result.ok, "a four-bit $dffsr with a two-bit CLR bus fails");
        expect(has_width_message(result.diagnostics,
                                 "cell '$dffsr$1' port 'CLR' is 2 bits, expected 4 or 1"),
               "the malformed $dffsr clear bus is a width mismatch");
    }
}

void test_library_instance() {
    gmax::db::Design library;
    gmax::db::Module and2;
    and2.name = "AND2";
    and2.inputs = {"A", "B"};
    and2.outputs = {"Y"};
    gmax::db::Gate gate;
    gate.type = gmax::db::GateType::And2;
    gate.name = "g1";
    gate.fanins = {"A", "B"};
    gate.out = "Y";
    and2.gates.push_back(gate);
    library.modules.push_back(and2);

    const std::string instance =
        object({{"type", quote("AND2")},
                {"port_directions",
                 object({{"A", quote("input")}, {"B", quote("input")}, {"Y", quote("output")}})},
                {"connections", object({{"A", bits({2})}, {"B", bits({3})}, {"Y", bits({4})}})}});
    const std::string json = design_json({{"a", port_json("input", {2})},
                                          {"b", port_json("input", {3})},
                                          {"y", port_json("output", {4})}},
                                         {{"u1", instance}});

    const auto result = gmax::frontend::import_yosys_json(json, "top", library, {{"AND2", 1}});
    expect(result.ok, "a library instance imports and reconciles");
    if (!result.ok) {
        print_diagnostics(result.diagnostics, "library");
    }
    expect(result.design.instances.size() == 1, "the library instance is present");
    if (result.design.instances.size() == 1) {
        expect(result.design.instances[0].module == "AND2", "the instance names its module");
        expect(result.design.instances[0].connections.at("A") == "a" &&
                   result.design.instances[0].connections.at("Y") == "y",
               "the instance connections bind to top nets");
    }
    expect(result.counts.primitives.at("AND2") == 1,
           "flattening the library instance keeps its AND2 primitive");
}

void test_multiple_drivers() {
    const std::string json = design_json(
        {{"a", port_json("input", {2})}, {"y", port_json("output", {4})}},
        {{"$not$1", cell_json("$not", unary_params(1, 1),
                              object({{"A", bits({2})}, {"Y", bits({2})}}), "top.v:3.1")}});
    const auto result = gmax::frontend::import_yosys_json(json, "top");
    expect(!result.ok, "a net with an input and a gate driver fails");
    expect(has_code(result.diagnostics, gmax::frontend::DiagnosticCode::MultipleDrivers),
           "multiple drivers are reported structurally");
}

void test_undriven_input() {
    const std::string json =
        design_json({{"a", port_json("input", {2})}, {"y", port_json("output", {4})}},
                    {{"$not$1", cell_json("$not", unary_params(1, 1),
                                          object({{"A", bits({3})}, {"Y", bits({4})}}))}});
    const auto result = gmax::frontend::import_yosys_json(json, "top");
    expect(!result.ok, "a gate input with no driver fails");
    expect(has_code(result.diagnostics, gmax::frontend::DiagnosticCode::UndrivenInput),
           "an undriven input is reported structurally");
}

void test_width_mismatch() {
    const std::string json = design_json(
        {{"a", port_json("input", {2})},
         {"b", port_json("input", {3})},
         {"y", port_json("output", {4})}},
        {{"$and$1",
          cell_json("$and", width_params(4, 1, 4),
                    object({{"A", bits({2})}, {"B", bits({3})}, {"Y", bits({4})}}), kSource)}});
    const auto result = gmax::frontend::import_yosys_json(json, "top");
    expect(!result.ok, "an inconsistent-width primitive is rejected");
    bool width = false;
    for (const auto& diagnostic : result.diagnostics) {
        if (diagnostic.code == gmax::frontend::DiagnosticCode::WidthMismatch) {
            width = true;
            expect(diagnostic.source.file == "top.v" && diagnostic.source.line == 7 &&
                       diagnostic.source.column == 2,
                   "the width diagnostic carries the Yosys src location");
        }
    }
    expect(width, "a width mismatch is reported structurally");
}

void test_unmapped_cell() {
    const std::string json = design_json(
        {{"a", port_json("input", {2})},
         {"b", port_json("input", {3})},
         {"y", port_json("output", {4})}},
        {{"$add$1", cell_json("$add", width_params(1, 1, 1),
                              object({{"A", bits({2})}, {"B", bits({3})}, {"Y", bits({4})}}))}});
    const auto result = gmax::frontend::import_yosys_json(json, "top");
    expect(!result.ok, "an unsupported Yosys cell fails");
    expect(has_code(result.diagnostics, gmax::frontend::DiagnosticCode::UnmappedCell),
           "an unsupported cell is reported structurally");
}

void test_unresolved_module() {
    const std::string json = design_json(
        {{"a", port_json("input", {2})}, {"y", port_json("output", {4})}},
        {{"u1", cell_json("missing", "", object({{"A", bits({2})}, {"Y", bits({4})}}))}});
    const auto result = gmax::frontend::import_yosys_json(json, "top");
    expect(!result.ok, "a cell whose module is absent fails");
    expect(has_code(result.diagnostics, gmax::frontend::DiagnosticCode::UnresolvedModule),
           "an unresolved module is reported structurally");
}

void test_loop() {
    const std::string json =
        design_json({{"a", port_json("input", {2})}, {"y", port_json("output", {4})}},
                    {{"$not$1", cell_json("$not", unary_params(1, 1),
                                          object({{"A", bits({4})}, {"Y", bits({4})}}))}});
    const auto result = gmax::frontend::import_yosys_json(json, "top");
    expect(!result.ok, "a self-driving gate fails");
    expect(has_code(result.diagnostics, gmax::frontend::DiagnosticCode::CombinationalLoop),
           "a combinational loop is reported structurally");
}

void test_cell_count_mismatch() {
    const auto result = gmax::frontend::import_yosys_json(smoke_design(), "top", {}, {{"$and", 2}});
    expect(!result.ok, "a cell-count mismatch fails");
    expect(has_code(result.diagnostics, gmax::frontend::DiagnosticCode::CellCountMismatch),
           "the mismatch is reported structurally");
    expect(result.counts.deltas.size() == 1, "the report carries one delta");
    if (result.counts.deltas.size() == 1) {
        expect(result.counts.deltas[0].type == "$and" && result.counts.deltas[0].expected == 2 &&
                   result.counts.deltas[0].actual == 1,
               "the delta names the type and both counts");
    }
}

void test_invalid_json() {
    const auto result = gmax::frontend::import_yosys_json("{", "top");
    expect(!result.ok, "invalid JSON fails");
    expect(has_code(result.diagnostics, gmax::frontend::DiagnosticCode::InvalidJson),
           "invalid JSON is reported structurally");
}

void test_recipe() {
    gmax::frontend::YosysOptions options;
    options.libraries = {"/tmp/lib one.v"};
    options.netlist = "/tmp/net list.v";
    options.top = "top";
    options.techmap = true;
    const std::string script =
        gmax::frontend::yosys_recipe_script(options, "/tmp/out dir/design.json");
    expect(script.find("read_verilog -sv -lib \"/tmp/lib one.v\"") != std::string::npos,
           "the recipe quotes library paths");
    expect(script.find("read_verilog -sv \"/tmp/net list.v\"") != std::string::npos,
           "the recipe quotes the netlist path");
    expect(script.find("hierarchy -check -top top") != std::string::npos,
           "the recipe checks the top module");
    expect(script.find("flatten\nopt_clean\ntechmap\nwrite_json \"/tmp/out dir/design.json\"") !=
               std::string::npos,
           "the recipe runs the pinned phase 0-1 passes in order");
    expect(script.find("proc") == std::string::npos,
           "the default recipe leaves behavioral processes to the caller");

    options.proc = true;
    const std::string process_script =
        gmax::frontend::yosys_recipe_script(options, "/tmp/out dir/design.json");
    expect(process_script.find("flatten\nproc\nopt_dff\nopt_clean\ntechmap\nwrite_json "
                               "\"/tmp/out dir/design.json\"") != std::string::npos,
           "the process recipe elaborates behavior before cleaning");
}

std::string find_yosys() {
    if (const char* environment = std::getenv("GMAX_YOSYS");
        environment != nullptr && *environment != '\0' && std::filesystem::exists(environment)) {
        return environment;
    }
    if (const char* home = std::getenv("HOME"); home != nullptr && *home != '\0') {
        const std::filesystem::path cached =
            std::filesystem::path(home) / ".cache/gmax/yosys/0.71/bin/yosys";
        if (std::filesystem::exists(cached)) {
            return cached.string();
        }
    }
    return {};
}

void test_live_smoke(const std::string& yosys) {
    gmax::frontend::YosysOptions options;
    options.yosys = yosys;
    options.netlist =
        (std::filesystem::path(GMAX_SOURCE_DIR) / "tests/differential/yosys/smoke.v").string();
    options.top = "smoke";
    const auto result = gmax::frontend::elaborate_with_yosys(options);
    expect(result.ok, "the pinned Yosys elaborates the smoke netlist");
    if (!result.ok) {
        print_diagnostics(result.diagnostics, "live smoke");
        std::cerr << result.log << std::endl;
    }
    expect(result.exit_code == 0, "the smoke Yosys process exits zero");
    expect(!result.counts.yosys_cells.empty(), "the smoke JSON has cells");
    expect(!result.counts.primitives.empty(), "the smoke design has primitives");
}

void test_live_bus_and(const std::string& yosys) {
    std::error_code error;
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path(error) / "gmax-yosys-bus-test";
    std::filesystem::remove_all(directory, error);
    std::filesystem::create_directories(directory, error);
    const std::filesystem::path netlist = directory / "bus.v";
    {
        std::ofstream out(netlist);
        out << "module bus(input [3:0] a, input [3:0] b, output [3:0] y);\n"
               "  assign y = a & b;\n"
               "endmodule\n";
    }

    gmax::frontend::YosysOptions options;
    options.yosys = yosys;
    options.netlist = netlist.string();
    options.top = "bus";
    const auto result = gmax::frontend::elaborate_with_yosys(options);
    expect(result.ok, "the pinned Yosys elaborates a four-bit $and netlist");
    if (!result.ok) {
        print_diagnostics(result.diagnostics, "live bus and");
        std::cerr << result.log << std::endl;
    }
    expect(result.counts.yosys_cells.size() == 1 && result.counts.yosys_cells.at("$and") == 1,
           "the live bus $and is one Yosys cell");
    expect(result.counts.primitives.size() == 1 && result.counts.primitives.at("AND2") == 4,
           "the live bus $and becomes four AND2 primitives");
    std::filesystem::remove_all(directory, error);
}

void test_live_bus_mux(const std::string& yosys) {
    std::error_code error;
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path(error) / "gmax-yosys-bus-mux-test";
    std::filesystem::remove_all(directory, error);
    std::filesystem::create_directories(directory, error);
    const std::filesystem::path netlist = directory / "bus_mux.v";
    {
        std::ofstream out(netlist);
        out << "module bus_mux(input [3:0] a, input [3:0] b, input s, output [3:0] y);\n"
               "  assign y = s ? b : a;\n"
               "endmodule\n";
    }

    gmax::frontend::YosysOptions options;
    options.yosys = yosys;
    options.netlist = netlist.string();
    options.top = "bus_mux";
    const auto result = gmax::frontend::elaborate_with_yosys(options);
    expect(result.ok, "the pinned Yosys elaborates a four-bit $mux netlist");
    if (!result.ok) {
        print_diagnostics(result.diagnostics, "live bus mux");
        std::cerr << result.log << std::endl;
    }
    expect(result.counts.yosys_cells.size() == 1 && result.counts.yosys_cells.at("$mux") == 1,
           "the live bus $mux is one Yosys cell");
    expect(result.counts.primitives.size() == 1 && result.counts.primitives.at("MUX2") == 4,
           "the live bus $mux becomes four MUX2 primitives");
    std::filesystem::remove_all(directory, error);
}

void test_live_bus_flops(const std::string& yosys) {
    std::error_code error;
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path(error) / "gmax-yosys-bus-flops-test";
    std::filesystem::remove_all(directory, error);
    std::filesystem::create_directories(directory, error);
    const std::filesystem::path netlist = directory / "bus_flops.v";
    {
        std::ofstream out(netlist);
        out << "module bus_flops(\n"
               "  input clk, input [3:0] d, input en, input rst, input set, input clr,\n"
               "  output reg [3:0] q_dff, output reg [3:0] q_dffe, output reg [3:0] q_adff,\n"
               "  output reg [3:0] q_dffsr, output reg [3:0] q_dlatch\n"
               ");\n"
               "  always @(posedge clk) q_dff <= d;\n"
               "  always @(posedge clk) if (en) q_dffe <= d;\n"
               "  always @(posedge clk or posedge rst) if (rst) q_adff <= 4'h0; else q_adff <= d;\n"
               "  always @(posedge clk or posedge set or negedge clr)\n"
               "    if (set) q_dffsr <= 4'hf; else if (!clr) q_dffsr <= 4'h0; else q_dffsr <= d;\n"
               "  always @* if (en) q_dlatch = d;\n"
               "endmodule\n";
    }

    gmax::frontend::YosysOptions options;
    options.yosys = yosys;
    options.netlist = netlist.string();
    options.top = "bus_flops";
    options.proc = true;
    const auto result = gmax::frontend::elaborate_with_yosys(options);
    expect(result.ok, "the pinned Yosys elaborates a four-bit bus-flop netlist");
    if (!result.ok) {
        print_diagnostics(result.diagnostics, "live bus flops");
        std::cerr << result.log << std::endl;
    }

    const auto count_of = [](const std::map<std::string, std::uint64_t>& counts,
                             const std::string& type) {
        const auto it = counts.find(type);
        return it == counts.end() ? std::uint64_t{0} : it->second;
    };
    for (const char* cell : {"$dff", "$dffe", "$adff", "$dffsr", "$dlatch"}) {
        expect(count_of(result.counts.yosys_cells, cell) == 1,
               std::string("the live bus flops use one ") + cell + " cell");
    }
    expect(count_of(result.counts.primitives, "DFF") == 16,
           "the four live bus flops become sixteen DFF primitives");
    expect(count_of(result.counts.primitives, "DLAT") == 4,
           "the live bus latch becomes four DLAT primitives");

    expect(result.design.modules.size() == 1, "the live bus-flop design has one module");
    if (result.design.modules.size() == 1) {
        const auto& gates = result.design.modules[0].gates;
        const auto find_gate = [&gates](const std::string& out) -> const gmax::db::Gate* {
            for (const auto& gate : gates) {
                if (gate.out == out) {
                    return &gate;
                }
            }
            return nullptr;
        };
        const auto check =
            [&find_gate](const std::string& output, const gmax::db::GateType type,
                         const std::vector<std::string>& fanins) -> const gmax::db::Gate* {
            const gmax::db::Gate* gate = find_gate(output);
            expect(gate != nullptr && gate->type == type, output + " keeps its primitive type");
            if (gate != nullptr) {
                expect(gate->fanins == fanins, output + " reads the expected fanins");
            }
            return gate;
        };

        for (int lane = 0; lane < 4; ++lane) {
            const std::string index = "[" + std::to_string(lane) + "]";
            const std::string data = "d" + index;
            check("q_dff" + index, gmax::db::GateType::Dff, {data, "clk"});
            if (const gmax::db::Gate* dffe =
                    check("q_dffe" + index, gmax::db::GateType::Dff, {data, "clk", "en"});
                dffe != nullptr) {
                expect(dffe->enable == "en", "each live $dffe lane holds on en");
            }
            if (const gmax::db::Gate* adff =
                    check("q_adff" + index, gmax::db::GateType::Dff, {data, "clk", "rst"});
                adff != nullptr) {
                expect(adff->async_clear == "rst" && adff->async_preset.empty(),
                       "each live $adff lane resets from rst");
            }
            const gmax::db::Gate* dffsr = find_gate("q_dffsr" + index);
            expect(dffsr != nullptr && dffsr->type == gmax::db::GateType::Dff,
                   "each live $dffsr lane is a DFF");
            if (dffsr != nullptr) {
                expect(dffsr->fanins.size() == 4 && dffsr->fanins[0] == data &&
                           dffsr->fanins[1] == "clk",
                       "each live $dffsr lane reads the matching D and clock");
                expect(!dffsr->async_clear.empty() && !dffsr->async_preset.empty(),
                       "each live $dffsr lane has clear and preset controls");
                if (dffsr->fanins.size() == 4) {
                    expect(dffsr->fanins[2] == dffsr->async_clear &&
                               dffsr->fanins[3] == dffsr->async_preset,
                           "each live $dffsr lane wires its clear and preset controls");
                }
            }
            check("q_dlatch" + index, gmax::db::GateType::Dlat, {data, "en"});
        }
    }
    std::filesystem::remove_all(directory, error);
}

void test_live_unresolved(const std::string& yosys) {
    std::error_code error;
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path(error) / "gmax-yosys-unresolved-test";
    std::filesystem::remove_all(directory, error);
    std::filesystem::create_directories(directory, error);
    const std::filesystem::path netlist = directory / "top.v";
    {
        std::ofstream out(netlist);
        out << "module top(input a, output y);\n"
               "  missing u(.a(a), .y(y));\n"
               "endmodule\n";
    }

    gmax::frontend::YosysOptions options;
    options.yosys = yosys;
    options.netlist = netlist.string();
    options.top = "top";
    const auto result = gmax::frontend::elaborate_with_yosys(options);
    expect(!result.ok, "Yosys reports the unresolved module");
    expect(result.exit_code != 0, "the unresolved module exits nonzero");
    expect(has_code(result.diagnostics, gmax::frontend::DiagnosticCode::UnresolvedModule),
           "the unresolved module is a structured diagnostic");
    std::filesystem::remove_all(directory, error);
}

}  // namespace

int main() {
    test_smoke();
    test_source_name_beats_generated_alias();
    test_mux();
    test_bus_and();
    test_dff();
    test_bus_dff();
    test_bus_mux();
    test_bus_bitwise();
    test_bus_unary();
    test_bus_dffe();
    test_bus_adff();
    test_bus_dffsr();
    test_bus_dffsr_broadcast();
    test_bus_dffsr_shared_clear_low();
    test_bus_dlatch();
    test_bus_width_mismatches();
    test_library_instance();
    test_multiple_drivers();
    test_undriven_input();
    test_width_mismatch();
    test_unmapped_cell();
    test_unresolved_module();
    test_loop();
    test_cell_count_mismatch();
    test_invalid_json();
    test_recipe();

    const std::string yosys = find_yosys();
    if (yosys.empty()) {
        std::cout << "skip: pinned Yosys not found" << std::endl;
    } else {
        test_live_smoke(yosys);
        test_live_bus_and(yosys);
        test_live_bus_mux(yosys);
        test_live_bus_flops(yosys);
        test_live_unresolved(yosys);
    }

    if (failures != 0) {
        std::cerr << failures << " expectation(s) failed" << std::endl;
        return EXIT_FAILURE;
    }
    std::cout << "yosys importer checks passed" << std::endl;
    return EXIT_SUCCESS;
}
