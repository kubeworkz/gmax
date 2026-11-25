#include "core/atpg/podem.h"

#include <chrono>
#include <cstdint>
#include <initializer_list>
#include <span>
#include <vector>

#include "core/atpg/implication.h"
#include "core/db/status.h"

namespace gmax::atpg {
namespace {

Logic5 activation_value(const sim::StuckValue stuck) {
    return stuck == sim::StuckValue::Zero ? Logic5::D : Logic5::DBar;
}

Logic5 opposite_binary(const Logic5 value) {
    return value == Logic5::One ? Logic5::Zero : Logic5::One;
}

bool is_source_or_state(const db::GateType type) {
    return type == db::GateType::Pi || type == db::GateType::Dff || type == db::GateType::Dlat ||
           type == db::GateType::ScanCell || type == db::GateType::Blackbox;
}

constexpr Logic5 kUnknown = Logic5::X;

// A side input must be a binary value in both circuits. D and DBar expose only
// the good-circuit bit; leaving one on a side input makes the gate output X in
// the faulty circuit, so it cannot satisfy a control objective.
int binary_good(const Logic5 value) {
    if (logic5_binary(value)) {
        return logic5_good(value);
    }
    return value == Logic5::X ? -1 : -2;
}

struct Consumers {
    std::vector<std::uint32_t> begin;
    std::vector<db::GateId> gates;

    bool build(const db::FlatNetlist& netlist) {
        const std::size_t gates_count = netlist.gate_count();
        const std::size_t nets = netlist.net_count();
        if (netlist.fanin_begin.size() != gates_count + 1 ||
            netlist.out_net.size() != gates_count || netlist.fanin_begin.empty()) {
            return false;
        }
        const std::size_t edges = netlist.fanin_begin.back();
        if (edges > netlist.fanins.size()) {
            return false;
        }

        begin.assign(nets + 1, 0);
        for (std::size_t gate = 0; gate < gates_count; ++gate) {
            const std::uint32_t first = netlist.fanin_begin[gate];
            const std::uint32_t last = netlist.fanin_begin[gate + 1];
            if (first > last || static_cast<std::size_t>(last) > netlist.fanins.size()) {
                return false;
            }
            const db::NetId out = netlist.out_net[gate];
            if (out != db::kInvalidId && static_cast<std::size_t>(out) >= nets) {
                return false;
            }
            for (std::uint32_t i = first; i < last; ++i) {
                const db::NetId net = netlist.fanins[i];
                if (static_cast<std::size_t>(net) >= nets) {
                    return false;
                }
                ++begin[net + 1];
            }
        }
        for (std::size_t net = 0; net < nets; ++net) {
            begin[net + 1] += begin[net];
        }
        gates.resize(edges);
        std::vector<std::uint32_t> cursor(begin.begin(), begin.end() - 1);
        for (std::size_t gate = 0; gate < gates_count; ++gate) {
            for (std::uint32_t i = netlist.fanin_begin[gate]; i < netlist.fanin_begin[gate + 1];
                 ++i) {
                const db::NetId net = netlist.fanins[i];
                gates[cursor[net]++] = static_cast<db::GateId>(gate);
            }
        }
        return true;
    }
};

class PodemSearch {
  public:
    PodemSearch(const db::FlatNetlist& netlist, const sim::Fault& fault,
                const std::span<const db::NetId> observables, const PodemBudget budget,
                const std::span<const db::CaseAssignment> constraints)
        : netlist_(netlist), fault_(fault), observables_(observables.begin(), observables.end()),
          constraints_(constraints.begin(), constraints.end()), budget_(budget), engine_(netlist),
          defect_(activation_value(fault.stuck)) {
        valid_ = fault_.net < netlist_.net_count() && consumers_.build(netlist_);
        observable_.assign(netlist_.net_count(), 0);
        for (const db::NetId net : observables_) {
            if (net >= netlist_.net_count()) {
                valid_ = false;
            } else {
                observable_[net] = 1;
            }
        }
        start_ = Clock::now();
    }

