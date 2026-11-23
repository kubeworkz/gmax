#include "core/frontend/decompose.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <utility>
#include <vector>

#include "core/frontend/expr.h"

namespace gmax::frontend {
namespace {

Expr variable(std::string name) {
    Expr expr;
    expr.kind = ExprKind::Variable;
    expr.name = std::move(name);
    return expr;
}

Expr constant(bool value) {
    Expr expr;
    expr.kind = ExprKind::Constant;
    expr.value = value;
    return expr;
}

Expr binary(ExprKind kind, Expr left, Expr right) {
    Expr expr;
    expr.kind = kind;
    expr.args.push_back(std::move(left));
    expr.args.push_back(std::move(right));
    return expr;
}

Expr inverted(Expr inner) {
    Expr expr;
    expr.kind = ExprKind::Not;
    expr.args.push_back(std::move(inner));
    return expr;
}

bool is_scalar_bit(std::string_view text) { return text.size() == 1; }

std::string quote(const std::string& text) { return "'" + text + "'"; }

// A statetable row list becomes sum-of-products: every row whose next state is
// 1 contributes a term, '-' columns drop out of the term, and rows whose next
// state is '-' are don't-cares (architecture 6.2's next-state function).
db::Status statetable_expr(const Cell& cell, const StateTable& table, Expr& out) {
    std::vector<Expr> terms;
    for (const StateRow& row : table.rows) {
        if (row.next != "0" && row.next != "1" && row.next != "-") {
            return db::Status::failure(db::Error::BadPayload,
                                       "cell " + quote(cell.name) + ": statetable next state " +
                                           quote(row.next) + " is not 0, 1 or -");
        }
        if (row.inputs.size() != table.input_names.size()) {
            return db::Status::failure(db::Error::BadPayload,
                                       "cell " + quote(cell.name) + ": statetable row has " +
                                           std::to_string(row.inputs.size()) +
                                           " input values, expected " +
                                           std::to_string(table.input_names.size()));
        }
        // Validate the row shape even when the row's next state is 0 or a
        // don't-care: a malformed table should not pass because of which rows
        // happen to contribute terms.
        for (std::size_t i = 0; i < table.input_names.size(); ++i) {
            const std::string& bits = row.inputs[i];
            if (!is_scalar_bit(bits)) {
                return db::Status::failure(db::Error::BadPayload,
                                           "cell " + quote(cell.name) + ": statetable column " +
                                               quote(table.input_names[i]) + " is not scalar (" +
                                               quote(bits) + ")");
            }
            if (bits != "0" && bits != "1" && bits != "-") {
                return db::Status::failure(db::Error::BadPayload,
                                           "cell " + quote(cell.name) + ": statetable bit " +
                                               quote(bits) + " is not 0, 1 or -");
            }
        }
        if (!is_scalar_bit(row.current)) {
            return db::Status::failure(db::Error::BadPayload,
                                       "cell " + quote(cell.name) + ": statetable state value " +
                                           quote(row.current) + " is not scalar");
        }
        if (row.current != "0" && row.current != "1" && row.current != "-") {
            return db::Status::failure(db::Error::BadPayload,
                                       "cell " + quote(cell.name) + ": statetable state value " +
                                           quote(row.current) + " is not 0, 1 or -");
        }
        if (row.next == "0" || row.next == "-") {
            continue;
        }

        std::vector<Expr> literals;
        for (std::size_t i = 0; i < table.input_names.size(); ++i) {
            const std::string& bits = row.inputs[i];
            if (bits == "-") {
                continue;
            }
            Expr literal = variable(table.input_names[i]);
            if (bits == "0") {
                literal = inverted(std::move(literal));
            }
            literals.push_back(std::move(literal));
        }
        if (row.current != "-") {
            Expr literal = variable(table.state_name);
            if (row.current == "0") {
                literal = inverted(std::move(literal));
            }
            literals.push_back(std::move(literal));
        }

        Expr term;
        if (literals.empty()) {
            term = constant(true);
        } else {
            term = std::move(literals[0]);
            for (std::size_t i = 1; i < literals.size(); ++i) {
                term = binary(ExprKind::And, std::move(term), std::move(literals[i]));
            }
        }
        terms.push_back(std::move(term));
    }

    if (terms.empty()) {
        out = constant(false);
        return {};
    }
    out = std::move(terms[0]);
    for (std::size_t i = 1; i < terms.size(); ++i) {
        out = binary(ExprKind::Or, std::move(out), std::move(terms[i]));
    }
    return {};
}

class ConeEmitter {
  public:
    ConeEmitter(db::Module& module, std::uint32_t lib_cell, const std::vector<std::string>& inputs)
        : module_(module), lib_cell_(lib_cell), inputs_(inputs) {
        for (const std::string& name : module.inputs) {
            taken_.insert(name);
        }
        for (const std::string& name : module.outputs) {
            taken_.insert(name);
        }
    }

