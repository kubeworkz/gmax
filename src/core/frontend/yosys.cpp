#include "core/frontend/yosys.h"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include "core/db/db.h"
#include "core/frontend/json.h"

namespace gmax::frontend {
namespace {

// Yosys marks the names it generates with a leading '$'. Those aliases are
// stable only for a given Yosys build, while a name that came from the Verilog
// source is what a constraint or fault list can address.
bool generated_name(const std::string_view name) {
    return !name.empty() && name.front() == '$';
}

constexpr std::uint32_t kNoNet = std::numeric_limits<std::uint32_t>::max();

// Yosys writes "file.v:line.column-line.column" (or a shorter range). The file
// may contain a drive colon, so split at the last one and parse digits after it.
SourceLocation parse_source_text(const std::string_view text) {
    if (text.empty()) {
        return {};
    }
    const std::size_t last = text.rfind(':');
    if (last == std::string::npos) {
        return SourceLocation{std::string(text), 0, 0};
    }
    SourceLocation location;
    location.file = std::string(text.substr(0, last));
    const std::string rest(text.substr(last + 1));
    std::size_t pos = 0;
    std::uint32_t line = 0;
    bool digits = false;
    while (pos < rest.size() && rest[pos] >= '0' && rest[pos] <= '9') {
        digits = true;
        line = line * 10 + static_cast<std::uint32_t>(rest[pos] - '0');
        ++pos;
    }
    if (!digits) {
        location.file = std::string(text);
        return location;
    }
    location.line = line;
    if (pos < rest.size() && rest[pos] == '.') {
        ++pos;
        std::uint32_t column = 0;
        while (pos < rest.size() && rest[pos] >= '0' && rest[pos] <= '9') {
            column = column * 10 + static_cast<std::uint32_t>(rest[pos] - '0');
            ++pos;
        }
        location.column = column;
    }
    return location;
}

SourceLocation source_from(const JsonValue& object) {
    const JsonValue* attributes = object.find("attributes");
    if (attributes == nullptr || !attributes->is_object()) {
        return {};
    }
    const JsonValue* source = attributes->find("src");
    return source != nullptr && source->is_string() ? parse_source_text(source->text)
                                                    : SourceLocation{};
}

std::string json_string(const JsonValue* value) {
    return value != nullptr && value->is_string() ? value->text : std::string();
}

bool json_bool(const JsonValue* value) {
    if (value == nullptr) {
        return false;
    }
    if (value->kind == JsonValue::Kind::Bool) {
        return value->boolean;
    }
    return value->is_number() && value->number != 0.0;
}

bool parameter_u32(const JsonValue& cell, const std::string_view name, std::uint32_t& out) {
    const JsonValue* parameters = cell.find("parameters");
    if (parameters == nullptr || !parameters->is_object()) {
        return false;
    }
    const JsonValue* value = parameters->find(name);
    if (value == nullptr) {
        return false;
    }
    if (value->is_number()) {
        if (value->number < 0.0 || value->number > static_cast<double>(kNoNet) ||
            std::trunc(value->number) != value->number) {
            return false;
        }
        out = static_cast<std::uint32_t>(value->number);
        return true;
    }
    if (!value->is_string() || value->text.empty()) {
        return false;
    }
    bool binary = true;
    for (const char c : value->text) {
        if (c != '0' && c != '1') {
            binary = false;
            break;
        }
    }
    if (binary) {
        if (value->text.size() > 32) {
            return false;
        }
        std::uint32_t parsed = 0;
        for (const char c : value->text) {
            parsed = (parsed << 1) | static_cast<std::uint32_t>(c == '1');
        }
        out = parsed;
        return true;
    }
    errno = 0;
    char* end = nullptr;
    const unsigned long long parsed = std::strtoull(value->text.c_str(), &end, 10);
    if (errno == ERANGE || end == value->text.c_str() || *end != '\0' || parsed > kNoNet) {
        return false;
    }
    out = static_cast<std::uint32_t>(parsed);
    return true;
}

bool parameter_u64(const JsonValue& cell, const std::string_view name, std::uint64_t& out) {
    const JsonValue* parameters = cell.find("parameters");
    if (parameters == nullptr || !parameters->is_object()) {
        return false;
    }
    const JsonValue* value = parameters->find(name);
    if (value == nullptr) {
        return false;
    }
    if (value->is_number()) {
        constexpr double kMaxU64 = 18446744073709551615.0;
        if (value->number < 0.0 || value->number > kMaxU64 ||
            std::trunc(value->number) != value->number) {
            return false;
        }
        out = static_cast<std::uint64_t>(value->number);
        return true;
    }
    if (!value->is_string() || value->text.empty()) {
        return false;
    }
    bool binary = true;
    for (const char c : value->text) {
        if (c != '0' && c != '1') {
            binary = false;
            break;
        }
    }
    if (binary) {
        if (value->text.size() > 64) {
            return false;
        }
        std::uint64_t parsed = 0;
        for (const char c : value->text) {
            parsed = (parsed << 1) | static_cast<std::uint64_t>(c == '1');
        }
        out = parsed;
        return true;
    }
    errno = 0;
    char* end = nullptr;
    const unsigned long long parsed = std::strtoull(value->text.c_str(), &end, 10);
    if (errno == ERANGE || end == value->text.c_str() || *end != '\0') {
        return false;
    }
    out = static_cast<std::uint64_t>(parsed);
    return true;
}

struct BitRef {
    enum class Kind : std::uint8_t { Net, Zero, One, X, Z };
    Kind kind = Kind::Net;
    std::uint32_t net = 0;
};

bool parse_bit(const JsonValue& value, BitRef& out) {
    if (value.is_number()) {
        if (value.number < 0.0 || value.number > static_cast<double>(kNoNet) ||
            std::trunc(value.number) != value.number) {
            return false;
        }
        const auto id = static_cast<std::uint32_t>(value.number);
        if (id == 0) {
            out = {BitRef::Kind::Zero, 0};
        } else if (id == 1) {
            out = {BitRef::Kind::One, 0};
        } else {
            out = {BitRef::Kind::Net, id};
        }
        return true;
    }
    if (!value.is_string()) {
        return false;
    }
    if (value.text == "0") {
        out = {BitRef::Kind::Zero, 0};
    } else if (value.text == "1") {
        out = {BitRef::Kind::One, 0};
    } else if (value.text == "x" || value.text == "X") {
        out = {BitRef::Kind::X, 0};
    } else if (value.text == "z" || value.text == "Z") {
        out = {BitRef::Kind::Z, 0};
    } else {
        return false;
    }
    return true;
}

bool contains(const std::vector<std::string>& names, const std::string_view name) {
    return std::find(names.begin(), names.end(), name) != names.end();
}

class Importer {
  public:
    Importer(const JsonValue& root, const std::string_view top, const db::Design& library_design,
             const std::map<std::string, std::uint64_t>& expected)
        : root_(root), top_name_(top), base_(library_design), expected_(expected) {
        result_.design = base_;
        result_.counts.expected_cells = expected_;
    }