    PodemResult run() {
        PodemResult result;
        if (!valid_) {
            return result;
        }
        if (!engine_.reset().ok()) {
            return result;
        }
        // ADR-012 section 4: a case value is a hard assignment made before the
        // fault is activated. The values are assigned but not propagated yet:
        // implying here would decide the fault site from the case value alone and
        // then make activation look like a conflict, which would report a
        // detectable fault as untestable. The fault is forced first and the one
        // implication that follows carries both the case values and the fault,
        // so the engine arbitrates between them. The first checkpoint is taken
        // inside search(), after this prologue, so backtracking can never drop a
        // case value.
        for (const db::CaseAssignment& constraint : constraints_) {
            if (constraint.net >= netlist_.net_count()) {
                return result;
            }
            const Logic5 value = constraint.value ? Logic5::One : Logic5::Zero;
            if (!engine_.assign(constraint.net, value).ok()) {
                return result;
            }
        }
        if (!engine_.force_fault(fault_.net, defect_).ok()) {
            // A case value on the fault site that contradicts the defect conflicts
            // with activation, which ADR-012 calls untestable under constraints
            // rather than aborted.
            if (!constraints_.empty()) {
                result.status = PodemStatus::Untestable;
            }
            return result;
        }
        if (!engine_.imply().ok()) {
            return result;
        }
        if (engine_.conflict()) {
            result.status = PodemStatus::Untestable;
            return result;
        }

        const SearchStatus searched = search(0);
        result.backtracks = backtracks_;
        result.decisions = decisions_;
        if (searched == SearchStatus::Detected) {
            result.status = PodemStatus::Detected;
            result.values.reserve(netlist_.net_count());
            for (db::NetId net = 0; net < netlist_.net_count(); ++net) {
                result.values.push_back(engine_.value(net));
            }
        } else if (searched == SearchStatus::Aborted) {
            result.status = PodemStatus::Aborted;
        } else {
            result.status = PodemStatus::Untestable;
        }
        return result;
    }

  private:
    using Clock = std::chrono::steady_clock;

    enum class SearchStatus { Detected, Failed, Aborted };
    enum class ChoiceStatus { Objective, Detected, Blocked };
    enum class ActivationStatus { Done, Objective, Conflict };
    enum class BacktraceStatus { Assignment, Satisfied, Conflict };
    enum class PropagationStatus { Success, Objective, Blocked };

    struct Objective {
        db::NetId net = db::kInvalidId;
        Logic5 value = Logic5::X;
        bool justify = false;
    };

    struct Backtrace {
        BacktraceStatus status = BacktraceStatus::Conflict;
        db::NetId net = db::kInvalidId;
        Logic5 value = Logic5::X;
    };

    bool over_time() const {
        if (budget_.max_time_ms == 0) {
            return false;
        }
        const auto elapsed =
            std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start_).count();
        return elapsed >= static_cast<long long>(budget_.max_time_ms);
    }

    std::uint32_t fanin_count(const db::GateId gate) const {
        return netlist_.fanin_begin[gate + 1] - netlist_.fanin_begin[gate];
    }

    db::NetId fanin(const db::GateId gate, const std::uint32_t index) const {
        return netlist_.fanins[netlist_.fanin_begin[gate] + index];
    }

    bool is_observable(const db::NetId net) const {
        return net < observable_.size() && observable_[net] != 0;
    }

    bool detected() const {
        for (const db::NetId net : observables_) {
            if (logic5_defect(engine_.value(net))) {
                return true;
            }
        }
        return false;
    }

    Logic5 gate_value(const db::GateId gate) {
        fanin_values_.clear();
        const std::uint32_t first = netlist_.fanin_begin[gate];
        const std::uint32_t last = netlist_.fanin_begin[gate + 1];
        fanin_values_.reserve(static_cast<std::size_t>(last - first));
        for (std::uint32_t i = first; i < last; ++i) {
            fanin_values_.push_back(engine_.value(netlist_.fanins[i]));
        }
        return eval_gate(netlist_.type[gate], std::span<const Logic5>(fanin_values_));
    }

    bool can_justify(const db::NetId net, const Logic5 desired) {
        return backtrace(net, desired, false).status != BacktraceStatus::Conflict;
    }

