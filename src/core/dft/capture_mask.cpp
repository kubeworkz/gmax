#include "core/dft/capture_mask.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gmax::dft {
namespace {

using db::ConstraintClock;
using db::FlatNetlist;
using db::GateType;
using db::NetId;
using db::StringTable;

bool state_holding(const GateType type) {
    return type == GateType::Dff || type == GateType::Dlat || type == GateType::ScanCell;
}

// A state element's second fanin is the clock or gate it samples, the order
// db::clock_gate_counts and the crossing pass already read.
NetId sampled_clock(const FlatNetlist& flat, const std::uint32_t gate) {
    const std::uint32_t begin = flat.fanin_begin[gate];
    return begin + 1 < flat.fanin_begin[gate + 1] ? flat.fanins[begin + 1] : db::kInvalidId;
}

NetId net_named(const FlatNetlist& flat, const StringTable& strings, const std::string& name) {
    if (name.empty()) {
        return db::kInvalidId;
    }
    for (NetId net = 0; net < flat.net_count(); ++net) {
        if (strings.at(flat.name_id[net]) == name) {
            return net;
        }
    }
    return db::kInvalidId;
}

// The net a declared clock drives is the domain key, as in db::clock_gate_counts.
NetId clock_net(const db::Constraints& constraints, const FlatNetlist& flat,
                const StringTable& strings, const std::string& name) {
    for (const ConstraintClock& clock : constraints.clocks) {
        if (clock.name == name) {
            return net_named(flat, strings, clock.source);
        }
    }
    return db::kInvalidId;
}

}  // namespace

CaptureMask apply_capture_mask(const CaptureProcedure& procedure,
                               const db::Constraints& constraints, const db::FlatNetlist& flat,
                               const db::StringTable& strings,
                               const std::span<const db::NetId> observables) {
    CaptureMask mask;
    // Section 2 only asks about a domain a non-final cycle captures, so the final
    // cycle never masks anything and a stale entry there is ignored like the payload
    // ignores it.
    for (std::size_t c = 0; c + 1 < procedure.cycles.size(); ++c) {
        const CaptureCycle& cycle = procedure.cycles[c];
        for (const CaptureGroup& group : cycle.groups) {
            for (const std::string& clock : group.timing.clocks) {
                if (capture_intermediate_state(cycle, clock) == CaptureIntermediateState::Masked) {
                    mask.domains.push_back(clock);
                }
            }
        }
    }
    std::sort(mask.domains.begin(), mask.domains.end());
    mask.domains.erase(std::unique(mask.domains.begin(), mask.domains.end()), mask.domains.end());
    mask.observables.assign(observables.begin(), observables.end());
    if (mask.domains.empty()) {
        return mask;
    }

    // One consumer index for the whole walk: net -> the gates that read it.
    std::vector<std::vector<std::uint32_t>> consumers(flat.net_count());
    for (std::uint32_t gate = 0; gate < flat.gate_count(); ++gate) {
        for (std::uint32_t i = flat.fanin_begin[gate]; i < flat.fanin_begin[gate + 1]; ++i) {
            const NetId net = flat.fanins[i];
            if (net != db::kInvalidId && net < flat.net_count()) {
                consumers[net].push_back(gate);
            }
        }
    }

    // The masked value starts at every state element that samples the domain's
    // clock, latch included: a lockup latch carries the same value forward.
    std::vector<char> forced(flat.net_count(), 0);
    std::vector<NetId> pending;
    for (const std::string& domain : mask.domains) {
        const NetId domain_net = clock_net(constraints, flat, strings, domain);
        if (domain_net == db::kInvalidId) {
            continue;
        }
        for (std::uint32_t gate = 0; gate < flat.gate_count(); ++gate) {
            if (!state_holding(flat.type[gate]) || sampled_clock(flat, gate) != domain_net) {
                continue;
            }
            const NetId held = flat.out_net[gate];
            if (!forced[held]) {
                forced[held] = 1;
                pending.push_back(held);
            }
        }
    }
    while (!pending.empty()) {
        const NetId net = pending.back();
        pending.pop_back();
        for (const std::uint32_t gate : consumers[net]) {
            if (state_holding(flat.type[gate])) {
                continue;
            }
            const NetId out = flat.out_net[gate];
            if (!forced[out]) {
                forced[out] = 1;
                pending.push_back(out);
            }
        }
    }

    mask.observables.clear();
    for (const NetId net : observables) {
        if (net == db::kInvalidId || net >= flat.net_count() || !forced[net]) {
            mask.observables.push_back(net);
        }
    }
    return mask;
}

}  // namespace gmax::dft