    ImportResult run() {
        if (top_name_.empty()) {
            add(DiagnosticCode::InvalidTop, DiagnosticSeverity::Error,
                "the top module name is empty", {}, {});
            return finish();
        }
        if (!root_.is_object()) {
            add(DiagnosticCode::InvalidJson, DiagnosticSeverity::Error,
                "the JSON root is not an object", {}, {});
            return finish();
        }
        const JsonValue* modules = root_.find("modules");
        if (modules == nullptr || !modules->is_object()) {
            add(DiagnosticCode::InvalidJson, DiagnosticSeverity::Error,
                "the JSON has no modules object", {}, {});
            return finish();
        }
        const JsonValue* top = modules->find(top_name_);
        if (top == nullptr || !top->is_object()) {
            add(DiagnosticCode::InvalidTop, DiagnosticSeverity::Error,
                "top module '" + top_name_ + "' is not in the JSON", top_name_, {});
            return finish();
        }
        if (db::find_module(result_.design, top_name_) != nullptr) {
            add(DiagnosticCode::InvalidTop, DiagnosticSeverity::Error,
                "the library design already defines module '" + top_name_ + "'", top_name_, {});
            return finish();
        }

        module_source_ = source_from(*top);
        top_module_.name = top_name_;
        index_ports(*top);
        index_netnames(*top);
        finalize_names();
        populate_ports();
        map_cells(*top);
        validate_undriven();
        flatten_and_finish();
        return finish();
    }

  private:
    struct PortBit {
        std::string name;
        BitRef bit;
        std::string direction;
    };

    struct NameChoice {
        int priority = 0;
        std::string name;
    };

    struct Driver {
        std::string label;
        SourceLocation source;
        bool primary = false;
    };

    struct FaninUse {
        std::uint32_t net = 0;
        std::string object;
        SourceLocation source;
    };

    void add(const DiagnosticCode code, const DiagnosticSeverity severity, std::string message,
             std::string object, const SourceLocation& source) {
        for (const Diagnostic& existing : result_.diagnostics) {
            if (existing.code == code && existing.object == object && existing.message == message) {
                return;
            }
        }
        Diagnostic diagnostic;
        diagnostic.code = code;
        diagnostic.severity = severity;
        diagnostic.message = std::move(message);
        diagnostic.object = std::move(object);
        diagnostic.source = source;
        result_.diagnostics.push_back(std::move(diagnostic));
    }

    void error(const DiagnosticCode code, std::string message, std::string object,
               const SourceLocation& source) {
        add(code, DiagnosticSeverity::Error, std::move(message), std::move(object), source);
    }

    bool has_code(const DiagnosticCode code) const {
        return std::any_of(
            result_.diagnostics.begin(), result_.diagnostics.end(),
            [code](const Diagnostic& diagnostic) { return diagnostic.code == code; });
    }

    void offer_name(const std::uint32_t net, std::string name, const int priority) {
        if (name.empty()) {
            return;
        }
        // One net can be known by several names: a port, a source wire and a
        // Yosys-generated alias such as `$flatten$...`. Only the source name can
        // be written in an SDC file or a fault list, and the generated aliases
        // change with the Yosys build, so a source name outranks one at the same
        // priority. Within a kind the smallest name keeps the choice
        // deterministic.
        const auto rank = [](const int priority, const std::string& name) {
            return std::tuple{priority, generated_name(name), std::string_view(name)};
        };
        const auto it = bit_names_.find(net);
        if (it == bit_names_.end() ||
            rank(priority, name) < rank(it->second.priority, it->second.name)) {
            bit_names_[net] = NameChoice{priority, std::move(name)};
        }
    }

    void index_ports(const JsonValue& module) {
        const JsonValue* ports = module.find("ports");
        if (ports == nullptr || !ports->is_object()) {
            return;
        }
        for (const auto& [port_name, port] : ports->object) {
            if (!port->is_object()) {
                error(DiagnosticCode::InvalidJson, "port '" + port_name + "' is not an object",
                      port_name, module_source_);
                continue;
            }
            const std::string direction = json_string(port->find("direction"));
            const JsonValue* bits = port->find("bits");
            if (!bits || !bits->is_array() || bits->array.empty()) {
                error(DiagnosticCode::InvalidJson, "port '" + port_name + "' has no bits",
                      port_name, module_source_);
                continue;
            }
            for (std::size_t i = 0; i < bits->array.size(); ++i) {
                BitRef bit;
                if (!parse_bit(*bits->array[i], bit)) {
                    error(DiagnosticCode::InvalidJson,
                          "port '" + port_name + "' has an invalid bit", port_name, module_source_);
                    continue;
                }
                std::string name =
                    bits->array.size() == 1 ? port_name : port_name + "[" + std::to_string(i) + "]";
                if (bit.kind == BitRef::Kind::Net) {
                    offer_name(bit.net, std::move(name), 0);
                }
                port_bits_.push_back(PortBit{name, bit, direction});
            }
        }
    }

    void index_netnames(const JsonValue& module) {
        const JsonValue* netnames = module.find("netnames");
        if (netnames == nullptr || !netnames->is_object()) {
            return;
        }
        for (const auto& [name, entry] : netnames->object) {
            if (!entry->is_object()) {
                continue;
            }
            const JsonValue* bits = entry->find("bits");
            if (bits == nullptr || !bits->is_array()) {
                continue;
            }
            const bool hidden = json_bool(entry->find("hide_name"));
            const SourceLocation source = source_from(*entry);
            for (std::size_t i = 0; i < bits->array.size(); ++i) {
                BitRef bit;
                if (!parse_bit(*bits->array[i], bit) || bit.kind != BitRef::Kind::Net) {
                    continue;
                }
                const std::string candidate =
                    bits->array.size() == 1 ? name : name + "[" + std::to_string(i) + "]";
                offer_name(bit.net, candidate, hidden ? 2 : 1);
                if (!source.empty()) {
                    net_sources_.emplace(bit.net, source);
                }
            }
        }
    }