    ActivationStatus activation_objective(Objective& objective) {
        const Logic5 current = engine_.value(fault_.net);
        if (!logic5_defect(current)) {
            return ActivationStatus::Conflict;
        }

        const std::uint32_t driver = netlist_.driver[fault_.net];
        if (driver == db::kPrimaryInput || driver == db::kInvalidId) {
            return ActivationStatus::Done;
        }
        if (driver >= netlist_.gate_count()) {
            return ActivationStatus::Conflict;
        }
        const db::GateType type = netlist_.type[driver];
        if (is_source_or_state(type)) {
            return ActivationStatus::Done;
        }

        const int actual = logic5_good(gate_value(static_cast<db::GateId>(driver)));
        const int required = logic5_good(defect_);
        if (actual < 0) {
            objective.net = fault_.net;
            objective.value = required == 1 ? Logic5::One : Logic5::Zero;
            objective.justify = true;
            return ActivationStatus::Objective;
        }
        return actual == required ? ActivationStatus::Done : ActivationStatus::Conflict;
    }

    PropagationStatus side_objective(const db::GateId gate, const db::NetId net,
                                     Objective& objective) {
        const db::GateType type = netlist_.type[gate];
        std::uint32_t index = 0;
        std::uint32_t count = 0;
        for (std::uint32_t i = 0; i < fanin_count(gate); ++i) {
            if (fanin(gate, i) == net) {
                index = i;
                ++count;
            }
        }
        if (count == 0) {
            return PropagationStatus::Blocked;
        }

        switch (type) {
        case db::GateType::And2:
        case db::GateType::Nand2: {
            const std::uint32_t fanins = fanin_count(gate);
            if (fanins != 2) {
                return PropagationStatus::Blocked;
            }
            bool needs_objective = false;
            for (std::uint32_t i = 0; i < fanins; ++i) {
                if (i == index && count == 1) {
                    continue;
                }
                const db::NetId side = fanin(gate, i);
                const int good = binary_good(engine_.value(side));
                if (good == -2) {
                    return PropagationStatus::Blocked;
                }
                if (good == 0) {
                    return PropagationStatus::Blocked;
                }
                if (good < 0) {
                    if (!can_justify(side, Logic5::One)) {
                        return PropagationStatus::Blocked;
                    }
                    if (!needs_objective) {
                        objective.net = side;
                        objective.value = Logic5::One;
                        objective.justify = false;
                        needs_objective = true;
                    }
                }
            }
            return needs_objective ? PropagationStatus::Objective : PropagationStatus::Success;
        }
        case db::GateType::Or2:
        case db::GateType::Nor2: {
            const std::uint32_t fanins = fanin_count(gate);
            if (fanins != 2) {
                return PropagationStatus::Blocked;
            }
            bool needs_objective = false;
            for (std::uint32_t i = 0; i < fanins; ++i) {
                if (i == index && count == 1) {
                    continue;
                }
                const db::NetId side = fanin(gate, i);
                const int good = binary_good(engine_.value(side));
                if (good == -2) {
                    return PropagationStatus::Blocked;
                }
                if (good == 0) {
                    return PropagationStatus::Success;
                }
                if (good == 1) {
                    return PropagationStatus::Blocked;
                }
                if (!needs_objective && can_justify(side, Logic5::Zero)) {
                    objective.net = side;
                    objective.value = Logic5::Zero;
                    objective.justify = false;
                    needs_objective = true;
                }
            }
            return needs_objective ? PropagationStatus::Objective : PropagationStatus::Blocked;
        }
        case db::GateType::Xor2:
        case db::GateType::Xnor2: {
            const std::uint32_t fanins = fanin_count(gate);
            if (fanins != 2) {
                return PropagationStatus::Blocked;
            }
            for (std::uint32_t i = 0; i < fanins; ++i) {
                if (i == index && count == 1) {
                    continue;
                }
                const db::NetId side = fanin(gate, i);
                const int good = binary_good(engine_.value(side));
                if (good == -2) {
                    return PropagationStatus::Blocked;
                }
                if (good < 0) {
                    if (!can_justify(side, Logic5::Zero)) {
                        return PropagationStatus::Blocked;
                    }
                    objective.net = side;
                    objective.value = Logic5::Zero;
                    objective.justify = false;
                    return PropagationStatus::Objective;
                }
            }
            return PropagationStatus::Success;
        }
        case db::GateType::Buf:
        case db::GateType::Not:
        case db::GateType::Po:
            return fanin_count(gate) == 1 ? PropagationStatus::Success : PropagationStatus::Blocked;
        case db::GateType::Mux2: {
            if (fanin_count(gate) != 3) {
                return PropagationStatus::Blocked;
            }
            const db::NetId select = fanin(gate, 2);
            const int select_good = binary_good(engine_.value(select));
            if (index == 0) {
                if (select_good == -2) {
                    return PropagationStatus::Blocked;
                }
                if (select_good < 0) {
                    if (!can_justify(select, Logic5::One)) {
                        return PropagationStatus::Blocked;
                    }
                    objective.net = select;
                    objective.value = Logic5::One;
                    objective.justify = false;
                    return PropagationStatus::Objective;
                }
                return select_good == 1 ? PropagationStatus::Success : PropagationStatus::Blocked;
            }
            if (index == 1) {
                if (select_good == -2) {
                    return PropagationStatus::Blocked;
                }
                if (select_good < 0) {
                    if (!can_justify(select, Logic5::Zero)) {
                        return PropagationStatus::Blocked;
                    }
                    objective.net = select;
                    objective.value = Logic5::Zero;
                    objective.justify = false;
                    return PropagationStatus::Objective;
                }
                return select_good == 0 ? PropagationStatus::Success : PropagationStatus::Blocked;
            }
            const db::NetId then_net = fanin(gate, 0);
            const db::NetId else_net = fanin(gate, 1);
            const int then_good = binary_good(engine_.value(then_net));
            const int else_good = binary_good(engine_.value(else_net));
            if (then_good == -2 || else_good == -2) {
                return PropagationStatus::Blocked;
            }
            if (then_good >= 0 && else_good >= 0) {
                return then_good != else_good ? PropagationStatus::Success
                                              : PropagationStatus::Blocked;
            }
            if (then_good < 0 && else_good < 0) {
                // The select value is the fault effect. Either ordering of the
                // data values separates the two paths; try 0 first, then 1 so a
                // data input fixed to the opposite value is still reachable.
                for (const Logic5 value : {Logic5::Zero, Logic5::One}) {
                    if (can_justify(then_net, value)) {
                        objective.net = then_net;
                        objective.value = value;
                        objective.justify = false;
                        return PropagationStatus::Objective;
                    }
                }
                return PropagationStatus::Blocked;
            }
            const db::NetId unknown = then_good < 0 ? then_net : else_net;
            const int known = then_good >= 0 ? then_good : else_good;
            const Logic5 needed = known == 0 ? Logic5::One : Logic5::Zero;
            if (!can_justify(unknown, needed)) {
                return PropagationStatus::Blocked;
            }
            objective.net = unknown;
            objective.value = needed;
            objective.justify = false;
            return PropagationStatus::Objective;
        }
        case db::GateType::Tie0:
        case db::GateType::Tie1:
        case db::GateType::Dff:
        case db::GateType::Dlat:
        case db::GateType::Pi:
        case db::GateType::Blackbox:
        case db::GateType::ScanCell:
            return PropagationStatus::Blocked;
        }
        return PropagationStatus::Blocked;
    }