    std::string fresh_net() {
        while (true) {
            std::string name = "n" + std::to_string(next_net_++);
            if (taken_.insert(name).second) {
                return name;
            }
        }
    }

    db::Status add(db::GateType type, std::vector<std::string> fanins, const std::string& target,
                   std::string enable = {}, std::string async_clear = {},
                   std::string async_preset = {}) {
        db::Gate gate;
        gate.type = type;
        gate.name = "g" + std::to_string(next_gate_++);
        gate.fanins = std::move(fanins);
        gate.out = target;
        gate.lib_cell = lib_cell_;
        gate.enable = std::move(enable);
        gate.async_clear = std::move(async_clear);
        gate.async_preset = std::move(async_preset);
        module_.gates.push_back(std::move(gate));
        return {};
    }

    db::Status resolve(const std::string& name, const std::map<std::string, std::string>& aliases,
                       std::string& out) const {
        const auto alias = aliases.find(name);
        if (alias != aliases.end()) {
            out = alias->second;
            return {};
        }
        if (std::find(inputs_.begin(), inputs_.end(), name) == inputs_.end()) {
            return db::Status::failure(db::Error::BadPayload,
                                       "cell '" + module_.name + "': expression references '" +
                                           name + "', which is not an input pin of the cell");
        }
        out = name;
        return {};
    }

    db::Status net_for(const Expr& expr, const std::map<std::string, std::string>& aliases,
                       std::string& out) {
        if (expr.kind == ExprKind::Variable) {
            return resolve(expr.name, aliases, out);
        }
        if (expr.kind == ExprKind::Constant) {
            out = fresh_net();
            return add(expr.value ? db::GateType::Tie1 : db::GateType::Tie0, {}, out);
        }
        out = fresh_net();
        return emit(expr, out, aliases);
    }

    db::Status emit(const Expr& expr, const std::string& target,
                    const std::map<std::string, std::string>& aliases) {
        db::Status status;
        switch (expr.kind) {
        case ExprKind::Constant:
            return add(expr.value ? db::GateType::Tie1 : db::GateType::Tie0, {}, target);
        case ExprKind::Variable: {
            std::string net;
            status = resolve(expr.name, aliases, net);
            if (!status.ok()) {
                return status;
            }
            return add(db::GateType::Buf, {net}, target);
        }
        case ExprKind::Not: {
            if (expr.args.size() != 1) {
                return malformed("not");
            }
            std::string in;
            status = net_for(expr.args[0], aliases, in);
            if (!status.ok()) {
                return status;
            }
            return add(db::GateType::Not, {in}, target);
        }
        case ExprKind::And:
        case ExprKind::Or:
        case ExprKind::Xor: {
            if (expr.args.size() != 2) {
                return malformed("binary");
            }
            std::string left;
            status = net_for(expr.args[0], aliases, left);
            if (!status.ok()) {
                return status;
            }
            std::string right;
            status = net_for(expr.args[1], aliases, right);
            if (!status.ok()) {
                return status;
            }
            const db::GateType type = expr.kind == ExprKind::And  ? db::GateType::And2
                                      : expr.kind == ExprKind::Or ? db::GateType::Or2
                                                                  : db::GateType::Xor2;
            return add(type, {left, right}, target);
        }
        case ExprKind::Mux: {
            if (expr.args.size() != 3) {
                return malformed("mux");
            }
            std::string then_net;
            status = net_for(expr.args[0], aliases, then_net);
            if (!status.ok()) {
                return status;
            }
            std::string else_net;
            status = net_for(expr.args[1], aliases, else_net);
            if (!status.ok()) {
                return status;
            }
            std::string select_net;
            status = net_for(expr.args[2], aliases, select_net);
            if (!status.ok()) {
                return status;
            }
            return add(db::GateType::Mux2, {then_net, else_net, select_net}, target);
        }
        }
        return malformed("unknown");
    }