    void finalize_names() {
        for (const auto& [net, choice] : bit_names_) {
            std::string name = choice.name.empty() ? "$net" + std::to_string(net) : choice.name;
            if (!used_names_.insert(name).second) {
                name += "$" + std::to_string(net);
                used_names_.insert(name);
            }
            names_[net] = std::move(name);
        }
    }

    std::string name_for_net(const std::uint32_t net) {
        const auto it = names_.find(net);
        if (it != names_.end()) {
            return it->second;
        }
        std::string name = "$net" + std::to_string(net);
        while (!used_names_.insert(name).second) {
            name += "$";
        }
        names_[net] = name;
        return name;
    }

    std::string unique_name(const std::string_view base) {
        std::string name(base);
        if (used_names_.insert(name).second) {
            return name;
        }
        for (std::uint32_t i = 1;; ++i) {
            std::string candidate = std::string(base) + "$" + std::to_string(i);
            if (used_names_.insert(candidate).second) {
                return candidate;
            }
        }
    }

    SourceLocation net_source(const std::uint32_t net) const {
        const auto it = net_sources_.find(net);
        return it == net_sources_.end() ? module_source_ : it->second;
    }

    void add_driver(const std::uint32_t net, std::string label, const SourceLocation& source,
                    const bool primary) {
        const auto it = drivers_.find(net);
        if (it == drivers_.end()) {
            drivers_.emplace(net, Driver{std::move(label), source, primary});
            return;
        }
        if (it->second.primary && primary) {
            return;
        }
        error(DiagnosticCode::MultipleDrivers,
              "net '" + name_for_net(net) + "' has multiple drivers: " + it->second.label +
                  " and " + label,
              name_for_net(net), source);
    }

    std::string tie(const bool one) {
        std::string& cached = one ? tie_one_ : tie_zero_;
        if (!cached.empty()) {
            return cached;
        }
        cached = unique_name(one ? "$const1" : "$const0");
        add_gate(one ? db::GateType::Tie1 : db::GateType::Tie0, {}, cached);
        return cached;
    }

    std::string fanin(const BitRef bit, const std::string& object, const SourceLocation& source) {
        switch (bit.kind) {
        case BitRef::Kind::Net:
            fanins_.push_back(FaninUse{bit.net, object, source});
            return name_for_net(bit.net);
        case BitRef::Kind::Zero:
            return tie(false);
        case BitRef::Kind::One:
            return tie(true);
        case BitRef::Kind::X:
            error(DiagnosticCode::UnsupportedConstant,
                  "cell '" + object + "' has an x constant input", object, source);
            return "$x";
        case BitRef::Kind::Z:
            error(DiagnosticCode::UnsupportedConstant,
                  "cell '" + object + "' has a z constant input", object, source);
            return "$z";
        }
        return "$invalid";
    }

    bool output(const BitRef bit, const std::string& object, const SourceLocation& source,
                std::string& out) {
        if (bit.kind != BitRef::Kind::Net) {
            error(DiagnosticCode::UnsupportedConstant,
                  "cell '" + object + "' drives a constant output", object, source);
            return false;
        }
        out = name_for_net(bit.net);
        add_driver(bit.net, "cell '" + object + "'", source, false);
        return true;
    }

    std::string invert_net(const std::string& net, const std::string& object,
                           const SourceLocation& source) {
        const std::string inverted = unique_name("$not");
        add_gate(db::GateType::Not, {net}, inverted);
        (void)object;
        (void)source;
        return inverted;
    }

    void populate_ports() {
        for (const PortBit& port : port_bits_) {
            if (port.direction == "input") {
                if (port.bit.kind != BitRef::Kind::Net) {
                    error(DiagnosticCode::UnsupportedConstant,
                          "input port '" + port.name + "' is a constant", port.name,
                          module_source_);
                    continue;
                }
                const std::string name = name_for_net(port.bit.net);
                if (!contains(top_module_.inputs, name)) {
                    top_module_.inputs.push_back(name);
                }
                add_driver(port.bit.net, "port '" + port.name + "'", net_source(port.bit.net),
                           true);
            } else if (port.direction == "output") {
                if (port.bit.kind != BitRef::Kind::Net) {
                    error(DiagnosticCode::UnsupportedConstant,
                          "output port '" + port.name + "' is a constant", port.name,
                          module_source_);
                    continue;
                }
                const std::string name = name_for_net(port.bit.net);
                if (!contains(top_module_.outputs, name)) {
                    top_module_.outputs.push_back(name);
                }
            } else {
                error(DiagnosticCode::UnsupportedPort,
                      "port '" + port.name + "' has direction '" + port.direction +
                          "', which v0 does not import",
                      port.name, module_source_);
            }
        }
    }

    void map_cells(const JsonValue& module) {
        const JsonValue* cells = module.find("cells");
        if (cells == nullptr || !cells->is_object()) {
            return;
        }
        for (const auto& [name, cell] : cells->object) {
            if (!cell->is_object()) {
                error(DiagnosticCode::InvalidJson, "cell '" + name + "' is not an object", name,
                      module_source_);
                continue;
            }
            const std::string type = json_string(cell->find("type"));
            if (type == "$scopeinfo") {
                continue;
            }
            if (type.empty()) {
                error(DiagnosticCode::UnmappedCell, "cell '" + name + "' has no type", name,
                      source_from(*cell));
                continue;
            }
            ++result_.counts.yosys_cells[type];
            const SourceLocation source =
                source_from(*cell).empty() ? module_source_ : source_from(*cell);
            if (!type.empty() && type.front() == '$') {
                map_primitive(*cell, type, name, source);
            } else {
                map_library_cell(*cell, type, name, source);
            }
        }
    }

    bool read_bits(const JsonValue& cell, const std::string_view port, std::vector<BitRef>& out,
                   const std::string& object, const SourceLocation& source) {
        const JsonValue* connections = cell.find("connections");
        const JsonValue* bits = connections != nullptr ? connections->find(port) : nullptr;
        if (bits == nullptr || !bits->is_array()) {
            error(DiagnosticCode::UnmappedCell,
                  "cell '" + object + "' has no connection for port '" + std::string(port) + "'",
                  object, source);
            return false;
        }
        for (const auto& value : bits->array) {
            BitRef bit;
            if (!parse_bit(*value, bit)) {
                error(DiagnosticCode::InvalidJson,
                      "cell '" + object + "' has an invalid bit on port '" + std::string(port) +
                          "'",
                      object, source);
                return false;
            }
            out.push_back(bit);
        }
        return true;
    }