    PropagationStatus propagate(const db::NetId net, std::vector<std::uint8_t>& visited,
                                Objective& objective) {
        if (visited[net] != 0) {
            return PropagationStatus::Blocked;
        }
        visited[net] = 1;

        const std::uint32_t first = consumers_.begin[net];
        const std::uint32_t last = consumers_.begin[net + 1];
        for (std::uint32_t i = first; i < last; ++i) {
            const db::GateId gate = consumers_.gates[i];
            const db::NetId out = netlist_.out_net[gate];
            if (out == db::kInvalidId) {
                continue;
            }
            if (is_observable(out) && logic5_defect(engine_.value(out))) {
                return PropagationStatus::Success;
            }

            const Logic5 current = engine_.value(out);
            if (logic5_defect(current)) {
                const PropagationStatus downstream = propagate(out, visited, objective);
                if (downstream != PropagationStatus::Blocked) {
                    return downstream;
                }
                continue;
            }
            if (current != kUnknown) {
                continue;
            }

            const PropagationStatus side = side_objective(gate, net, objective);
            if (side == PropagationStatus::Blocked) {
                continue;
            }
            if (side == PropagationStatus::Objective) {
                return PropagationStatus::Objective;
            }
            const PropagationStatus downstream = propagate(out, visited, objective);
            if (downstream != PropagationStatus::Blocked) {
                return downstream;
            }
        }
        // This net may be reachable through another reconvergent path. Clearing
        // the marker on a dead end keeps the X-path search complete; leaving it
        // set made a blocked first path hide every later path to the same net.
        visited[net] = 0;
        return PropagationStatus::Blocked;
    }

