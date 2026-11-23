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
    expect(!result.ok, "a multi-bit primitive is rejected");
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
    test_mux();
    test_dff();
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
        test_live_unresolved(yosys);
    }

    if (failures != 0) {
        std::cerr << failures << " expectation(s) failed" << std::endl;
        return EXIT_FAILURE;
    }
    std::cout << "yosys importer checks passed" << std::endl;
    return EXIT_SUCCESS;
}