    bool one_bit(const JsonValue& cell, const std::string_view port, BitRef& out,
                 const std::string& object, const SourceLocation& source) {
        std::vector<BitRef> bits;
        if (!read_bits(cell, port, bits, object, source)) {
            return false;
        }
        if (bits.size() != 1) {
            error(DiagnosticCode::WidthMismatch,
                  "cell '" + object + "' port '" + std::string(port) + "' is " +
                      std::to_string(bits.size()) + " bits, expected 1",
                  object, source);
            return false;
        }
        out = bits[0];
        return true;
    }

    bool width_bits(const JsonValue& cell, const std::string_view port, const std::uint32_t width,
                    std::vector<BitRef>& out, const std::string& object,
                    const SourceLocation& source) {
        if (!read_bits(cell, port, out, object, source)) {
            return false;
        }
        if (out.size() != width) {
            error(DiagnosticCode::WidthMismatch,
                  "cell '" + object + "' port '" + std::string(port) + "' is " +
                      std::to_string(out.size()) + " bits, expected " + std::to_string(width),
                  object, source);
            return false;
        }
        return true;
    }

    // Word-level $dffsr carries one SET and one CLR bit per lane, but accepting a
    // single shared bit keeps older or simplified JSON importable.
    bool control_bits(const JsonValue& cell, const std::string_view port, const std::uint32_t width,
                      std::vector<BitRef>& out, const std::string& object,
                      const SourceLocation& source) {
        if (!read_bits(cell, port, out, object, source)) {
            return false;
        }
        if (out.size() == width) {
            return true;
        }
        if (out.size() == 1) {
            out.resize(width, out.front());
            return true;
        }
        const std::string expected = width == 1 ? "1" : std::to_string(width) + " or 1";
        error(DiagnosticCode::WidthMismatch,
              "cell '" + object + "' port '" + std::string(port) + "' is " +
                  std::to_string(out.size()) + " bits, expected " + expected,
              object, source);
        return false;
    }

    void width_error(const std::string& object, const std::string& what,
                     const SourceLocation& source) {
        error(DiagnosticCode::WidthMismatch, "cell '" + object + "': " + what, object, source);
    }

    bool require_width(const JsonValue& cell, const std::string_view parameter,
                       const std::string& object, const SourceLocation& source,
                       std::uint32_t& out) {
        if (!parameter_u32(cell, parameter, out)) {
            width_error(object, "missing or invalid " + std::string(parameter) + " parameter",
                        source);
            return false;
        }
        return true;
    }

    void add_gate(const db::GateType type, std::vector<std::string> fanins, const std::string& out,
                  const std::string& enable = {}, const std::string& async_clear = {},
                  const std::string& async_preset = {},
                  const db::ClockEdge clock_edge = db::ClockEdge::Rising) {
        db::Gate gate;
        gate.type = type;
        gate.name = "g" + std::to_string(next_gate_++);
        gate.fanins = std::move(fanins);
        gate.out = out;
        gate.enable = enable;
        gate.async_clear = async_clear;
        gate.async_preset = async_preset;
        gate.clock_edge = clock_edge;
        gate.lib_cell = db::kInvalidId;
        top_module_.gates.push_back(std::move(gate));
    }

    void map_bitwise(const JsonValue& cell, const db::GateType type, const std::string& object,
                     const SourceLocation& source) {
        std::uint32_t a_width = 0;
        std::uint32_t b_width = 0;
        std::uint32_t y_width = 0;
        if (!require_width(cell, "A_WIDTH", object, source, a_width) ||
            !require_width(cell, "B_WIDTH", object, source, b_width) ||
            !require_width(cell, "Y_WIDTH", object, source, y_width)) {
            return;
        }
        if (a_width == 0 || b_width == 0 || y_width == 0 || a_width != b_width ||
            b_width != y_width) {
            width_error(object,
                        "bitwise cell has inconsistent widths A=" + std::to_string(a_width) +
                            " B=" + std::to_string(b_width) + " Y=" + std::to_string(y_width),
                        source);
            return;
        }
        std::vector<BitRef> a;
        std::vector<BitRef> b;
        std::vector<BitRef> y;
        if (!width_bits(cell, "A", a_width, a, object, source) ||
            !width_bits(cell, "B", b_width, b, object, source) ||
            !width_bits(cell, "Y", y_width, y, object, source)) {
            return;
        }
        // Each lane is an independent primitive; a bus therefore becomes one
        // gate per bit with the same source location.
        for (std::size_t i = 0; i < a.size(); ++i) {
            const std::string an = fanin(a[i], object, source);
            const std::string bn = fanin(b[i], object, source);
            std::string out;
            if (!output(y[i], object, source, out)) {
                continue;
            }
            add_gate(type, {an, bn}, out);
        }
    }

    void map_unary(const JsonValue& cell, const db::GateType type, const std::string& object,
                   const SourceLocation& source) {
        std::uint32_t a_width = 0;
        std::uint32_t y_width = 0;
        if (!require_width(cell, "A_WIDTH", object, source, a_width) ||
            !require_width(cell, "Y_WIDTH", object, source, y_width)) {
            return;
        }
        if (a_width != y_width || a_width == 0) {
            width_error(object,
                        "unary cell has inconsistent widths A=" + std::to_string(a_width) +
                            " Y=" + std::to_string(y_width),
                        source);
            return;
        }
        std::vector<BitRef> a;
        std::vector<BitRef> y;
        if (!width_bits(cell, "A", a_width, a, object, source) ||
            !width_bits(cell, "Y", y_width, y, object, source)) {
            return;
        }
        for (std::size_t i = 0; i < a.size(); ++i) {
            const std::string an = fanin(a[i], object, source);
            std::string out;
            if (!output(y[i], object, source, out)) {
                continue;
            }
            add_gate(type, {an}, out);
        }
    }