    ChoiceStatus choose_objective(Objective& objective) {
        const ActivationStatus activation = activation_objective(objective);
        if (activation == ActivationStatus::Objective) {
            return ChoiceStatus::Objective;
        }
        if (activation == ActivationStatus::Conflict) {
            return ChoiceStatus::Blocked;
        }
        if (detected()) {
            return ChoiceStatus::Detected;
        }

        std::vector<std::uint8_t> visited(netlist_.net_count(), 0);
        const PropagationStatus propagated = propagate(fault_.net, visited, objective);
        if (propagated == PropagationStatus::Success) {
            return ChoiceStatus::Detected;
        }
        if (propagated == PropagationStatus::Objective) {
            return ChoiceStatus::Objective;
        }
        return ChoiceStatus::Blocked;
    }

    Backtrace backtrace_all(const db::GateId gate, const Logic5 desired) {
        const int wanted = logic5_good(desired);
        for (std::uint32_t i = 0; i < fanin_count(gate); ++i) {
            const db::NetId net = fanin(gate, i);
            const Logic5 current = engine_.value(net);
            const int good = logic5_binary(current) ? logic5_good(current) : -1;
            if (good == wanted) {
                continue;
            }
            if (good >= 0) {
                return Backtrace{BacktraceStatus::Conflict, db::kInvalidId, Logic5::X};
            }
            const Backtrace result = backtrace(net, desired, false);
            if (result.status == BacktraceStatus::Assignment) {
                return result;
            }
            if (result.status == BacktraceStatus::Conflict) {
                return result;
            }
        }
        return Backtrace{BacktraceStatus::Satisfied, db::kInvalidId, Logic5::X};
    }

    Backtrace backtrace_any(const db::GateId gate, const Logic5 desired) {
        const int wanted = logic5_good(desired);
        for (std::uint32_t i = 0; i < fanin_count(gate); ++i) {
            const db::NetId net = fanin(gate, i);
            const Logic5 current = engine_.value(net);
            const int good = logic5_binary(current) ? logic5_good(current) : -1;
            if (good == wanted) {
                return Backtrace{BacktraceStatus::Satisfied, db::kInvalidId, Logic5::X};
            }
            if (good >= 0) {
                continue;
            }
            const Backtrace result = backtrace(net, desired, false);
            if (result.status == BacktraceStatus::Assignment) {
                return result;
            }
            if (result.status == BacktraceStatus::Satisfied) {
                return result;
            }
        }
        return Backtrace{BacktraceStatus::Conflict, db::kInvalidId, Logic5::X};
    }

