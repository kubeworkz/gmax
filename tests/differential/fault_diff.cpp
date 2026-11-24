// G0.7 differential evidence: the 64-lane fault simulator against the scalar
// reference on every net of the loaded design. The fault set is the raw
// stuck-at list, two faults per net, so no net is skipped. Patterns are
// deterministic: all-zero, all-one, an alternating pattern, an X-seeded
// pattern, then a fixed-seed pseudo-random sequence. The first pattern that
// detects a fault must agree exactly between the two engines; any disagreement
// is a failure, not a warning.

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/atpg/fault_list.h"
#include "core/db/db.h"
#include "core/db/design.h"
#include "core/db/status.h"
#include "core/sim/fault.h"
#include "core/sim/logic.h"

namespace atpg = gmax::atpg;
namespace db = gmax::db;
namespace sim = gmax::sim;

namespace {

constexpr std::uint64_t kDefaultPatterns = 256;
constexpr std::uint64_t kMaxPatterns = 4096;

std::uint64_t splitmix(std::uint64_t& state) {
    state += 0x9e3779b97f4a7c15ULL;
    std::uint64_t value = state;
    value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31);
}

db::NetId net_named(const db::DesignDb& database, const std::string_view name) {
    const db::FlatNetlist& flat = database.flat();
    for (db::NetId net = 0; net < flat.net_count(); ++net) {
        if (database.strings().at(flat.name_id[net]) == name) {
            return net;
        }
    }
    return db::kInvalidId;
}

std::string net_name(const db::DesignDb& database, const db::NetId net) {
    const db::FlatNetlist& flat = database.flat();
    if (net >= flat.net_count() || flat.name_id[net] >= database.strings().size()) {
        return {};
    }
    return std::string(database.strings().at(flat.name_id[net]));
}

sim::Logic4 bit(const bool value) { return value ? sim::Logic4::One : sim::Logic4::Zero; }