    void map_mux(const JsonValue& cell, const std::string& object, const SourceLocation& source) {
        std::uint32_t width = 0;
        if (!require_width(cell, "WIDTH", object, source, width)) {
            return;
        }
        if (width == 0) {
            width_error(object, "mux has zero width", source);
            return;
        }
        std::vector<BitRef> a;
        std::vector<BitRef> b;
        std::vector<BitRef> y;
        BitRef s;
        if (!width_bits(cell, "A", width, a, object, source) ||
            !width_bits(cell, "B", width, b, object, source) ||
            !width_bits(cell, "Y", width, y, object, source) ||
            !one_bit(cell, "S", s, object, source)) {
            return;
        }
        const std::string sn = fanin(s, object, source);
        for (std::size_t i = 0; i < a.size(); ++i) {
            const std::string an = fanin(a[i], object, source);
            const std::string bn = fanin(b[i], object, source);
            std::string out;
            if (!output(y[i], object, source, out)) {
                continue;
            }
            // Yosys mux semantics: Y = S ? B : A. The db Mux2 convention is
            // {then, else, select}.
            add_gate(db::GateType::Mux2, {bn, an, sn}, out);
        }
    }

    void map_scalar_binary(const JsonValue& cell, const db::GateType type,
                           const std::string& object, const SourceLocation& source) {
        BitRef a;
        BitRef b;
        BitRef y;
        if (!one_bit(cell, "A", a, object, source) || !one_bit(cell, "B", b, object, source) ||
            !one_bit(cell, "Y", y, object, source)) {
            return;
        }
        const std::string an = fanin(a, object, source);
        const std::string bn = fanin(b, object, source);
        std::string out;
        if (!output(y, object, source, out)) {
            return;
        }
        add_gate(type, {an, bn}, out);
    }

    void map_scalar_unary(const JsonValue& cell, const db::GateType type, const std::string& object,
                          const SourceLocation& source) {
        BitRef a;
        BitRef y;
        if (!one_bit(cell, "A", a, object, source) || !one_bit(cell, "Y", y, object, source)) {
            return;
        }
        const std::string an = fanin(a, object, source);
        std::string out;
        if (!output(y, object, source, out)) {
            return;
        }
        add_gate(type, {an}, out);
    }

    void map_scalar_mux(const JsonValue& cell, const std::string& object,
                        const SourceLocation& source) {
        BitRef a;
        BitRef b;
        BitRef s;
        BitRef y;
        if (!one_bit(cell, "A", a, object, source) || !one_bit(cell, "B", b, object, source) ||
            !one_bit(cell, "S", s, object, source) || !one_bit(cell, "Y", y, object, source)) {
            return;
        }
        const std::string an = fanin(a, object, source);
        const std::string bn = fanin(b, object, source);
        const std::string sn = fanin(s, object, source);
        std::string out;
        if (!output(y, object, source, out)) {
            return;
        }
        add_gate(db::GateType::Mux2, {bn, an, sn}, out);
    }

    void map_dff(const JsonValue& cell, const std::string& type, const std::string& object,
                 const SourceLocation& source) {
        std::string code = type.substr(6);  // "$_DFF_"
        if (!code.empty() && code.back() == '_') {
            code.pop_back();
        }
        if (code.size() != 1 && code.size() != 3) {
            error(DiagnosticCode::UnmappedCell, "unsupported flip-flop cell '" + type + "'", object,
                  source);
            return;
        }
        const char clock = code[0];
        if (clock != 'P' && clock != 'N') {
            error(DiagnosticCode::UnmappedCell,
                  "unsupported flip-flop clock polarity in '" + type + "'", object, source);
            return;
        }
        BitRef d;
        BitRef c;
        BitRef q;
        if (!one_bit(cell, "D", d, object, source) || !one_bit(cell, "C", c, object, source) ||
            !one_bit(cell, "Q", q, object, source)) {
            return;
        }
        std::string enable;
        std::string clear;
        std::string preset;
        std::vector<std::string> fanins = {fanin(d, object, source), fanin(c, object, source)};
        if (code.size() == 3) {
            const char reset_polarity = code[1];
            const char reset_value = code[2];
            BitRef r;
            if (!one_bit(cell, "R", r, object, source)) {
                return;
            }
            const std::string rn = fanin(r, object, source);
            const bool active_high = reset_polarity == 'P';
            if (reset_value == '0') {
                clear = active_high ? rn : invert_net(rn, object, source);
            } else if (reset_value == '1') {
                preset = active_high ? rn : invert_net(rn, object, source);
            } else {
                error(DiagnosticCode::UnmappedCell,
                      "unsupported reset value in flip-flop cell '" + type + "'", object, source);
                return;
            }
            if (!clear.empty()) {
                fanins.push_back(clear);
            }
            if (!preset.empty()) {
                fanins.push_back(preset);
            }
        }
        std::string out;
        if (!output(q, object, source, out)) {
            return;
        }
        add_gate(db::GateType::Dff, std::move(fanins), out, enable, clear, preset,
                 clock == 'N' ? db::ClockEdge::Falling : db::ClockEdge::Rising);
    }

    void map_dffe(const JsonValue& cell, const std::string& type, const std::string& object,
                  const SourceLocation& source) {
        std::string code = type.substr(7);  // "$_DFFE_"
        if (!code.empty() && code.back() == '_') {
            code.pop_back();
        }
        if (code.size() != 2 || (code[0] != 'P' && code[0] != 'N') ||
            (code[1] != 'P' && code[1] != 'N')) {
            error(DiagnosticCode::UnmappedCell, "unsupported enabled flip-flop cell '" + type + "'",
                  object, source);
            return;
        }
        BitRef d;
        BitRef c;
        BitRef e;
        BitRef q;
        if (!one_bit(cell, "D", d, object, source) || !one_bit(cell, "C", c, object, source) ||
            !one_bit(cell, "E", e, object, source) || !one_bit(cell, "Q", q, object, source)) {
            return;
        }
        const std::string dn = fanin(d, object, source);
        const std::string cn = fanin(c, object, source);
        const std::string en = fanin(e, object, source);
        const std::string enable = code[1] == 'P' ? en : invert_net(en, object, source);
        std::vector<std::string> fanins = {dn, cn, enable};
        std::string out;
        if (!output(q, object, source, out)) {
            return;
        }
        add_gate(db::GateType::Dff, std::move(fanins), out, enable, {}, {},
                 code[0] == 'N' ? db::ClockEdge::Falling : db::ClockEdge::Rising);
    }