  private:
    db::Status malformed(std::string_view what) const {
        return db::Status::failure(db::Error::BadPayload, "cell '" + module_.name +
                                                              "': malformed " + std::string(what) +
                                                              " expression");
    }

    db::Module& module_;
    std::uint32_t lib_cell_;
    std::vector<std::string> inputs_;
    std::set<std::string> taken_;
    std::uint32_t next_net_ = 1;
    std::uint32_t next_gate_ = 1;
};

db::Status resolve_pin_name(const ConeEmitter& emitter, const std::string& text,
                            const std::map<std::string, std::string>& aliases,
                            const std::string& where, std::string& out) {
    if (text.empty()) {
        return db::Status::failure(db::Error::BadPayload, where + " is empty");
    }
    for (const char c : text) {
        const bool allowed = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                             (c >= '0' && c <= '9') || c == '_' || c == '[' || c == ']';
        if (!allowed) {
            return db::Status::failure(db::Error::BadPayload,
                                       where + " must name one pin, got '" + text + "'");
        }
    }
    return emitter.resolve(text, aliases, out);
}

db::Status with_context(const Cell& cell, const Pin& pin, const db::Status& status) {
    return db::Status::failure(status.error(), "cell '" + cell.name + "' pin '" + pin.name +
                                                   "': " + status.message());
}

// A Liberty sequential control (enable, clear, preset) is an expression over one
// pin. v0 accepts the pin or its complement and normalizes to active high by
// inserting a Not, so neither the gate model nor ATPG has to carry polarity.
db::Status control_net(const Cell& cell, const Pin& pin, std::string_view what,
                       const std::string& text, ConeEmitter& emitter, std::string& out) {
    const std::string where =
        "cell '" + cell.name + "' pin '" + pin.name + "' " + std::string(what);
    Expr expr;
    db::Status status = parse_expression(text, expr);
    if (!status.ok()) {
        return with_context(cell, pin, status);
    }
    bool invert = false;
    const Expr* operand = &expr;
    if (expr.kind == ExprKind::Not) {
        if (expr.args.size() != 1) {
            return db::Status::failure(db::Error::BadPayload,
                                       where + " must name one pin or its complement, got '" +
                                           text + "'");
        }
        invert = true;
        operand = &expr.args[0];
    }
    if (operand->kind != ExprKind::Variable) {
        return db::Status::failure(db::Error::BadPayload,
                                   where + " must name one pin or its complement, got '" + text +
                                       "'");
    }
    std::string net;
    status = emitter.resolve(operand->name, {}, net);
    if (!status.ok()) {
        return status;
    }
    if (!invert) {
        out = net;
        return {};
    }
    out = emitter.fresh_net();
    return emitter.add(db::GateType::Not, {net}, out);
}

db::Status decompose_sequential(const Cell& cell, const Pin& pin, ConeEmitter& emitter) {
    std::map<std::string, std::string> aliases;
    if (!pin.ff.state.empty()) {
        aliases[pin.ff.state] = pin.name;
    }
    if (!cell.statetable.state_name.empty()) {
        aliases[cell.statetable.state_name] = pin.name;
    }

    Expr next;
    bool has_next = false;
    db::Status status;
    if (!pin.ff.next_state.empty()) {
        status = parse_expression(pin.ff.next_state, next);
        if (!status.ok()) {
            return with_context(cell, pin, status);
        }
        has_next = true;
    } else if (!cell.statetable.rows.empty()) {
        status = statetable_expr(cell, cell.statetable, next);
        if (!status.ok()) {
            return status;
        }
        has_next = true;
    }
    if (!has_next) {
        return db::Status::failure(db::Error::BadPayload,
                                   "cell '" + cell.name + "' pin '" + pin.name +
                                       "' has no next_state and the cell has no statetable");
    }
    const std::string next_net = emitter.fresh_net();
    status = emitter.emit(next, next_net, aliases);
    if (!status.ok()) {
        return status;
    }

    std::string async_clear;
    if (!pin.ff.clear.empty()) {
        status = control_net(cell, pin, "clear", pin.ff.clear, emitter, async_clear);
        if (!status.ok()) {
            return status;
        }
    }
    std::string async_preset;
    if (!pin.ff.preset.empty()) {
        status = control_net(cell, pin, "preset", pin.ff.preset, emitter, async_preset);
        if (!status.ok()) {
            return status;
        }
    }

    if (pin.ff.kind == Ff::Kind::FlipFlop) {
        if (pin.ff.clock.empty()) {
            return db::Status::failure(db::Error::BadPayload, "cell '" + cell.name + "' pin '" +
                                                                  pin.name + "' has no clocked_on");
        }
        std::string clock;
        status =
            resolve_pin_name(emitter, pin.ff.clock, aliases,
                             "cell '" + cell.name + "' pin '" + pin.name + "' clocked_on", clock);
        if (!status.ok()) {
            return status;
        }
        std::string enable;
        if (!pin.ff.enable.empty()) {
            status = control_net(cell, pin, "enable", pin.ff.enable, emitter, enable);
            if (!status.ok()) {
                return status;
            }
        }
        std::vector<std::string> fanins = {next_net, clock};
        if (!enable.empty()) {
            fanins.push_back(enable);
        }
        if (!async_clear.empty()) {
            fanins.push_back(async_clear);
        }
        if (!async_preset.empty()) {
            fanins.push_back(async_preset);
        }
        return emitter.add(db::GateType::Dff, std::move(fanins), pin.name, enable, async_clear,
                           async_preset);
    }

    if (pin.ff.enable.empty()) {
        return db::Status::failure(db::Error::BadPayload,
                                   "cell '" + cell.name + "' pin '" + pin.name + "' has no enable");
    }
    std::string enable;
    status = control_net(cell, pin, "enable", pin.ff.enable, emitter, enable);
    if (!status.ok()) {
        return status;
    }
    std::vector<std::string> fanins = {next_net, enable};
    if (!async_clear.empty()) {
        fanins.push_back(async_clear);
    }
    if (!async_preset.empty()) {
        fanins.push_back(async_preset);
    }
    return emitter.add(db::GateType::Dlat, std::move(fanins), pin.name, {}, async_clear,
                       async_preset);
}

}  // namespace

db::Status decompose_cell(const Cell& cell, db::Module& module) {
    module = db::Module{};
    module.name = cell.name;

    std::vector<std::string> inputs;
    for (const Pin& pin : cell.pins) {
        if (pin.direction == Direction::Input) {
            inputs.push_back(pin.name);
        } else if (pin.direction == Direction::Output) {
            module.outputs.push_back(pin.name);
        }
    }
    module.inputs = inputs;
    if (module.outputs.empty()) {
        return db::Status::failure(db::Error::BadPayload,
                                   "cell '" + cell.name + "' has no output pin");
    }

    ConeEmitter emitter(module, cell.index, inputs);
    for (const Pin& pin : cell.pins) {
        if (pin.direction != Direction::Output) {
            continue;
        }
        if (pin.ff.kind != Ff::Kind::None) {
            const db::Status status = decompose_sequential(cell, pin, emitter);
            if (!status.ok()) {
                return status;
            }
            continue;
        }
        if (!pin.function.empty()) {
            Expr expr;
            db::Status status = parse_expression(pin.function, expr);
            if (!status.ok()) {
                return with_context(cell, pin, status);
            }
            status = emitter.emit(expr, pin.name, {});
            if (!status.ok()) {
                return status;
            }
            continue;
        }
        const db::Status status = emitter.add(db::GateType::Blackbox, inputs, pin.name);
        if (!status.ok()) {
            return status;
        }
    }
    return {};
}

db::Status decompose_library(const Library& library, db::Design& design) {
    for (const Cell& cell : library.cells) {
        if (db::find_module(design, cell.name) != nullptr) {
            return db::Status::failure(db::Error::BadPayload,
                                       "design already has a module named '" + cell.name + "'");
        }
        db::Module module;
        const db::Status status = decompose_cell(cell, module);
        if (!status.ok()) {
            return status;
        }
        design.modules.push_back(std::move(module));
    }
    return {};
}

}  // namespace gmax::frontend
