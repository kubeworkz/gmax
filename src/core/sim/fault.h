#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include "core/db/flat.h"
#include "core/db/status.h"
#include "core/sim/logic.h"

namespace gmax::sim {

// A single stuck-at fault: architecture 9.1's (net, stuck value) pair. WP-006
// generates and collapses the list; this module only grades one.
enum class StuckValue : std::uint8_t { Zero = 0, One = 1 };

std::string_view stuck_value_name(StuckValue value);

struct Fault {
    db::NetId net = db::kInvalidId;
    StuckValue stuck = StuckValue::Zero;

    friend bool operator==(const Fault&, const Fault&) = default;
};

enum class FaultState : std::uint8_t { Undetected = 0, Detected = 1 };

// One entry per input fault. `pattern` is the zero-based index of the first
// pattern that detected the fault, or db::kInvalidId while undetected. The
// scalar reference grades a single pattern at a time, so it leaves `pattern`
// unset and reports only `state`.
struct FaultResult {
    FaultState state = FaultState::Undetected;
    std::uint32_t pattern = db::kInvalidId;
};

// Grades every fault against one pattern, one fault at a time. This is the
// independent oracle for the word engine, not the fast path (architecture
// 10.4). `values` follows evaluate()'s convention: net_count() entries with
// primary inputs and state-holding outputs seeded by the caller and every other
// net X. It is evaluated in place to the good machine; each fault is then
// injected into a copy.
//
// Detection is conservative: a fault is detected only when the good and faulty
// values at an observable net are both known and differ. An X on either side is
// not evidence a tester could observe the difference.
db::Status simulate_scalar(const db::FlatNetlist& netlist, std::span<Logic4> values,
                           std::span<const db::NetId> observables, std::span<const Fault> faults,
                           std::span<FaultResult> results);

// 64-fault-per-word parallel-fault simulation with fault dropping (architecture
// 10.1). Faults are grouped into fixed chunks of 64 in list order, so a fault's
// lane is its position in its chunk; dropping a fault removes its lane from
// later patterns without renumbering the others.
//
// The good machine is scalar and is evaluated by evaluate() on each simulate().
// The faulty machine starts equal to the good machine and is evaluated only in
// the transitive fanout of the fault sites, in level order through an event
// queue. Detected lanes are ignored for the rest of their pattern and their
// faults are skipped by every later pattern.
//
// `netlist` and `faults` must outlive the simulator; `observables` is copied.
// The caller supplies the observation points: primary outputs and, in the
// capture frame, scan-cell data inputs.
class FaultSimulator {
  public:
    static constexpr std::size_t kFaultsPerWord = LogicWord::kLanes;

    FaultSimulator(const db::FlatNetlist& netlist, std::span<const Fault> faults,
                   std::span<const db::NetId> observables);

    // Grades one pattern and drops the faults it detects. `values` follows the
    // same seeding convention as evaluate(). A malformed netlist, value array,
    // fault net or observable net returns BadPayload.
    db::Status simulate(std::span<Logic4> values);

    std::size_t fault_count() const { return faults_.size(); }
    std::size_t detected_count() const { return detected_; }
    std::size_t active_count() const { return faults_.size() - detected_; }
    std::uint32_t pattern_count() const { return patterns_; }

    const FaultResult& result(std::size_t index) const { return results_[index]; }
    std::span<const FaultResult> results() const { return results_; }

  private:
    struct Site {
        db::NetId net = db::kInvalidId;
        std::uint64_t zero_mask = 0;
        std::uint64_t one_mask = 0;
    };

    void build_consumers();
    void grade_group(std::size_t first, std::span<const Logic4> good);

    LogicWord read_net(db::NetId net, std::span<const Logic4> good) const;
    void store_net(db::NetId net, LogicWord word);
    const Site* find_site(db::NetId net) const;
    std::uint64_t detect(db::NetId net, std::size_t first, std::span<const Logic4> good,
                         LogicWord word, std::uint64_t active);
    static void apply_force(LogicWord& word, const Site& site);

    const db::FlatNetlist& netlist_;
    std::span<const Fault> faults_;
    std::vector<db::NetId> observables_;
    std::vector<FaultResult> results_;
    std::size_t detected_ = 0;
    std::uint32_t patterns_ = 0;

    std::vector<std::uint8_t> observable_;
    std::vector<std::uint32_t> consumer_begin_;
    std::vector<db::GateId> consumers_;
    std::vector<LogicWord> faulty_;
    std::vector<std::uint32_t> stamp_;
    std::uint32_t generation_ = 0;
    std::vector<std::uint8_t> queued_;
    std::vector<db::GateId> events_;
    std::vector<Site> sites_;
    std::vector<LogicWord> fanin_values_;
};

}  // namespace gmax::sim