    Backtrace backtrace_gate(const db::GateId gate, const Logic5 desired) {
        const db::GateType type = netlist_.type[gate];
        const std::uint32_t count = fanin_count(gate);
        switch (type) {
        case db::GateType::Buf:
        case db::GateType::Po:
            if (count != 1) {
                return Backtrace{BacktraceStatus::Conflict, db::kInvalidId, Logic5::X};
            }
            return backtrace(fanin(gate, 0), desired, false);
        case db::GateType::Not:
            if (count != 1) {
                return Backtrace{BacktraceStatus::Conflict, db::kInvalidId, Logic5::X};
            }
            return backtrace(fanin(gate, 0), opposite_binary(desired), false);
        case db::GateType::And2:
            if (count != 2) {
                return Backtrace{BacktraceStatus::Conflict, db::kInvalidId, Logic5::X};
            }
            return logic5_good(desired) == 1 ? backtrace_all(gate, Logic5::One)
                                             : backtrace_any(gate, Logic5::Zero);
        case db::GateType::Nand2:
            if (count != 2) {
                return Backtrace{BacktraceStatus::Conflict, db::kInvalidId, Logic5::X};
            }
            return logic5_good(desired) == 0 ? backtrace_all(gate, Logic5::One)
                                             : backtrace_any(gate, Logic5::Zero);
        case db::GateType::Or2:
            if (count != 2) {
                return Backtrace{BacktraceStatus::Conflict, db::kInvalidId, Logic5::X};
            }
            return logic5_good(desired) == 0 ? backtrace_all(gate, Logic5::Zero)
                                             : backtrace_any(gate, Logic5::One);
        case db::GateType::Nor2:
            if (count != 2) {
                return Backtrace{BacktraceStatus::Conflict, db::kInvalidId, Logic5::X};
            }
            return logic5_good(desired) == 1 ? backtrace_all(gate, Logic5::Zero)
                                             : backtrace_any(gate, Logic5::One);
        case db::GateType::Xor2:
        case db::GateType::Xnor2: {
            if (count != 2) {
                return Backtrace{BacktraceStatus::Conflict, db::kInvalidId, Logic5::X};
            }
            const Logic5 base = type == db::GateType::Xnor2 ? opposite_binary(desired) : desired;
            const int wanted = logic5_good(base);
            const db::NetId left = fanin(gate, 0);
            const db::NetId right = fanin(gate, 1);
            const int left_good = logic5_good(engine_.value(left));
            const int right_good = logic5_good(engine_.value(right));
            if (left_good >= 0 && right_good >= 0) {
                return (left_good ^ right_good) == wanted
                           ? Backtrace{BacktraceStatus::Satisfied, db::kInvalidId, Logic5::X}
                           : Backtrace{BacktraceStatus::Conflict, db::kInvalidId, Logic5::X};
            }
            if (left_good >= 0) {
                const int need = left_good ^ wanted;
                return backtrace(right, need == 1 ? Logic5::One : Logic5::Zero, false);
            }
            if (right_good >= 0) {
                const int need = right_good ^ wanted;
                return backtrace(left, need == 1 ? Logic5::One : Logic5::Zero, false);
            }
            Backtrace first = backtrace(left, base, false);
            if (first.status == BacktraceStatus::Assignment) {
                return first;
            }
            return backtrace(right, base, false);
        }
        case db::GateType::Mux2: {
            if (count != 3) {
                return Backtrace{BacktraceStatus::Conflict, db::kInvalidId, Logic5::X};
            }
            const db::NetId then_net = fanin(gate, 0);
            const db::NetId else_net = fanin(gate, 1);
            const db::NetId select = fanin(gate, 2);
            const int select_good = logic5_good(engine_.value(select));
            if (select_good == 1) {
                return backtrace(then_net, desired, false);
            }
            if (select_good == 0) {
                return backtrace(else_net, desired, false);
            }
            const int then_good = logic5_good(engine_.value(then_net));
            const int else_good = logic5_good(engine_.value(else_net));
            const int wanted = logic5_good(desired);
            Logic5 select_value = Logic5::One;
            if (then_good == wanted) {
                select_value = Logic5::One;
            } else if (else_good == wanted) {
                select_value = Logic5::Zero;
            } else if (then_good < 0 && else_good >= 0) {
                select_value = Logic5::Zero;
            } else if (then_good >= 0 && else_good < 0) {
                select_value = Logic5::One;
            } else if (then_good >= 0 && else_good >= 0) {
                return Backtrace{BacktraceStatus::Conflict, db::kInvalidId, Logic5::X};
            }
            return backtrace(select, select_value, false);
        }
        case db::GateType::Tie0:
        case db::GateType::Tie1:
        case db::GateType::Dff:
        case db::GateType::Dlat:
        case db::GateType::Pi:
        case db::GateType::Blackbox:
        case db::GateType::ScanCell:
            return Backtrace{BacktraceStatus::Conflict, db::kInvalidId, Logic5::X};
        }
        return Backtrace{BacktraceStatus::Conflict, db::kInvalidId, Logic5::X};
    }

    Backtrace backtrace(const db::NetId net, const Logic5 desired, const bool ignore_current) {
        const int wanted = logic5_good(desired);
        const Logic5 actual = engine_.value(net);
        const int current = logic5_binary(actual) ? logic5_good(actual) : -1;
        if (!ignore_current) {
            if (current == wanted) {
                return Backtrace{BacktraceStatus::Satisfied, db::kInvalidId, Logic5::X};
            }
            if (actual != Logic5::X) {
                return Backtrace{BacktraceStatus::Conflict, db::kInvalidId, Logic5::X};
            }
        }

        const std::uint32_t driver = netlist_.driver[net];
        if (driver == db::kPrimaryInput) {
            if (current == wanted) {
                return Backtrace{BacktraceStatus::Satisfied, db::kInvalidId, Logic5::X};
            }
            if (actual != Logic5::X) {
                return Backtrace{BacktraceStatus::Conflict, db::kInvalidId, Logic5::X};
            }
            return Backtrace{BacktraceStatus::Assignment, net, desired};
        }
        if (driver == db::kInvalidId || driver >= netlist_.gate_count()) {
            return Backtrace{BacktraceStatus::Conflict, db::kInvalidId, Logic5::X};
        }
        if (is_source_or_state(netlist_.type[driver])) {
            return Backtrace{BacktraceStatus::Conflict, db::kInvalidId, Logic5::X};
        }
        return backtrace_gate(static_cast<db::GateId>(driver), desired);
    }