// The first four patterns are canonical and the rest are pseudo-random, all
// derived from the pattern index and input index only.
void seed_pattern(const std::uint64_t pattern, const std::span<const db::NetId> inputs,
                  std::span<sim::Logic4> values, std::uint64_t& random_state) {
    for (std::size_t index = 0; index < inputs.size(); ++index) {
        sim::Logic4 value = sim::Logic4::Zero;
        switch (pattern) {
        case 0:
            value = sim::Logic4::Zero;
            break;
        case 1:
            value = sim::Logic4::One;
            break;
        case 2:
            value = bit(index % 2 != 0);
            break;
        case 3:
            // X on a third of the inputs exercises the four-state formulas on
            // both the good and the faulty machine.
            value = index % 3 == 0 ? sim::Logic4::X : sim::Logic4::Zero;
            break;
        default:
            value = bit((splitmix(random_state) & (1ULL << 63)) != 0);
            break;
        }
        values[inputs[index]] = value;
    }
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2 || argc > 3) {
        std::cerr << "usage: fault_diff <design.dfdb> [patterns]" << std::endl;
        return 2;
    }

    std::uint64_t pattern_count = kDefaultPatterns;
    if (argc == 3) {
        const std::string_view text(argv[2]);
        const auto [end, error] =
            std::from_chars(text.data(), text.data() + text.size(), pattern_count);
        if (error != std::errc{} || end != text.data() + text.size() || pattern_count == 0 ||
            pattern_count > kMaxPatterns) {
            std::cerr << "fault_diff: invalid pattern count '" << text << "'" << std::endl;
            return 2;
        }
    }

    db::DesignDb database;
    const db::Status loaded = db::DesignDb::load(argv[1], database);
    if (!loaded.ok()) {
        std::cerr << "fault_diff: cannot load design '" << argv[1] << "': " << loaded.message()
                  << std::endl;
        return 2;
    }

    const db::Module* top = db::find_module(database.design(), database.design().top);
    if (top == nullptr) {
        std::cerr << "fault_diff: top module '" << database.design().top << "' is not defined"
                  << std::endl;
        return 2;
    }

    std::vector<db::NetId> inputs;
    inputs.reserve(top->inputs.size());
    for (const std::string& name : top->inputs) {
        const db::NetId net = net_named(database, name);
        if (net == db::kInvalidId) {
            std::cerr << "fault_diff: top input '" << name << "' has no flattened net" << std::endl;
            return 2;
        }
        inputs.push_back(net);
    }

    std::vector<db::NetId> observables;
    observables.reserve(top->outputs.size());
    for (const std::string& name : top->outputs) {
        const db::NetId net = net_named(database, name);
        if (net == db::kInvalidId) {
            std::cerr << "fault_diff: top output '" << name << "' has no flattened net"
                      << std::endl;
            return 2;
        }
        observables.push_back(net);
    }
    if (observables.empty()) {
        std::cerr << "fault_diff: design has no primary outputs to observe" << std::endl;
        return 2;
    }

    const db::FlatNetlist& flat = database.flat();
    const std::vector<sim::Fault> faults = atpg::stuck_at_faults(flat);
    if (faults.empty()) {
        std::cerr << "fault_diff: design has no stuck-at faults" << std::endl;
        return 2;
    }

    sim::FaultSimulator word(flat, faults, observables);
    std::vector<sim::FaultResult> scalar_results(faults.size());
    std::vector<std::uint32_t> scalar_first(faults.size(), db::kInvalidId);
    std::vector<sim::Logic4> values(flat.net_count(), sim::Logic4::X);

    std::uint64_t random_state = 0x475a4937ULL;
    std::size_t mismatches = 0;
    const auto report = [&](const std::size_t index, const std::uint64_t pattern,
                            const std::string_view what) {
        ++mismatches;
        if (mismatches > 20) {
            return;
        }
        const sim::Fault& fault = faults[index];
        std::cerr << "fault_diff: " << what << ": fault " << index << " at net " << fault.net
                  << " ('" << net_name(database, fault.net) << "') "
                  << sim::stuck_value_name(fault.stuck) << ", pattern " << pattern << std::endl;
    };

    for (std::uint64_t pattern = 0; pattern < pattern_count; ++pattern) {
        std::fill(values.begin(), values.end(), sim::Logic4::X);
        seed_pattern(pattern, inputs, values, random_state);

        std::vector<sim::Logic4> scalar_values = values;
        const db::Status scalar_status =
            sim::simulate_scalar(flat, scalar_values, observables, faults, scalar_results);
        if (!scalar_status.ok()) {
            std::cerr << "fault_diff: scalar simulation failed at pattern " << pattern << ": "
                      << scalar_status.message() << std::endl;
            return 1;
        }
        const db::Status word_status = word.simulate(values);
        if (!word_status.ok()) {
            std::cerr << "fault_diff: word simulation failed at pattern " << pattern << ": "
                      << word_status.message() << std::endl;
            return 1;
        }

        for (std::size_t index = 0; index < faults.size(); ++index) {
            if (scalar_results[index].state == sim::FaultState::Detected &&
                scalar_first[index] == db::kInvalidId) {
                scalar_first[index] = static_cast<std::uint32_t>(pattern);
            }
            const sim::FaultResult& word_result = word.result(index);
            if (word_result.state == sim::FaultState::Detected && word_result.pattern == pattern &&
                scalar_first[index] != pattern) {
                report(index, pattern, "word engine detects but the scalar reference does not");
            }
        }
    }

    for (std::size_t index = 0; index < faults.size(); ++index) {
        const sim::FaultResult& word_result = word.result(index);
        const bool word_detected = word_result.state == sim::FaultState::Detected;
        const bool scalar_detected = scalar_first[index] != db::kInvalidId;
        if (word_detected != scalar_detected) {
            report(index, word_result.pattern,
                   "detection sets differ between the word engine and the scalar reference");
        } else if (word_detected && word_result.pattern != scalar_first[index]) {
            report(index, word_result.pattern,
                   "first detecting pattern differs between the engines");
        }
    }

    std::cout << "fault_diff: design: " << database.design().top << '\n';
    std::cout << "fault_diff: nets: " << flat.net_count() << '\n';
    std::cout << "fault_diff: faults: " << faults.size() << '\n';
    std::cout << "fault_diff: patterns: " << pattern_count << '\n';
    std::cout << "fault_diff: detected: " << word.detected_count() << '\n';

    if (mismatches != 0) {
        std::cerr << "fault_diff: " << mismatches << " disagreement(s) between the engines"
                  << std::endl;
        return 1;
    }
    std::cout << "fault_diff: the 64-lane engine and the scalar reference agree on every fault"
              << std::endl;
    return 0;
}
