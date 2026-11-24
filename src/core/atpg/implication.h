#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "core/atpg/logic5.h"
#include "core/db/flat.h"
#include "core/db/status.h"

namespace gmax::atpg {

// WP-007's value/implication layer. The search loop is deliberately absent:
// this class assigns values, propagates them to a fixpoint, and lets a caller
// checkpoint and restore a branch.
//
// The engine keeps occurrence counts for AND/OR/NAND/NOR gates. A binary
// controlling input fixes the output immediately; when every input is a binary
// non-controlling value the output is fixed the other way. Gates containing D,
// DBar or X fall through to eval_gate so the counts cannot hide a defect value.
class ImplicationEngine {
  public:
    explicit ImplicationEngine(const db::FlatNetlist& netlist);

    // Clears every net to X, rebuilds the consumer lists, and propagates tie
    // constants. Must be called before assign(), imply(), or value().
    db::Status reset();

    // Assigns 0 or 1 to a net (primary input or state output). Defect values
    // are rejected; use force_fault() so a fault site cannot be created
    // without the forced flag. Assigning a different binary value to a net that
    // already has one sets conflict() and leaves the old value.
    db::Status assign(db::NetId net, Logic5 value);

    // Forces a fault site to D or DBar. The site must still be X (or already
    // carry the same defect value); activation after the net has been implied
    // is a conflict, because the engine does not retract values derived from
    // the old value. The driving gate may still evaluate, but a compatible
    // good value does not replace the forced defect value; a contradictory
    // good value sets conflict().
    db::Status force_fault(db::NetId net, Logic5 value);

    // Processes queued gates to a fixpoint. A logical conflict is reported by
    // conflict(), not by a Status failure; the caller restores a checkpoint.
    db::Status imply();

    Logic5 value(db::NetId net) const;
    bool conflict() const { return conflict_; }

    // Restores values, gate counts, queue position, queued flags, and the
    // conflict flag. Checkpoints are intended to be used LIFO, as PODEM does.
    struct Checkpoint {
        std::size_t trail_size = 0;
        std::size_t queue_size = 0;
        std::size_t queue_head = 0;
        bool conflict = false;
    };
    Checkpoint checkpoint() const;
    db::Status restore(const Checkpoint& checkpoint);

  private:
    struct TrailEntry {
        db::NetId net = db::kInvalidId;
        Logic5 old_value = Logic5::X;
        bool old_forced = false;
    };

    static bool is_source_or_state(db::GateType type);
    static int control_class(db::GateType type, Logic5 value);

    db::Status build_consumers();
    void schedule(db::GateId gate);
    void update_counts(db::NetId net, Logic5 old_value, Logic5 new_value);
    void commit(db::NetId net, Logic5 value, bool force);
    Logic5 eval_gate_at(db::GateId gate);

    const db::FlatNetlist& netlist_;
    std::vector<Logic5> values_;
    std::vector<std::uint32_t> consumer_begin_;
    std::vector<db::GateId> consumers_;
    std::vector<std::uint32_t> control_count_;
    std::vector<std::uint32_t> noncontrol_count_;
    std::vector<db::GateId> queue_;
    std::size_t queue_head_ = 0;
    std::vector<std::uint8_t> queued_;
    std::vector<std::uint8_t> forced_;
    std::vector<TrailEntry> trail_;
    std::vector<Logic5> fanin_values_;
    bool conflict_ = false;
    bool initialized_ = false;
};

}  // namespace gmax::atpg
