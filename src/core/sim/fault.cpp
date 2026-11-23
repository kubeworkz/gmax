#include "core/sim/fault.h"

#include <algorithm>
#include <bit>
#include <numeric>
#include <string>
#include <utility>
#include <vector>

namespace gmax::sim {
namespace {

bool is_state_or_source(const db::GateType type) {
    return type == db::GateType::Pi || type == db::GateType::Dff || type == db::GateType::Dlat ||
           type == db::GateType::ScanCell;
}

Logic4 stuck_logic(const StuckValue value) {
    return value == StuckValue::One ? Logic4::One : Logic4::Zero;
}

bool observable_difference(const Logic4 good, const Logic4 faulty) {
    // Both sides must be known. An X on either side is not evidence that a
    // tester could see a difference (architecture 9.1's conservative rule).
    return logic4_known(good) && logic4_known(faulty) && good != faulty;
}

}  // namespace

std::string_view stuck_value_name(const StuckValue value) {
    return value == StuckValue::One ? "sa1" : "sa0";
}

db::Status simulate_scalar(const db::FlatNetlist& netlist, const std::span<Logic4> values,
                           const std::span<const db::NetId> observables,
                           const std::span<const Fault> faults,
                           const std::span<FaultResult> results) {
    const std::size_t nets = netlist.net_count();
    if (results.size() != faults.size()) {
        return db::Status::failure(db::Error::BadPayload,
                                   "scalar fault simulation needs one result per fault");
    }
    for (const db::NetId net : observables) {
        if (net >= nets) {
            return db::Status::failure(db::Error::BadPayload, "observable net " +
                                                                  std::to_string(net) +
                                                                  " is outside the netlist");
        }
    }
    for (const Fault& fault : faults) {
        if (fault.net >= nets) {
            return db::Status::failure(db::Error::BadPayload, "fault net " +
                                                                  std::to_string(fault.net) +
                                                                  " is outside the netlist");
        }
    }

    const db::Status evaluated = evaluate(netlist, values);
    if (!evaluated) {
        return evaluated;
    }

    for (FaultResult& result : results) {
        result = FaultResult{};
    }

    // The scalar reference intentionally walks every gate for every fault: it is
    // the independent oracle, so it must not share the word engine's event-queue
    // or fault-dropping logic.
    std::vector<db::GateId> order(netlist.gate_count());
    std::iota(order.begin(), order.end(), db::GateId{0});
    std::stable_sort(order.begin(), order.end(),
                     [&netlist](const db::GateId left, const db::GateId right) {
                         if (netlist.level[left] != netlist.level[right]) {
                             return netlist.level[left] < netlist.level[right];
                         }
                         return left < right;
                     });

    std::vector<Logic4> faulty;
    std::vector<Logic4> fanins;
    for (std::size_t index = 0; index < faults.size(); ++index) {
        const Fault& fault = faults[index];
        faulty.assign(values.begin(), values.end());
        faulty[fault.net] = stuck_logic(fault.stuck);

        for (const db::GateId gate : order) {
            const db::GateType type = netlist.type[gate];
            if (is_state_or_source(type)) {
                continue;
            }
            fanins.clear();
            for (std::uint32_t i = netlist.fanin_begin[gate]; i < netlist.fanin_begin[gate + 1];
                 ++i) {
                fanins.push_back(faulty[netlist.fanins[i]]);
            }
            const db::NetId out = netlist.out_net[gate];
            if (out == db::kInvalidId) {
                continue;
            }
            faulty[out] = eval_gate(type, std::span<const Logic4>(fanins));
            // A fault on a gate output is re-forced after its driver evaluates.
            if (out == fault.net) {
                faulty[out] = stuck_logic(fault.stuck);
            }
        }

        for (const db::NetId net : observables) {
            if (observable_difference(values[net], faulty[net])) {
                results[index].state = FaultState::Detected;
                break;
            }
        }
    }
    return {};
}

FaultSimulator::FaultSimulator(const db::FlatNetlist& netlist, const std::span<const Fault> faults,
                               const std::span<const db::NetId> observables)
    : netlist_(netlist), faults_(faults), observables_(observables.begin(), observables.end()),
      results_(faults.size()) {
    observable_.assign(netlist.net_count(), 0);
    for (const db::NetId net : observables_) {
        if (net < netlist.net_count()) {
            observable_[net] = 1;
        }
    }
    faulty_.resize(netlist.net_count());
    stamp_.assign(netlist.net_count(), 0);
    queued_.assign(netlist.gate_count(), 0);
    build_consumers();
}

db::Status FaultSimulator::simulate(const std::span<Logic4> values) {
    const std::size_t nets = netlist_.net_count();
    for (const db::NetId net : observables_) {
        if (net >= nets) {
            return db::Status::failure(db::Error::BadPayload, "observable net " +
                                                                  std::to_string(net) +
                                                                  " is outside the netlist");
        }
    }
    for (const Fault& fault : faults_) {
        if (fault.net >= nets) {
            return db::Status::failure(db::Error::BadPayload, "fault net " +
                                                                  std::to_string(fault.net) +
                                                                  " is outside the netlist");
        }
    }

    const db::Status evaluated = evaluate(netlist_, values);
    if (!evaluated) {
        return evaluated;
    }

    // Groups are fixed chunks of the fault list, so a fault's lane never moves.
    // Chunks whose faults have all been dropped are skipped without touching the
    // faulty planes.
    for (std::size_t first = 0; first < faults_.size(); first += kFaultsPerWord) {
        grade_group(first, values);
    }
    ++patterns_;
    return {};
}

void FaultSimulator::build_consumers() {
    const std::size_t gates = netlist_.gate_count();
    const std::size_t nets = netlist_.net_count();
    if (netlist_.fanin_begin.size() != gates + 1 ||
        static_cast<std::size_t>(netlist_.fanin_begin.back()) > netlist_.fanins.size()) {
        return;
    }

    consumer_begin_.assign(nets + 1, 0);
    std::uint64_t edges = 0;
    for (std::size_t gate = 0; gate < gates; ++gate) {
        for (std::uint32_t i = netlist_.fanin_begin[gate]; i < netlist_.fanin_begin[gate + 1];
             ++i) {
            const db::NetId net = netlist_.fanins[i];
            if (net >= nets) {
                consumer_begin_.clear();
                consumers_.clear();
                return;
            }
            ++consumer_begin_[net + 1];
            ++edges;
        }
    }
    for (std::size_t net = 0; net < nets; ++net) {
        consumer_begin_[net + 1] += consumer_begin_[net];
    }
    consumers_.resize(static_cast<std::size_t>(edges));
    std::vector<std::uint32_t> cursor(consumer_begin_.begin(), consumer_begin_.end() - 1);
    // Gates are visited in id order, so each net's consumer list is sorted by
    // gate id and queue order never depends on hash iteration.
    for (std::size_t gate = 0; gate < gates; ++gate) {
        for (std::uint32_t i = netlist_.fanin_begin[gate]; i < netlist_.fanin_begin[gate + 1];
             ++i) {
            const db::NetId net = netlist_.fanins[i];
            consumers_[cursor[net]++] = static_cast<db::GateId>(gate);
        }
    }
}

LogicWord FaultSimulator::read_net(const db::NetId net, const std::span<const Logic4> good) const {
    if (stamp_[net] == generation_) {
        return faulty_[net];
    }
    // An untouched net is still identical to the good machine in every lane.
    return LogicWord::splat(good[net]);
}

void FaultSimulator::store_net(const db::NetId net, const LogicWord word) {
    stamp_[net] = generation_;
    faulty_[net] = word;
}

const FaultSimulator::Site* FaultSimulator::find_site(const db::NetId net) const {
    const auto it =
        std::lower_bound(sites_.begin(), sites_.end(), net,
                         [](const Site& site, const db::NetId value) { return site.net < value; });
    if (it == sites_.end() || it->net != net) {
        return nullptr;
    }
    return &*it;
}

void FaultSimulator::apply_force(LogicWord& word, const Site& site) {
    const std::uint64_t mask = site.zero_mask | site.one_mask;
    word.known |= mask;
    word.value = (word.value & ~mask) | site.one_mask;
}

std::uint64_t FaultSimulator::detect(const db::NetId net, const std::size_t first,
                                     const std::span<const Logic4> good, const LogicWord word,
                                     std::uint64_t active) {
    if (active == 0 || observable_[net] == 0) {
        return active;
    }
    const Logic4 good_value = good[net];
    if (!logic4_known(good_value)) {
        return active;
    }

    // A lane is detected when its faulty value is known and differs from the
    // known good value at an observable net.
    const std::uint64_t differs =
        (good_value == Logic4::One ? ~word.value : word.value) & word.known;
    std::uint64_t hits = active & differs;
    while (hits != 0) {
        const unsigned lane = std::countr_zero(hits);
        const std::size_t index = first + lane;
        results_[index].state = FaultState::Detected;
        results_[index].pattern = patterns_;
        ++detected_;
        hits &= hits - 1;
        active &= ~(std::uint64_t{1} << lane);
    }
    return active;
}

void FaultSimulator::grade_group(const std::size_t first, const std::span<const Logic4> good) {
    const std::size_t count = std::min(kFaultsPerWord, faults_.size() - first);
    std::uint64_t active = 0;
    for (std::size_t lane = 0; lane < count; ++lane) {
        if (results_[first + lane].state == FaultState::Undetected) {
            active |= std::uint64_t{1} << lane;
        }
    }
    if (active == 0) {
        return;
    }

    // Generation stamps make the faulty planes logically empty without an
    // O(nets) clear per group. An untouched net reads as the good machine.
    ++generation_;
    if (generation_ == 0) {
        std::fill(stamp_.begin(), stamp_.end(), 0);
        generation_ = 1;
    }

    sites_.clear();
    for (std::size_t lane = 0; lane < count; ++lane) {
        if ((active & (std::uint64_t{1} << lane)) == 0) {
            continue;
        }
        const Fault& fault = faults_[first + lane];
        auto site = std::find_if(sites_.begin(), sites_.end(), [&fault](const Site& candidate) {
            return candidate.net == fault.net;
        });
        if (site == sites_.end()) {
            sites_.push_back(Site{fault.net, 0, 0});
            site = sites_.end() - 1;
        }
        const std::uint64_t bit = std::uint64_t{1} << lane;
        if (fault.stuck == StuckValue::One) {
            site->one_mask |= bit;
        } else {
            site->zero_mask |= bit;
        }
    }
    std::sort(sites_.begin(), sites_.end(),
              [](const Site& left, const Site& right) { return left.net < right.net; });

    // Level order with a gate-id tie-break, so 1-thread and N-thread runs visit
    // gates identically. A gate is queued at most once while pending; all of its
    // fanins are strictly lower level and therefore settle before it is popped.
    const auto later = [this](const db::GateId left, const db::GateId right) {
        if (netlist_.level[left] != netlist_.level[right]) {
            return netlist_.level[left] > netlist_.level[right];
        }
        return left > right;
    };
    const auto enqueue = [this, &later](const db::GateId gate) {
        if (queued_[gate] != 0) {
            return;
        }
        queued_[gate] = 1;
        events_.push_back(gate);
        std::push_heap(events_.begin(), events_.end(), later);
    };
    const auto enqueue_fanouts = [this, &enqueue](const db::NetId net) {
        if (consumer_begin_.empty() || net + 1 >= consumer_begin_.size()) {
            return;
        }
        for (std::uint32_t i = consumer_begin_[net]; i < consumer_begin_[net + 1]; ++i) {
            enqueue(consumers_[i]);
        }
    };

    // Force every fault site before propagation, so a fault on a gate output is
    // active even when none of that gate's fanins changed.
    for (const Site& site : sites_) {
        const LogicWord before = read_net(site.net, good);
        LogicWord after = before;
        apply_force(after, site);
        if (after == before) {
            continue;
        }
        store_net(site.net, after);
        active = detect(site.net, first, good, after, active);
        enqueue_fanouts(site.net);
    }

    while (!events_.empty()) {
        std::pop_heap(events_.begin(), events_.end(), later);
        const db::GateId gate = events_.back();
        events_.pop_back();
        queued_[gate] = 0;

        const db::GateType type = netlist_.type[gate];
        if (is_state_or_source(type)) {
            continue;
        }

        fanin_values_.clear();
        for (std::uint32_t i = netlist_.fanin_begin[gate]; i < netlist_.fanin_begin[gate + 1];
             ++i) {
            fanin_values_.push_back(read_net(netlist_.fanins[i], good));
        }

        const db::NetId out = netlist_.out_net[gate];
        if (out == db::kInvalidId) {
            continue;
        }
        const LogicWord before = read_net(out, good);
        LogicWord after = eval_gate(type, std::span<const LogicWord>(fanin_values_));
        if (const Site* site = find_site(out); site != nullptr) {
            apply_force(after, *site);
        }
        if (after == before) {
            continue;
        }
        store_net(out, after);
        active = detect(out, first, good, after, active);
        enqueue_fanouts(out);
    }
}

}  // namespace gmax::sim