    SearchStatus search(const std::size_t depth) {
        if (over_time()) {
            return SearchStatus::Aborted;
        }
        if (engine_.conflict()) {
            return SearchStatus::Failed;
        }

        Objective objective;
        Backtrace assignment;
        // A backtrace can report that the objective became satisfied as a
        // consequence of the previous decision, without needing a new PI
        // assignment. Re-evaluate the next objective instead of treating that
        // as a failed branch; otherwise a valid test is discarded and the
        // search explores the remaining PI combinations.
        std::size_t satisfied_retries = 0;
        for (;;) {
            const ChoiceStatus choice = choose_objective(objective);
            if (choice == ChoiceStatus::Detected) {
                return SearchStatus::Detected;
            }
            if (choice == ChoiceStatus::Blocked) {
                return SearchStatus::Failed;
            }

            assignment = backtrace(objective.net, objective.value, objective.justify);
            if (assignment.status == BacktraceStatus::Assignment) {
                break;
            }
            if (assignment.status == BacktraceStatus::Conflict ||
                ++satisfied_retries > netlist_.net_count()) {
                return SearchStatus::Failed;
            }
            if (over_time()) {
                return SearchStatus::Aborted;
            }
        }
        for (std::uint32_t branch = 0; branch < 2; ++branch) {
            if (over_time()) {
                return SearchStatus::Aborted;
            }
            const db::NetId net = assignment.net;
            const Logic5 value = branch == 0 ? assignment.value : opposite_binary(assignment.value);
            const ImplicationEngine::Checkpoint checkpoint = engine_.checkpoint();
            ++decisions_;
            const db::Status assigned = engine_.assign(net, value);
            if (!assigned.ok()) {
                engine_.restore(checkpoint);
                return SearchStatus::Aborted;
            }
            const db::Status implied = engine_.imply();
            if (!implied.ok()) {
                engine_.restore(checkpoint);
                return SearchStatus::Aborted;
            }

            SearchStatus child = SearchStatus::Failed;
            if (!engine_.conflict()) {
                child = search(depth + 1);
            }
            if (child == SearchStatus::Detected) {
                // Keep the successful assignment; restoring here would discard
                // the only state that explains the detection.
                return SearchStatus::Detected;
            }

            const db::Status restored = engine_.restore(checkpoint);
            if (!restored.ok()) {
                return SearchStatus::Aborted;
            }
            if (child == SearchStatus::Aborted) {
                return SearchStatus::Aborted;
            }
            ++backtracks_;
            if (backtracks_ > budget_.max_backtracks) {
                return SearchStatus::Aborted;
            }
        }
        return SearchStatus::Failed;
    }

    const db::FlatNetlist& netlist_;
    const sim::Fault& fault_;
    std::vector<db::NetId> observables_;
    std::vector<db::CaseAssignment> constraints_;
    const PodemBudget budget_;
    ImplicationEngine engine_;
    Consumers consumers_;
    std::vector<std::uint8_t> observable_;
    std::vector<Logic5> fanin_values_;
    const Logic5 defect_;
    bool valid_ = false;
    Clock::time_point start_;
    std::size_t backtracks_ = 0;
    std::size_t decisions_ = 0;
};

}  // namespace

std::string_view podem_status_name(const PodemStatus status) {
    switch (status) {
    case PodemStatus::Detected:
        return "detected";
    case PodemStatus::Untestable:
        return "untestable";
    case PodemStatus::Aborted:
        return "aborted";
    }
    return "unknown";
}

PodemResult run_podem(const db::FlatNetlist& netlist, const sim::Fault& fault,
                      const std::span<const db::NetId> observables, const PodemBudget budget,
                      const std::span<const db::CaseAssignment> constraints) {
    PodemSearch search(netlist, fault, observables, budget, constraints);
    return search.run();
}

}  // namespace gmax::atpg