    void map_dlatch_primitive(const JsonValue& cell, const std::string& type,
                              const std::string& object, const SourceLocation& source) {
        std::string code = type.substr(9);  // "$_DLATCH_"
        if (!code.empty() && code.back() == '_') {
            code.pop_back();
        }
        if (code.size() != 1 || (code[0] != 'P' && code[0] != 'N')) {
            error(DiagnosticCode::UnmappedCell, "unsupported latch cell '" + type + "'", object,
                  source);
            return;
        }
        BitRef d;
        BitRef e;
        BitRef q;
        if (!one_bit(cell, "D", d, object, source) || !one_bit(cell, "E", e, object, source) ||
            !one_bit(cell, "Q", q, object, source)) {
            return;
        }
        const std::string dn = fanin(d, object, source);
        const std::string en = fanin(e, object, source);
        const std::string enable = code[0] == 'P' ? en : invert_net(en, object, source);
        std::string out;
        if (!output(q, object, source, out)) {
            return;
        }
        add_gate(db::GateType::Dlat, {dn, enable}, out);
    }

    void map_word_dff(const JsonValue& cell, const std::string& type, const std::string& object,
                      const SourceLocation& source) {
        (void)type;
        std::uint32_t width = 0;
        std::uint32_t clock_polarity = 0;
        if (!require_width(cell, "WIDTH", object, source, width) ||
            !require_width(cell, "CLK_POLARITY", object, source, clock_polarity)) {
            return;
        }
        if (width == 0) {
            width_error(object, "flip-flop has zero width", source);
            return;
        }
        std::vector<BitRef> d;
        std::vector<BitRef> q;
        BitRef c;
        if (!width_bits(cell, "D", width, d, object, source) ||
            !width_bits(cell, "Q", width, q, object, source) ||
            !one_bit(cell, "CLK", c, object, source)) {
            return;
        }
        const std::string cn = fanin(c, object, source);
        const db::ClockEdge edge =
            clock_polarity == 0 ? db::ClockEdge::Falling : db::ClockEdge::Rising;
        for (std::size_t i = 0; i < d.size(); ++i) {
            const std::string dn = fanin(d[i], object, source);
            std::string out;
            if (!output(q[i], object, source, out)) {
                continue;
            }
            add_gate(db::GateType::Dff, {dn, cn}, out, {}, {}, {}, edge);
        }
    }

    void map_word_dffe(const JsonValue& cell, const std::string& object,
                       const SourceLocation& source) {
        std::uint32_t width = 0;
        std::uint32_t clock_polarity = 0;
        std::uint32_t enable_polarity = 0;
        if (!require_width(cell, "WIDTH", object, source, width) ||
            !require_width(cell, "CLK_POLARITY", object, source, clock_polarity) ||
            !require_width(cell, "EN_POLARITY", object, source, enable_polarity)) {
            return;
        }
        if (width == 0) {
            width_error(object, "flip-flop has zero width", source);
            return;
        }
        std::vector<BitRef> d;
        std::vector<BitRef> q;
        BitRef c;
        BitRef e;
        if (!width_bits(cell, "D", width, d, object, source) ||
            !width_bits(cell, "Q", width, q, object, source) ||
            !one_bit(cell, "CLK", c, object, source) || !one_bit(cell, "EN", e, object, source)) {
            return;
        }
        const std::string cn = fanin(c, object, source);
        const std::string en = fanin(e, object, source);
        const std::string enable = enable_polarity == 0 ? invert_net(en, object, source) : en;
        const db::ClockEdge edge =
            clock_polarity == 0 ? db::ClockEdge::Falling : db::ClockEdge::Rising;
        for (std::size_t i = 0; i < d.size(); ++i) {
            const std::string dn = fanin(d[i], object, source);
            std::string out;
            if (!output(q[i], object, source, out)) {
                continue;
            }
            add_gate(db::GateType::Dff, {dn, cn, enable}, out, enable, {}, {}, edge);
        }
    }

    void map_word_adff(const JsonValue& cell, const std::string& object,
                       const SourceLocation& source) {
        std::uint32_t width = 0;
        std::uint32_t clock_polarity = 0;
        std::uint32_t reset_polarity = 0;
        std::uint64_t reset_value = 0;
        if (!require_width(cell, "WIDTH", object, source, width) ||
            !require_width(cell, "CLK_POLARITY", object, source, clock_polarity) ||
            !require_width(cell, "ARST_POLARITY", object, source, reset_polarity)) {
            return;
        }
        if (!parameter_u64(cell, "ARST_VALUE", reset_value)) {
            width_error(object, "missing or invalid ARST_VALUE parameter", source);
            return;
        }
        if (width == 0 || width > 64) {
            width_error(object,
                        "flip-flop width " + std::to_string(width) +
                            " cannot carry an async reset value",
                        source);
            return;
        }
        std::vector<BitRef> d;
        std::vector<BitRef> q;
        BitRef c;
        BitRef r;
        if (!width_bits(cell, "D", width, d, object, source) ||
            !width_bits(cell, "Q", width, q, object, source) ||
            !one_bit(cell, "CLK", c, object, source) || !one_bit(cell, "ARST", r, object, source)) {
            return;
        }
        const std::string cn = fanin(c, object, source);
        const std::string rn = fanin(r, object, source);
        const std::string control = reset_polarity == 0 ? invert_net(rn, object, source) : rn;
        const db::ClockEdge edge =
            clock_polarity == 0 ? db::ClockEdge::Falling : db::ClockEdge::Rising;
        for (std::size_t i = 0; i < d.size(); ++i) {
            const bool reset_one = ((reset_value >> i) & 1u) != 0;
            const std::string clear = reset_one ? std::string() : control;
            const std::string preset = reset_one ? control : std::string();
            const std::string dn = fanin(d[i], object, source);
            std::vector<std::string> fanins = {dn, cn};
            if (!clear.empty()) {
                fanins.push_back(clear);
            }
            if (!preset.empty()) {
                fanins.push_back(preset);
            }
            std::string out;
            if (!output(q[i], object, source, out)) {
                continue;
            }
            add_gate(db::GateType::Dff, std::move(fanins), out, {}, clear, preset, edge);
        }
    }

