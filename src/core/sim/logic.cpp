#include "core/sim/logic.h"

#include <algorithm>
#include <numeric>
#include <span>
#include <string>
#include <vector>

namespace gmax::sim {
namespace {

bool is_stateful(const db::GateType type) {
    return type == db::GateType::Dff || type == db::GateType::Dlat ||
           type == db::GateType::ScanCell;
}

bool is_source_or_state(const db::GateType type) {
    return type == db::GateType::Pi || is_stateful(type);
}

Logic4 invert_scalar(Logic4 value) {
    value = logic4_normalized(value);
    if (value == Logic4::Zero) {
        return Logic4::One;
    }
    if (value == Logic4::One) {
        return Logic4::Zero;
    }
    return Logic4::X;
}

Logic4 and_scalar(Logic4 left, Logic4 right) {
    left = logic4_normalized(left);
    right = logic4_normalized(right);
    if ((logic4_known(left) && left == Logic4::Zero) ||
        (logic4_known(right) && right == Logic4::Zero)) {
        return Logic4::Zero;
    }
    if (logic4_known(left) && logic4_known(right)) {
        return Logic4::One;
    }
    return Logic4::X;
}

Logic4 or_scalar(Logic4 left, Logic4 right) {
    left = logic4_normalized(left);
    right = logic4_normalized(right);
    if ((logic4_known(left) && left == Logic4::One) ||
        (logic4_known(right) && right == Logic4::One)) {
        return Logic4::One;
    }
    if (logic4_known(left) && logic4_known(right)) {
        return Logic4::Zero;
    }
    return Logic4::X;
}

Logic4 xor_scalar(Logic4 left, Logic4 right) {
    left = logic4_normalized(left);
    right = logic4_normalized(right);
    if (!logic4_known(left) || !logic4_known(right)) {
        return Logic4::X;
    }
    return left == right ? Logic4::Zero : Logic4::One;
}

Logic4 mux_scalar(Logic4 then_value, Logic4 else_value, Logic4 select) {
    then_value = logic4_normalized(then_value);
    else_value = logic4_normalized(else_value);
    select = logic4_normalized(select);
    // An unknown select propagates X even when both data inputs agree. MUX2 is
    // a control primitive; the architecture's AND/OR expression is a gate-level
    // sketch, not its four-state truth table (that form is data-order dependent).
    if (!logic4_known(select)) {
        return Logic4::X;
    }
    return select == Logic4::One ? then_value : else_value;
}

LogicWord canonical(LogicWord word) {
    word.value &= word.known;
    return word;
}

LogicWord word_invert(LogicWord input) {
    input = canonical(input);
    return LogicWord{input.known, (~input.value) & input.known};
}

LogicWord word_and(LogicWord left, LogicWord right) {
    left = canonical(left);
    right = canonical(right);
    LogicWord out;
    out.known =
        (left.known & right.known) | (left.known & ~left.value) | (right.known & ~right.value);
    out.value = (left.value & right.value) & out.known;
    return out;
}

LogicWord word_or(LogicWord left, LogicWord right) {
    left = canonical(left);
    right = canonical(right);
    LogicWord out;
    out.known =
        (left.known & right.known) | (left.known & left.value) | (right.known & right.value);
    out.value = (left.value | right.value) & out.known;
    return out;
}

LogicWord word_xor(LogicWord left, LogicWord right) {
    left = canonical(left);
    right = canonical(right);
    LogicWord out;
    out.known = left.known & right.known;
    out.value = (left.value ^ right.value) & out.known;
    return out;
}

LogicWord word_mux(LogicWord then_value, LogicWord else_value, LogicWord select) {
    then_value = canonical(then_value);
    else_value = canonical(else_value);
    select = canonical(select);

    const std::uint64_t select_zero = select.known & ~select.value;
    const std::uint64_t select_one = select.known & select.value;

    LogicWord out;
    out.known = (else_value.known & select_zero) | (then_value.known & select_one);
    out.value = (else_value.value & select_zero) | (then_value.value & select_one);
    out.value &= out.known;
    return out;
}

}  // namespace

LogicWord LogicWord::splat(const Logic4 value) {
    switch (value) {
    case Logic4::Zero:
        return LogicWord{~std::uint64_t{0}, 0};
    case Logic4::One:
        return LogicWord{~std::uint64_t{0}, ~std::uint64_t{0}};
    case Logic4::X:
    case Logic4::Z:
        return LogicWord{};
    }
    return LogicWord{};
}

Logic4 LogicWord::lane(const std::size_t index) const {
    if (index >= kLanes) {
        return Logic4::X;
    }
    const std::uint64_t bit = std::uint64_t{1} << index;
    if ((known & bit) == 0) {
        return Logic4::X;
    }
    return (value & bit) != 0 ? Logic4::One : Logic4::Zero;
}

void LogicWord::set_lane(const std::size_t index, const Logic4 value) {
    if (index >= kLanes) {
        return;
    }
    const std::uint64_t bit = std::uint64_t{1} << index;
    known &= ~bit;
    this->value &= ~bit;
    if (value == Logic4::Zero) {
        known |= bit;
    } else if (value == Logic4::One) {
        known |= bit;
        this->value |= bit;
    }
}

std::string_view logic4_name(const Logic4 value) {
    switch (value) {
    case Logic4::Zero:
        return "0";
    case Logic4::One:
        return "1";
    case Logic4::X:
        return "X";
    case Logic4::Z:
        return "Z";
    }
    return "?";
}

Logic4 eval_gate(const db::GateType type, const std::span<const Logic4> fanins) {
    switch (type) {
    case db::GateType::And2:
        return fanins.size() == 2 ? and_scalar(fanins[0], fanins[1]) : Logic4::X;
    case db::GateType::Or2:
        return fanins.size() == 2 ? or_scalar(fanins[0], fanins[1]) : Logic4::X;
    case db::GateType::Nand2:
        return fanins.size() == 2 ? invert_scalar(and_scalar(fanins[0], fanins[1])) : Logic4::X;
    case db::GateType::Nor2:
        return fanins.size() == 2 ? invert_scalar(or_scalar(fanins[0], fanins[1])) : Logic4::X;
    case db::GateType::Xor2:
        return fanins.size() == 2 ? xor_scalar(fanins[0], fanins[1]) : Logic4::X;
    case db::GateType::Xnor2:
        return fanins.size() == 2 ? invert_scalar(xor_scalar(fanins[0], fanins[1])) : Logic4::X;
    case db::GateType::Not:
        return fanins.size() == 1 ? invert_scalar(fanins[0]) : Logic4::X;
    case db::GateType::Buf:
    case db::GateType::Po:
        return fanins.size() == 1 ? logic4_normalized(fanins[0]) : Logic4::X;
    case db::GateType::Mux2:
        return fanins.size() == 3 ? mux_scalar(fanins[0], fanins[1], fanins[2]) : Logic4::X;
    case db::GateType::Tie0:
        return fanins.empty() ? Logic4::Zero : Logic4::X;
    case db::GateType::Tie1:
        return fanins.empty() ? Logic4::One : Logic4::X;
    case db::GateType::Dff:
    case db::GateType::Dlat:
    case db::GateType::Pi:
    case db::GateType::Blackbox:
    case db::GateType::ScanCell:
        return Logic4::X;
    }
    return Logic4::X;
}

LogicWord eval_gate(const db::GateType type, const std::span<const LogicWord> fanins) {
    switch (type) {
    case db::GateType::And2:
        return fanins.size() == 2 ? word_and(fanins[0], fanins[1]) : LogicWord{};
    case db::GateType::Or2:
        return fanins.size() == 2 ? word_or(fanins[0], fanins[1]) : LogicWord{};
    case db::GateType::Nand2:
        return fanins.size() == 2 ? word_invert(word_and(fanins[0], fanins[1])) : LogicWord{};
    case db::GateType::Nor2:
        return fanins.size() == 2 ? word_invert(word_or(fanins[0], fanins[1])) : LogicWord{};
    case db::GateType::Xor2:
        return fanins.size() == 2 ? word_xor(fanins[0], fanins[1]) : LogicWord{};
    case db::GateType::Xnor2:
        return fanins.size() == 2 ? word_invert(word_xor(fanins[0], fanins[1])) : LogicWord{};
    case db::GateType::Not:
        return fanins.size() == 1 ? word_invert(fanins[0]) : LogicWord{};
    case db::GateType::Buf:
    case db::GateType::Po:
        return fanins.size() == 1 ? canonical(fanins[0]) : LogicWord{};
    case db::GateType::Mux2:
        return fanins.size() == 3 ? word_mux(fanins[0], fanins[1], fanins[2]) : LogicWord{};
    case db::GateType::Tie0:
        return fanins.empty() ? LogicWord::splat(Logic4::Zero) : LogicWord{};
    case db::GateType::Tie1:
        return fanins.empty() ? LogicWord::splat(Logic4::One) : LogicWord{};
    case db::GateType::Dff:
    case db::GateType::Dlat:
    case db::GateType::Pi:
    case db::GateType::Blackbox:
    case db::GateType::ScanCell:
        return LogicWord{};
    }
    return LogicWord{};
}

namespace {

template <typename Value>
db::Status evaluate_impl(const db::FlatNetlist& netlist, const std::span<Value> values) {
    const std::size_t gates = netlist.gate_count();
    if (netlist.fanin_begin.size() != gates + 1 || netlist.out_net.size() != gates ||
        netlist.level.size() != gates || netlist.driver.size() != values.size() ||
        netlist.fanin_begin.empty() ||
        static_cast<std::size_t>(netlist.fanin_begin.back()) > netlist.fanins.size()) {
        return db::Status::failure(db::Error::BadPayload,
                                   "simulation arrays do not match the flat netlist");
    }

    // Levels are stable under flatten(); a level-order walk therefore evaluates
    // every combinational fanin before its consumer without assuming that gate
    // ids happen to be topologically sorted.
    std::vector<db::GateId> order(gates);
    std::iota(order.begin(), order.end(), db::GateId{0});
    std::stable_sort(order.begin(), order.end(),
                     [&netlist](const db::GateId left, const db::GateId right) {
                         if (netlist.level[left] != netlist.level[right]) {
                             return netlist.level[left] < netlist.level[right];
                         }
                         return left < right;
                     });

    std::vector<Value> fanin_values;
    for (const db::GateId gate : order) {
        const db::GateType type = netlist.type[gate];
        if (is_source_or_state(type)) {
            continue;
        }
        const std::uint32_t begin = netlist.fanin_begin[gate];
        const std::uint32_t end = netlist.fanin_begin[gate + 1];
        if (begin > end || static_cast<std::size_t>(end) > netlist.fanins.size()) {
            return db::Status::failure(db::Error::BadPayload, "gate " + std::to_string(gate) +
                                                                  " has an invalid fanin slice");
        }

        fanin_values.clear();
        fanin_values.reserve(static_cast<std::size_t>(end - begin));
        for (std::uint32_t i = begin; i < end; ++i) {
            const db::NetId net = netlist.fanins[i];
            if (static_cast<std::size_t>(net) >= values.size()) {
                return db::Status::failure(db::Error::BadPayload, "gate " + std::to_string(gate) +
                                                                      " reads invalid net " +
                                                                      std::to_string(net));
            }
            const std::uint32_t driver = netlist.driver[net];
            if (driver != db::kPrimaryInput && driver != db::kInvalidId) {
                if (static_cast<std::size_t>(driver) >= gates) {
                    return db::Status::failure(db::Error::BadPayload,
                                               "net " + std::to_string(net) +
                                                   " names an invalid driver");
                }
                if (!is_source_or_state(netlist.type[driver]) &&
                    netlist.level[driver] >= netlist.level[gate]) {
                    return db::Status::failure(db::Error::CycleDetected,
                                               "gate " + std::to_string(gate) +
                                                   " depends on gate " + std::to_string(driver) +
                                                   " at the same or a later level");
                }
            }
            fanin_values.push_back(values[net]);
        }

        const db::NetId out = netlist.out_net[gate];
        if (out == db::kInvalidId) {
            continue;
        }
        if (static_cast<std::size_t>(out) >= values.size()) {
            return db::Status::failure(db::Error::BadPayload, "gate " + std::to_string(gate) +
                                                                  " writes invalid net " +
                                                                  std::to_string(out));
        }
        values[out] = eval_gate(type, std::span<const Value>(fanin_values));
    }
    return {};
}

}  // namespace

db::Status evaluate(const db::FlatNetlist& netlist, const std::span<Logic4> values) {
    return evaluate_impl(netlist, values);
}

db::Status evaluate(const db::FlatNetlist& netlist, const std::span<LogicWord> values) {
    return evaluate_impl(netlist, values);
}

}  // namespace gmax::sim
