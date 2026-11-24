#include "core/atpg/implication.h"

#include <span>
#include <string>
#include <utility>

namespace gmax::atpg {
namespace {

bool is_controlling_gate(const db::GateType type) {
    return type == db::GateType::And2 || type == db::GateType::Or2 || type == db::GateType::Nand2 ||
           type == db::GateType::Nor2;
}

}  // namespace

ImplicationEngine::ImplicationEngine(const db::FlatNetlist& netlist) : netlist_(netlist) {}

bool ImplicationEngine::is_source_or_state(const db::GateType type) {
    return type == db::GateType::Pi || type == db::GateType::Dff || type == db::GateType::Dlat ||
           type == db::GateType::ScanCell || type == db::GateType::Blackbox;
}

int ImplicationEngine::control_class(const db::GateType type, const Logic5 value) {
    if (!logic5_binary(value)) {
        return 0;
    }
    const bool is_and = type == db::GateType::And2 || type == db::GateType::Nand2;
    const bool is_or = type == db::GateType::Or2 || type == db::GateType::Nor2;
    if (is_and) {
        return value == Logic5::Zero ? 1 : -1;
    }
    if (is_or) {
        return value == Logic5::One ? 1 : -1;
    }
    return 0;
}

db::Status ImplicationEngine::build_consumers() {
    const std::size_t gates = netlist_.gate_count();
    const std::size_t nets = netlist_.net_count();
    if (netlist_.fanin_begin.size() != gates + 1 || netlist_.out_net.size() != gates ||
        netlist_.fanin_begin.empty()) {
        return db::Status::failure(db::Error::BadPayload,
                                   "implication arrays do not match the flat netlist");
    }
    const std::size_t edges = netlist_.fanin_begin.back();
    if (edges > netlist_.fanins.size()) {
        return db::Status::failure(db::Error::BadPayload,
                                   "the flat netlist has an invalid fanin slice");
    }

    consumer_begin_.assign(nets + 1, 0);
    for (std::size_t gate = 0; gate < gates; ++gate) {
        const std::uint32_t begin = netlist_.fanin_begin[gate];
        const std::uint32_t end = netlist_.fanin_begin[gate + 1];
        if (begin > end || static_cast<std::size_t>(end) > netlist_.fanins.size()) {
            return db::Status::failure(db::Error::BadPayload, "gate " + std::to_string(gate) +
                                                                  " has an invalid fanin slice");
        }
        const db::NetId out = netlist_.out_net[gate];
        if (out != db::kInvalidId && static_cast<std::size_t>(out) >= nets) {
            return db::Status::failure(db::Error::BadPayload,
                                       "gate " + std::to_string(gate) + " writes an invalid net");
        }
        for (std::uint32_t i = begin; i < end; ++i) {
            const db::NetId net = netlist_.fanins[i];
            if (static_cast<std::size_t>(net) >= nets) {
                return db::Status::failure(db::Error::BadPayload, "gate " + std::to_string(gate) +
                                                                      " reads an invalid net");
            }
            ++consumer_begin_[net + 1];
        }
    }

    for (std::size_t net = 0; net < nets; ++net) {
        consumer_begin_[net + 1] += consumer_begin_[net];
    }
    consumers_.resize(edges);
    std::vector<std::uint32_t> cursor(consumer_begin_.begin(), consumer_begin_.end() - 1);
    for (std::size_t gate = 0; gate < gates; ++gate) {
        for (std::uint32_t i = netlist_.fanin_begin[gate]; i < netlist_.fanin_begin[gate + 1];
             ++i) {
            const db::NetId net = netlist_.fanins[i];
            consumers_[cursor[net]++] = static_cast<db::GateId>(gate);
        }
    }
    return {};
}

db::Status ImplicationEngine::reset() {
    initialized_ = false;
    const db::Status built = build_consumers();
    if (!built) {
        return built;
    }

    const std::size_t gates = netlist_.gate_count();
    values_.assign(netlist_.net_count(), Logic5::X);
    control_count_.assign(gates, 0);
    noncontrol_count_.assign(gates, 0);
    trail_.clear();
    queue_.clear();
    queue_.reserve(gates);
    queued_.assign(gates, 0);
    forced_.assign(netlist_.net_count(), 0);
    queue_head_ = 0;
    conflict_ = false;
    fanin_values_.clear();

    // Queue every gate once. Ties become constants in implication; gates that
    // read a tie are re-queued if they were evaluated before it.
    for (db::GateId gate = 0; gate < gates; ++gate) {
        queue_.push_back(gate);
        queued_[gate] = 1;
    }
    initialized_ = true;
    const db::Status implied = imply();
    if (!implied) {
        initialized_ = false;
    }
    return implied;
}

db::Status ImplicationEngine::assign(const db::NetId net, const Logic5 value) {
    if (!initialized_) {
        return db::Status::failure(db::Error::BadPayload, "implication engine is not initialized");
    }
    if (static_cast<std::size_t>(net) >= values_.size()) {
        return db::Status::failure(db::Error::BadPayload, "assigned net " + std::to_string(net) +
                                                              " is outside the netlist");
    }
    if (value == Logic5::X) {
        return db::Status::failure(db::Error::BadPayload, "cannot assign X to a net");
    }
    if (!logic5_binary(value)) {
        return db::Status::failure(db::Error::BadPayload,
                                   "assign accepts only 0 or 1; use force_fault for D or Dbar");
    }
    if (conflict_) {
        return {};
    }

    const Logic5 current = values_[net];
    if (current == Logic5::X) {
        commit(net, value, false);
    } else if (current != value) {
        conflict_ = true;
    }
    return {};
}

db::Status ImplicationEngine::force_fault(const db::NetId net, const Logic5 value) {
    if (!initialized_) {
        return db::Status::failure(db::Error::BadPayload, "implication engine is not initialized");
    }
    if (static_cast<std::size_t>(net) >= values_.size()) {
        return db::Status::failure(db::Error::BadPayload,
                                   "fault net " + std::to_string(net) + " is outside the netlist");
    }
    if (value != Logic5::D && value != Logic5::DBar) {
        return db::Status::failure(db::Error::BadPayload,
                                   "a fault site must be forced to D or Dbar");
    }
    if (conflict_) {
        return {};
    }

    const Logic5 current = values_[net];
    if (current == Logic5::X || current == value) {
        commit(net, value, true);
    } else {
        // A binary value, or the other defect value, was derived before the
        // fault was activated. Forcing now would require retracting downstream
        // implications, so report the conflict instead.
        conflict_ = true;
    }
    return {};
}

db::Status ImplicationEngine::imply() {
    if (!initialized_) {
        return db::Status::failure(db::Error::BadPayload, "implication engine is not initialized");
    }
    if (conflict_) {
        return {};
    }

    while (queue_head_ < queue_.size()) {
        const db::GateId gate = queue_[queue_head_++];
        queued_[gate] = 0;
        const db::GateType type = netlist_.type[gate];
        if (is_source_or_state(type)) {
            continue;
        }
        const db::NetId out = netlist_.out_net[gate];
        if (out == db::kInvalidId) {
            continue;
        }

        Logic5 next = Logic5::X;
        if (is_controlling_gate(type)) {
            const std::uint32_t fanin_count =
                netlist_.fanin_begin[gate + 1] - netlist_.fanin_begin[gate];
            if (fanin_count != 2) {
                next = Logic5::X;
            } else if (control_count_[gate] > 0) {
                const bool inverted = type == db::GateType::Nand2 || type == db::GateType::Nor2;
                next = inverted ? Logic5::One : Logic5::Zero;
                if (type == db::GateType::Or2 || type == db::GateType::Nor2) {
                    next = inverted ? Logic5::Zero : Logic5::One;
                }
            } else if (noncontrol_count_[gate] == fanin_count) {
                // All inputs are non-controlling: AND/NOR output 1, while
                // NAND/OR output 0. The OR family is the inverse of the AND
                // family here, not the controlling-value case above.
                next = (type == db::GateType::And2 || type == db::GateType::Nor2) ? Logic5::One
                                                                                  : Logic5::Zero;
            } else {
                next = eval_gate_at(gate);
            }
        } else {
            next = eval_gate_at(gate);
        }

        if (next == Logic5::X) {
            continue;
        }
        if (forced_[out] != 0) {
            if (logic5_good(next) != logic5_good(values_[out])) {
                conflict_ = true;
                return {};
            }
            continue;
        }
        const Logic5 current = values_[out];
        if (current == Logic5::X) {
            commit(out, next, false);
        } else if (current != next) {
            conflict_ = true;
            return {};
        }
    }
    return {};
}

Logic5 ImplicationEngine::value(const db::NetId net) const {
    if (static_cast<std::size_t>(net) >= values_.size()) {
        return Logic5::X;
    }
    return values_[net];
}

ImplicationEngine::Checkpoint ImplicationEngine::checkpoint() const {
    return Checkpoint{trail_.size(), queue_.size(), queue_head_, conflict_};
}

db::Status ImplicationEngine::restore(const Checkpoint& checkpoint) {
    if (!initialized_) {
        return db::Status::failure(db::Error::BadPayload, "implication engine is not initialized");
    }
    if (checkpoint.trail_size > trail_.size() || checkpoint.queue_size > queue_.size() ||
        checkpoint.queue_head > checkpoint.queue_size) {
        return db::Status::failure(db::Error::BadPayload, "checkpoint is not valid for this run");
    }

    for (std::size_t i = trail_.size(); i > checkpoint.trail_size; --i) {
        const TrailEntry& entry = trail_[i - 1];
        const Logic5 current = values_[entry.net];
        values_[entry.net] = entry.old_value;
        forced_[entry.net] = entry.old_forced ? 1 : 0;
        update_counts(entry.net, current, entry.old_value);
    }
    trail_.resize(checkpoint.trail_size);

    for (std::size_t i = checkpoint.queue_size; i < queue_.size(); ++i) {
        queued_[queue_[i]] = 0;
    }
    for (std::size_t i = 0; i < checkpoint.queue_head; ++i) {
        queued_[queue_[i]] = 0;
    }
    for (std::size_t i = checkpoint.queue_head; i < checkpoint.queue_size; ++i) {
        queued_[queue_[i]] = 1;
    }
    queue_.resize(checkpoint.queue_size);
    queue_head_ = checkpoint.queue_head;
    conflict_ = checkpoint.conflict;
    return {};
}

void ImplicationEngine::schedule(const db::GateId gate) {
    if (queued_[gate] != 0) {
        return;
    }
    queued_[gate] = 1;
    queue_.push_back(gate);
}

void ImplicationEngine::update_counts(const db::NetId net, const Logic5 old_value,
                                      const Logic5 new_value) {
    const std::uint32_t begin = consumer_begin_[net];
    const std::uint32_t end = consumer_begin_[net + 1];
    for (std::uint32_t i = begin; i < end; ++i) {
        const db::GateId gate = consumers_[i];
        const db::GateType type = netlist_.type[gate];
        const int old_class = control_class(type, old_value);
        const int new_class = control_class(type, new_value);
        if (old_class == new_class) {
            continue;
        }
        if (old_class > 0 && control_count_[gate] > 0) {
            --control_count_[gate];
        } else if (old_class < 0 && noncontrol_count_[gate] > 0) {
            --noncontrol_count_[gate];
        }
        if (new_class > 0) {
            ++control_count_[gate];
        } else if (new_class < 0) {
            ++noncontrol_count_[gate];
        }
    }
}

void ImplicationEngine::commit(const db::NetId net, const Logic5 value, const bool force) {
    const Logic5 old_value = values_[net];
    const bool old_forced = forced_[net] != 0;
    if (old_value == value && old_forced == force) {
        return;
    }
    trail_.push_back(TrailEntry{net, old_value, old_forced});
    values_[net] = value;
    forced_[net] = force ? 1 : 0;
    if (old_value == value) {
        return;
    }
    update_counts(net, old_value, value);

    const std::uint32_t begin = consumer_begin_[net];
    const std::uint32_t end = consumer_begin_[net + 1];
    for (std::uint32_t i = begin; i < end; ++i) {
        schedule(consumers_[i]);
    }
}

Logic5 ImplicationEngine::eval_gate_at(const db::GateId gate) {
    fanin_values_.clear();
    const std::uint32_t begin = netlist_.fanin_begin[gate];
    const std::uint32_t end = netlist_.fanin_begin[gate + 1];
    fanin_values_.reserve(static_cast<std::size_t>(end - begin));
    for (std::uint32_t i = begin; i < end; ++i) {
        fanin_values_.push_back(values_[netlist_.fanins[i]]);
    }
    return eval_gate(netlist_.type[gate], std::span<const Logic5>(fanin_values_));
}

}  // namespace gmax::atpg