    void map_word_dffsr(const JsonValue& cell, const std::string& object,
                        const SourceLocation& source) {
        std::uint32_t width = 0;
        std::uint32_t clock_polarity = 0;
        std::uint32_t set_polarity = 0;
        std::uint32_t clear_polarity = 0;
        if (!require_width(cell, "WIDTH", object, source, width) ||
            !require_width(cell, "CLK_POLARITY", object, source, clock_polarity) ||
            !require_width(cell, "SET_POLARITY", object, source, set_polarity) ||
            !require_width(cell, "CLR_POLARITY", object, source, clear_polarity)) {
            return;
        }
        if (width == 0) {
            width_error(object, "flip-flop has zero width", source);
            return;
        }
        std::vector<BitRef> d;
        std::vector<BitRef> q;
        std::vector<BitRef> set;
        std::vector<BitRef> clear;
        BitRef c;
        if (!width_bits(cell, "D", width, d, object, source) ||
            !width_bits(cell, "Q", width, q, object, source) ||
            !one_bit(cell, "CLK", c, object, source) ||
            !control_bits(cell, "SET", width, set, object, source) ||
            !control_bits(cell, "CLR", width, clear, object, source)) {
            return;
        }
        const std::string cn = fanin(c, object, source);
        std::map<std::string, std::string> inverted_controls;
        const auto control = [&](const BitRef& bit, const std::uint32_t polarity) -> std::string {
            const std::string net = fanin(bit, object, source);
            if (polarity != 0) {
                return net;
            }
            const auto cached = inverted_controls.find(net);
            if (cached != inverted_controls.end()) {
                return cached->second;
            }
            const std::string inverted = invert_net(net, object, source);
            inverted_controls.emplace(net, inverted);
            return inverted;
        };
        const db::ClockEdge edge =
            clock_polarity == 0 ? db::ClockEdge::Falling : db::ClockEdge::Rising;
        for (std::size_t i = 0; i < d.size(); ++i) {
            const std::string dn = fanin(d[i], object, source);
            const std::string preset = control(set[i], set_polarity);
            const std::string clear_net = control(clear[i], clear_polarity);
            std::vector<std::string> fanins = {dn, cn, clear_net, preset};
            std::string out;
            if (!output(q[i], object, source, out)) {
                continue;
            }
            add_gate(db::GateType::Dff, std::move(fanins), out, {}, clear_net, preset, edge);
        }
    }

    void map_word_dlatch(const JsonValue& cell, const std::string& object,
                         const SourceLocation& source) {
        std::uint32_t width = 0;
        std::uint32_t enable_polarity = 0;
        if (!require_width(cell, "WIDTH", object, source, width) ||
            !require_width(cell, "EN_POLARITY", object, source, enable_polarity)) {
            return;
        }
        if (width == 0) {
            width_error(object, "latch has zero width", source);
            return;
        }
        std::vector<BitRef> d;
        std::vector<BitRef> q;
        BitRef e;
        if (!width_bits(cell, "D", width, d, object, source) ||
            !width_bits(cell, "Q", width, q, object, source) ||
            !one_bit(cell, "EN", e, object, source)) {
            return;
        }
        const std::string en = fanin(e, object, source);
        const std::string enable = enable_polarity == 0 ? invert_net(en, object, source) : en;
        for (std::size_t i = 0; i < d.size(); ++i) {
            const std::string dn = fanin(d[i], object, source);
            std::string out;
            if (!output(q[i], object, source, out)) {
                continue;
            }
            add_gate(db::GateType::Dlat, {dn, enable}, out);
        }
    }

    void map_primitive(const JsonValue& cell, const std::string& type, const std::string& object,
                       const SourceLocation& source) {
        if (type == "$and") {
            map_bitwise(cell, db::GateType::And2, object, source);
        } else if (type == "$or") {
            map_bitwise(cell, db::GateType::Or2, object, source);
        } else if (type == "$xor") {
            map_bitwise(cell, db::GateType::Xor2, object, source);
        } else if (type == "$xnor") {
            map_bitwise(cell, db::GateType::Xnor2, object, source);
        } else if (type == "$not") {
            map_unary(cell, db::GateType::Not, object, source);
        } else if (type == "$buf") {
            map_unary(cell, db::GateType::Buf, object, source);
        } else if (type == "$mux") {
            map_mux(cell, object, source);
        } else if (type == "$_AND_") {
            map_scalar_binary(cell, db::GateType::And2, object, source);
        } else if (type == "$_OR_") {
            map_scalar_binary(cell, db::GateType::Or2, object, source);
        } else if (type == "$_XOR_") {
            map_scalar_binary(cell, db::GateType::Xor2, object, source);
        } else if (type == "$_XNOR_") {
            map_scalar_binary(cell, db::GateType::Xnor2, object, source);
        } else if (type == "$_NAND_") {
            map_scalar_binary(cell, db::GateType::Nand2, object, source);
        } else if (type == "$_NOR_") {
            map_scalar_binary(cell, db::GateType::Nor2, object, source);
        } else if (type == "$_NOT_") {
            map_scalar_unary(cell, db::GateType::Not, object, source);
        } else if (type == "$_BUF_") {
            map_scalar_unary(cell, db::GateType::Buf, object, source);
        } else if (type == "$_MUX_") {
            map_scalar_mux(cell, object, source);
        } else if (type.rfind("$_DFFE_", 0) == 0) {
            map_dffe(cell, type, object, source);
        } else if (type.rfind("$_DFF_", 0) == 0) {
            map_dff(cell, type, object, source);
        } else if (type.rfind("$_DLATCH_", 0) == 0) {
            map_dlatch_primitive(cell, type, object, source);
        } else if (type == "$dff") {
            map_word_dff(cell, type, object, source);
        } else if (type == "$dffe") {
            map_word_dffe(cell, object, source);
        } else if (type == "$adff") {
            map_word_adff(cell, object, source);
        } else if (type == "$dffsr") {
            map_word_dffsr(cell, object, source);
        } else if (type == "$dlatch") {
            map_word_dlatch(cell, object, source);
        } else {
            error(DiagnosticCode::UnmappedCell,
                  "cell '" + object + "' has unsupported type '" + type + "'", object, source);
        }
    }

    void map_library_cell(const JsonValue& cell, const std::string& type, const std::string& object,
                          const SourceLocation& source) {
        const db::Module* library = db::find_module(result_.design, type);
        if (library == nullptr) {
            error(DiagnosticCode::UnresolvedModule,
                  "cell '" + object + "' references module '" + type + "', which is not defined",
                  object, source);
            return;
        }
        const JsonValue* connections = cell.find("connections");
        if (connections == nullptr || !connections->is_object()) {
            error(DiagnosticCode::UnmappedCell, "cell '" + object + "' has no connections object",
                  object, source);
            return;
        }
        const JsonValue* directions = cell.find("port_directions");
        db::Instance instance;
        instance.path = "u" + std::to_string(next_instance_++);
        instance.module = type;
        for (const auto& [port, bits] : connections->object) {
            const bool known_input = contains(library->inputs, port);
            const bool known_output = contains(library->outputs, port);
            if (!known_input && !known_output) {
                error(DiagnosticCode::UnsupportedPort,
                      "cell '" + object + "' has port '" + port + "', which module '" + type +
                          "' does not define",
                      object, source);
                continue;
            }
            std::string direction;
            if (directions != nullptr && directions->is_object()) {
                direction = json_string(directions->find(port));
            }
            if (direction.empty()) {
                direction = known_input ? "input" : "output";
            }
            if (direction == "inout") {
                error(DiagnosticCode::UnsupportedPort,
                      "cell '" + object + "' has inout port '" + port + "'", object, source);
                continue;
            }
            if (direction != "input" && direction != "output") {
                error(DiagnosticCode::UnsupportedPort,
                      "cell '" + object + "' has port '" + port + "' with direction '" + direction +
                          "'",
                      object, source);
                continue;
            }
            if (!bits->is_array() || bits->array.size() != 1) {
                error(DiagnosticCode::WidthMismatch,
                      "cell '" + object + "' port '" + port + "' is " +
                          std::to_string(bits->is_array() ? bits->array.size() : 0) +
                          " bits, expected 1",
                      object, source);
                continue;
            }
            BitRef bit;
            if (!parse_bit(*bits->array[0], bit)) {
                error(DiagnosticCode::InvalidJson,
                      "cell '" + object + "' has an invalid bit on port '" + port + "'", object,
                      source);
                continue;
            }
            if (direction == "input") {
                instance.connections[port] = fanin(bit, object, source);
            } else if (!output(bit, object, source, instance.connections[port])) {
                continue;
            }
        }
        result_.design.instances.push_back(std::move(instance));
    }

    void validate_undriven() {
        std::set<std::uint32_t> reported;
        for (const FaninUse& use : fanins_) {
            if (drivers_.find(use.net) != drivers_.end() || !reported.insert(use.net).second) {
                continue;
            }
            error(DiagnosticCode::UndrivenInput,
                  "net '" + name_for_net(use.net) + "' is used by '" + use.object +
                      "' but has no driver",
                  name_for_net(use.net), use.source);
        }
    }

    void count_flat(const db::FlatNetlist& flat) {
        for (const db::GateType type : flat.type) {
            ++result_.counts.primitives[std::string(db::gate_type_name(type))];
        }
    }

    void count_top_module() {
        for (const db::Gate& gate : top_module_.gates) {
            ++result_.counts.primitives[std::string(db::gate_type_name(gate.type))];
        }
    }

    void flatten_and_finish() {
        count_top_module();
        result_.design.top = top_name_;
        result_.design.modules.push_back(std::move(top_module_));
        db::DesignDb database;
        database.design() = result_.design;
        const db::Status status = database.flatten();
        if (status.ok()) {
            result_.design = database.design();
            result_.counts.primitives.clear();
            count_flat(database.flat());
        } else {
            if (status.error() == db::Error::CycleDetected) {
                error(DiagnosticCode::CombinationalLoop, status.message(), top_name_,
                      module_source_);
            } else if (status.error() == db::Error::MultipleDrivers) {
                if (!has_code(DiagnosticCode::MultipleDrivers)) {
                    error(DiagnosticCode::MultipleDrivers, status.message(), top_name_,
                          module_source_);
                }
            } else if (status.error() == db::Error::BadPayload &&
                       status.message().find("not defined") != std::string::npos) {
                if (!has_code(DiagnosticCode::UnresolvedModule)) {
                    error(DiagnosticCode::UnresolvedModule, status.message(), top_name_,
                          module_source_);
                }
            } else {
                error(DiagnosticCode::YosysError, status.message(), top_name_, module_source_);
            }
        }
        reconcile();
    }

    void reconcile() {
        if (expected_.empty()) {
            return;
        }
        std::set<std::string> types;
        for (const auto& [type, count] : expected_) {
            (void)count;
            types.insert(type);
        }
        for (const auto& [type, count] : result_.counts.yosys_cells) {
            (void)count;
            types.insert(type);
        }
        for (const std::string& type : types) {
            const auto expected_it = expected_.find(type);
            const auto actual_it = result_.counts.yosys_cells.find(type);
            const std::uint64_t expected = expected_it == expected_.end() ? 0 : expected_it->second;
            const std::uint64_t actual =
                actual_it == result_.counts.yosys_cells.end() ? 0 : actual_it->second;
            if (expected == actual) {
                continue;
            }
            result_.counts.deltas.push_back(CellCountDelta{type, expected, actual});
            error(DiagnosticCode::CellCountMismatch,
                  "cell count for '" + type + "' is " + std::to_string(actual) + ", reference " +
                      std::to_string(expected),
                  type, module_source_);
        }
    }

    ImportResult finish() {
        result_.ok = !has_errors(result_.diagnostics);
        return std::move(result_);
    }

    const JsonValue& root_;
    std::string top_name_;
    db::Design base_;
    std::map<std::string, std::uint64_t> expected_;
    ImportResult result_;
    db::Module top_module_;
    SourceLocation module_source_;
    std::vector<PortBit> port_bits_;
    std::map<std::uint32_t, NameChoice> bit_names_;
    std::map<std::uint32_t, std::string> names_;
    std::map<std::uint32_t, SourceLocation> net_sources_;
    std::set<std::string> used_names_;
    std::map<std::uint32_t, Driver> drivers_;
    std::vector<FaninUse> fanins_;
    std::uint32_t next_gate_ = 0;
    std::uint32_t next_instance_ = 0;
    std::string tie_zero_;
    std::string tie_one_;
};

}  // namespace

ImportResult import_yosys_json(const std::string_view json, const std::string_view top) {
    return import_yosys_json(json, top, {}, {});
}

ImportResult import_yosys_json(const std::string_view json, const std::string_view top,
                               const db::Design& library_design,
                               const std::map<std::string, std::uint64_t>& expected_cells) {
    JsonValue root;
    std::string error_text;
    if (!parse_json(json, root, error_text)) {
        ImportResult result;
        result.design = library_design;
        Diagnostic diagnostic;
        diagnostic.code = DiagnosticCode::InvalidJson;
        diagnostic.severity = DiagnosticSeverity::Error;
        diagnostic.message = std::move(error_text);
        result.diagnostics.push_back(std::move(diagnostic));
        return result;
    }
    Importer importer(root, top, library_design, expected_cells);
    return importer.run();
}

}  // namespace gmax::frontend
