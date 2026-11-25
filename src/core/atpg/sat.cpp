#include "core/atpg/sat.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

#include "core/db/ids.h"
#include "core/sim/logic.h"

namespace gmax::atpg {
namespace {

constexpr int kNoClause = -1;

using Clock = std::chrono::steady_clock;

Clock::time_point current_time(const SatNow now) { return now == nullptr ? Clock::now() : now(); }

bool expired(const Clock::time_point start, const std::uint64_t max_time_ms, const SatNow now) {
    if (max_time_ms == 0) {
        return false;
    }
    const double elapsed =
        std::chrono::duration<double, std::milli>(current_time(now) - start).count();
    return elapsed >= static_cast<double>(max_time_ms);
}

int literal_index(const int literal) {
    const int variable = literal < 0 ? -literal : literal;
    return 2 * (variable - 1) + (literal < 0 ? 1 : 0);
}

int variable_of(const int literal) { return literal < 0 ? -literal : literal; }

// A compact CDCL solver: two watched literals per clause, first-UIP learning,
// VSIDS-style activity, phase saving, and deterministic branching. The formulas
// produced by a small combinational miter are far below the point where a more
// elaborate solver is useful.
class CdclSolver final : public ISatSolver {
  public:
    using Result = ISatSolver::Result;

    CdclSolver() {
        assign_.push_back(0);
        level_.push_back(0);
        reason_.push_back(kNoClause);
        activity_.push_back(0.0);
        phase_.push_back(0);
        seen_.push_back(0);
        watches_.emplace_back();
        watches_.emplace_back();
    }

    int new_variable() override {
        const int variable = static_cast<int>(assign_.size());
        assign_.push_back(0);
        level_.push_back(0);
        reason_.push_back(kNoClause);
        activity_.push_back(0.0);
        phase_.push_back(0);
        seen_.push_back(0);
        watches_.emplace_back();
        watches_.emplace_back();
        return variable;
    }

    bool add_clause(std::vector<int> literals) override {
        std::vector<int> clean;
        clean.reserve(literals.size());
        for (const int literal : literals) {
            const int variable = variable_of(literal);
            if (variable <= 0 || variable >= static_cast<int>(assign_.size())) {
                ok_ = false;
                return false;
            }
            bool duplicate = false;
            for (const int existing : clean) {
                if (existing == -literal) {
                    return true;
                }
                if (existing == literal) {
                    duplicate = true;
                    break;
                }
            }
            if (!duplicate) {
                clean.push_back(literal);
            }
        }
        if (clean.empty()) {
            ok_ = false;
            return false;
        }
        Clause clause;
        clause.literals = std::move(clean);
        const int id = static_cast<int>(clauses_.size());
        clauses_.push_back(std::move(clause));
        if (clauses_[id].literals.size() == 1) {
            units_.push_back(clauses_[id].literals[0]);
        } else {
            watches_[literal_index(clauses_[id].literals[0])].push_back(id);
            watches_[literal_index(clauses_[id].literals[1])].push_back(id);
        }
        for (const int literal : clauses_[id].literals) {
            activity_[variable_of(literal)] += 1.0;
        }
        return true;
    }

    Result solve(const std::size_t max_conflicts, const std::uint64_t max_time_ms,
                 const Clock::time_point start, const SatNow now) override {
        if (!ok_) {
            return Result::Unsat;
        }
        conflicts_ = 0;
        qhead_ = 0;
        start_ = start;
        max_time_ms_ = max_time_ms;
        now_ = now;
        timed_out_ = false;
        for (const int unit : units_) {
            if (over_time()) {
                timed_out_ = true;
                return Result::Unknown;
            }
            if (!enqueue(unit, kNoClause)) {
                return Result::Unsat;
            }
        }
        const int initial = propagate();
        if (timed_out_) {
            return Result::Unknown;
        }
        if (initial != kNoClause) {
            return Result::Unsat;
        }

        while (conflicts_ < max_conflicts) {
            if (over_time()) {
                timed_out_ = true;
                return Result::Unknown;
            }
            const int conflict = propagate();
            if (timed_out_) {
                return Result::Unknown;
            }
            if (conflict != kNoClause) {
                ++conflicts_;
                if (decision_level() == 0) {
                    return Result::Unsat;
                }
                std::vector<int> learnt;
                int backtrack_level = 0;
                analyze(conflict, learnt, backtrack_level);
                if (timed_out_) {
                    return Result::Unknown;
                }
                if (learnt.empty() || learnt[0] == 0) {
                    return Result::Unknown;
                }
                backtrack(backtrack_level);
                const std::size_t before = clauses_.size();
                if (!add_clause(learnt)) {
                    return Result::Unsat;
                }
                if (clauses_.size() == before) {
                    return Result::Unknown;
                }
                const int id = static_cast<int>(before);
                if (!enqueue(clauses_[id].literals[0], id)) {
                    return Result::Unsat;
                }
                var_increment_ *= 1.0 / 0.95;
                if (var_increment_ > 1e100) {
                    for (double& activity : activity_) {
                        activity *= 1e-100;
                    }
                    var_increment_ *= 1e-100;
                }
            } else {
                const int decision = pick_branch();
                if (decision == 0) {
                    return Result::Sat;
                }
                new_decision_level();
                if (!enqueue(decision, kNoClause)) {
                    return Result::Unsat;
                }
            }
        }
        return Result::Unknown;
    }

    bool model_value(const int variable) const override { return assign_[variable] > 0; }
    bool model_is_consistent() const override {
        for (const Clause& clause : clauses_) {
            bool satisfied = false;
            for (const int literal : clause.literals) {
                if (value_of(literal) == 1) {
                    satisfied = true;
                    break;
                }
            }
            if (!satisfied) {
                return false;
            }
        }
        return true;
    }
    std::size_t conflicts() const override { return conflicts_; }
    std::size_t variables() const override { return assign_.size() - 1; }
    std::size_t clauses() const override { return clauses_.size(); }
    bool timed_out() const override { return timed_out_; }

  private:
    struct Clause {
        std::vector<int> literals;
    };

    bool over_time() const { return expired(start_, max_time_ms_, now_); }

    int decision_level() const { return static_cast<int>(trail_limits_.size()); }

    int value_of(const int literal) const {
        const int value = assign_[variable_of(literal)];
        return value == 0 ? 0 : (literal > 0 ? value : -value);
    }

    bool enqueue(const int literal, const int reason) {
        const int variable = variable_of(literal);
        const int wanted = literal > 0 ? 1 : -1;
        if (assign_[variable] != 0) {
            return assign_[variable] == wanted;
        }
        assign_[variable] = wanted;
        level_[variable] = decision_level();
        reason_[variable] = reason;
        phase_[variable] = wanted > 0 ? 1 : 0;
        trail_.push_back(literal);
        return true;
    }

    void new_decision_level() { trail_limits_.push_back(trail_.size()); }

    void backtrack(const int target_level) {
        if (decision_level() <= target_level) {
            return;
        }
        const std::size_t target = trail_limits_[target_level];
        for (std::size_t index = trail_.size(); index > target; --index) {
            const int variable = variable_of(trail_[index - 1]);
            assign_[variable] = 0;
            level_[variable] = 0;
            reason_[variable] = kNoClause;
        }
        trail_.resize(target);
        trail_limits_.resize(static_cast<std::size_t>(target_level));
        qhead_ = trail_.size();
    }

    int propagate() {
        std::size_t checks = 0;
        while (qhead_ < trail_.size()) {
            if ((++checks & 0xFFu) == 0 && over_time()) {
                timed_out_ = true;
                return kNoClause;
            }
            const int assigned = trail_[qhead_++];
            std::vector<int>& watch = watches_[literal_index(-assigned)];
            std::size_t kept = 0;
            for (std::size_t index = 0; index < watch.size(); ++index) {
                const int id = watch[index];
                Clause& clause = clauses_[id];
                // A clause watches two literals. If the falsified literal is
                // the second watch, put it first; otherwise this entry is
                // stale or belongs to the other watch and must be skipped.
                if (clause.literals[0] == -assigned) {
                    // Already first.
                } else if (clause.literals[1] == -assigned) {
                    std::swap(clause.literals[0], clause.literals[1]);
                } else {
                    watch[kept++] = id;
                    continue;
                }
                const int other = clause.literals[1];
                if (value_of(other) == 1) {
                    watch[kept++] = id;
                    continue;
                }
                bool moved = false;
                for (std::size_t other_index = 2; other_index < clause.literals.size();
                     ++other_index) {
                    if (value_of(clause.literals[other_index]) != -1) {
                        std::swap(clause.literals[1], clause.literals[other_index]);
                        watches_[literal_index(clause.literals[1])].push_back(id);
                        moved = true;
                        break;
                    }
                }
                if (moved) {
                    continue;
                }
                watch[kept++] = id;
                const int other_value = value_of(other);
                if (other_value == -1) {
                    for (++index; index < watch.size(); ++index) {
                        watch[kept++] = watch[index];
                    }
                    watch.resize(kept);
                    return id;
                }
                if (!enqueue(other, id)) {
                    for (++index; index < watch.size(); ++index) {
                        watch[kept++] = watch[index];
                    }
                    watch.resize(kept);
                    return id;
                }
            }
            watch.resize(kept);
        }
        return kNoClause;
    }

    void bump(const int variable) { activity_[variable] += var_increment_; }

    void analyze(const int conflict, std::vector<int>& learnt, int& backtrack_level) {
        learnt.clear();
        learnt.push_back(0);
        std::vector<int> touched;
        int path = 0;
        int propagated = 0;
        std::size_t index = trail_.size();
        int current = conflict;
        do {
            if (over_time()) {
                timed_out_ = true;
                return;
            }
            if (current == kNoClause) {
                break;
            }
            const Clause& clause = clauses_[current];
            for (const int literal : clause.literals) {
                const int variable = variable_of(literal);
                if (propagated != 0 && variable == variable_of(propagated)) {
                    continue;
                }
                if (seen_[variable] != 0 || level_[variable] == 0) {
                    continue;
                }
                seen_[variable] = 1;
                touched.push_back(variable);
                bump(variable);
                if (level_[variable] == decision_level()) {
                    ++path;
                } else {
                    learnt.push_back(literal);
                }
            }
            while (index > 0) {
                --index;
                const int literal = trail_[index];
                if (seen_[variable_of(literal)] != 0) {
                    propagated = literal;
                    break;
                }
            }
            if (propagated == 0) {
                break;
            }
            seen_[variable_of(propagated)] = 0;
            --path;
            current = reason_[variable_of(propagated)];
        } while (path > 0);

        learnt[0] = -propagated;
        backtrack_level = 0;
        for (std::size_t i = 1; i < learnt.size(); ++i) {
            backtrack_level = std::max(backtrack_level, level_[variable_of(learnt[i])]);
        }
        for (const int variable : touched) {
            seen_[variable] = 0;
        }
    }

    int pick_branch() const {
        int best = 0;
        double best_activity = -1.0;
        for (int variable = 1; variable < static_cast<int>(assign_.size()); ++variable) {
            if (assign_[variable] != 0) {
                continue;
            }
            if (activity_[variable] > best_activity ||
                (activity_[variable] == best_activity && (best == 0 || variable < best))) {
                best = variable;
                best_activity = activity_[variable];
            }
        }
        if (best == 0) {
            return 0;
        }
        return phase_[best] != 0 ? best : -best;
    }

    std::vector<Clause> clauses_;
    std::vector<std::vector<int>> watches_;
    std::vector<int> units_;
    std::vector<int> assign_;
    std::vector<int> level_;
    std::vector<int> reason_;
    std::vector<int> phase_;
    std::vector<std::uint8_t> seen_;
    std::vector<double> activity_;
    std::vector<int> trail_;
    std::vector<std::size_t> trail_limits_;
    std::size_t qhead_ = 0;
    std::size_t conflicts_ = 0;
    std::uint64_t max_time_ms_ = 0;
    Clock::time_point start_{};
    SatNow now_ = nullptr;
    bool timed_out_ = false;
    double var_increment_ = 1.0;
    bool ok_ = true;
};

bool is_source_or_state(const db::GateType type) {
    return type == db::GateType::Dff || type == db::GateType::Dlat ||
           type == db::GateType::ScanCell || type == db::GateType::Blackbox;
}

class FaultMiter {
  public:
    FaultMiter(const db::FlatNetlist& netlist, const sim::Fault& fault,
               const std::span<const db::NetId> observables, const SatNow now,
               const std::span<const db::CaseAssignment> constraints, ISatSolver& solver)
        : netlist_(netlist), fault_(fault), observables_(observables.begin(), observables.end()),
          constraints_(constraints.begin(), constraints.end()), solver_(solver), now_(now) {}

    SatFaultResult run(const std::size_t max_conflicts, const std::uint64_t max_time_ms) {
        SatFaultResult result;
        const Clock::time_point start = current_time(now_);
        timed_out_ = false;
        if (fault_.net >= netlist_.net_count()) {
            result.status = SatFaultStatus::Unsupported;
            return result;
        }
        if (!build(start, max_time_ms)) {
            result.status = timed_out_ ? SatFaultStatus::Unknown : SatFaultStatus::Unsupported;
            return result;
        }

        const ISatSolver::Result solved = solver_.solve(max_conflicts, max_time_ms, start, now_);
        result.conflicts = solver_.conflicts();
        result.variables = solver_.variables();
        result.clauses = solver_.clauses();
        result.solver_timed_out = solver_.timed_out();
        if (expired(start, max_time_ms, now_)) {
            result.status = SatFaultStatus::Unknown;
            return result;
        }
        if (solved == ISatSolver::Result::Unsat) {
            result.status = SatFaultStatus::Untestable;
            return result;
        }
        if (solved == ISatSolver::Result::Unknown || !solver_.model_is_consistent()) {
            result.status = SatFaultStatus::Unknown;
            return result;
        }
        result.status = replay_model();
        return result;
    }

  private:
    bool build(const Clock::time_point start, const std::uint64_t max_time_ms) {
        const std::size_t nets = netlist_.net_count();
        good_.resize(nets);
        faulty_.resize(nets);
        for (db::NetId net = 0; net < netlist_.net_count(); ++net) {
            if ((net & 0x3Fu) == 0 && expired(start, max_time_ms, now_)) {
                timed_out_ = true;
                return false;
            }
            good_[net] = solver_.new_variable();
            faulty_[net] = solver_.new_variable();
        }

        for (db::NetId net = 0; net < netlist_.net_count(); ++net) {
            if ((net & 0x3Fu) == 0 && expired(start, max_time_ms, now_)) {
                timed_out_ = true;
                return false;
            }
            if (netlist_.driver[net] != db::kPrimaryInput || net == fault_.net) {
                continue;
            }
            add_equal(good_[net], faulty_[net]);
        }

        // ADR-012 section 4: a case value is a hard assignment in both circuits.
        // Asserting it before the activation clause means a value that contradicts
        // the defect leaves the miter UNSAT, which run() reports as Untestable
        // rather than as an unclear answer.
        for (const db::CaseAssignment& constraint : constraints_) {
            if (constraint.net >= netlist_.net_count()) {
                return false;
            }
            const int good_value =
                constraint.value ? good_[constraint.net] : -good_[constraint.net];
            const int faulty_value =
                constraint.value ? faulty_[constraint.net] : -faulty_[constraint.net];
            if (!solver_.add_clause({good_value}) || !solver_.add_clause({faulty_value})) {
                timed_out_ = true;
                return false;
            }
        }

        if (!solver_.add_clause({fault_.stuck == sim::StuckValue::One ? faulty_[fault_.net]
                                                                      : -faulty_[fault_.net]})) {
            return false;
        }

        for (db::GateId gate = 0; gate < netlist_.gate_count(); ++gate) {
            if ((gate & 0x3Fu) == 0 && expired(start, max_time_ms, now_)) {
                timed_out_ = true;
                return false;
            }
            const db::NetId out = netlist_.out_net[gate];
            if (out == db::kInvalidId) {
                continue;
            }
            const db::GateType type = netlist_.type[gate];
            if (is_source_or_state(type)) {
                return false;
            }
            if (out != fault_.net && !encode_gate(good_, gate)) {
                return false;
            }
            if (out != fault_.net && !encode_gate(faulty_, gate)) {
                return false;
            }
            // The faulty driver for the fault site is deliberately absent: the
            // site is a cut in the faulty copy and its value is the forced unit.
            if (out == fault_.net && !encode_gate(good_, gate)) {
                return false;
            }
        }

        std::vector<db::NetId> unique = observables_;
        std::sort(unique.begin(), unique.end());
        unique.erase(std::unique(unique.begin(), unique.end()), unique.end());
        if (unique.empty()) {
            return false;
        }

        std::vector<int> differences;
        differences.reserve(unique.size());
        for (const db::NetId net : unique) {
            if (net >= netlist_.net_count()) {
                return false;
            }
            const int difference = solver_.new_variable();
            add_xor(difference, good_[net], faulty_[net], false);
            differences.push_back(difference);
        }
        return solver_.add_clause(std::move(differences));
    }

    bool encode_gate(const std::vector<int>& values, const db::GateId gate) {
        const std::uint32_t begin = netlist_.fanin_begin[gate];
        const std::uint32_t end = netlist_.fanin_begin[gate + 1];
        std::vector<int> fanins;
        fanins.reserve(static_cast<std::size_t>(end - begin));
        for (std::uint32_t i = begin; i < end; ++i) {
            fanins.push_back(values[netlist_.fanins[i]]);
        }
        const int out = values[netlist_.out_net[gate]];
        switch (netlist_.type[gate]) {
        case db::GateType::And2:
            return fanins.size() == 2 && add_and(out, fanins[0], fanins[1]);
        case db::GateType::Or2:
            return fanins.size() == 2 && add_or(out, fanins[0], fanins[1]);
        case db::GateType::Nand2:
            return fanins.size() == 2 && add_nand(out, fanins[0], fanins[1]);
        case db::GateType::Nor2:
            return fanins.size() == 2 && add_nor(out, fanins[0], fanins[1]);
        case db::GateType::Xor2:
            return fanins.size() == 2 && add_xor(out, fanins[0], fanins[1], false);
        case db::GateType::Xnor2:
            return fanins.size() == 2 && add_xor(out, fanins[0], fanins[1], true);
        case db::GateType::Not:
            return fanins.size() == 1 && add_not(out, fanins[0]);
        case db::GateType::Buf:
        case db::GateType::Po:
            return fanins.size() == 1 && add_equal(out, fanins[0]);
        case db::GateType::Mux2:
            return fanins.size() == 3 && add_mux(out, fanins[0], fanins[1], fanins[2]);
        case db::GateType::Tie0:
            return fanins.empty() && solver_.add_clause({-out});
        case db::GateType::Tie1:
            return fanins.empty() && solver_.add_clause({out});
        case db::GateType::Pi:
            return fanins.empty();
        case db::GateType::Dff:
        case db::GateType::Dlat:
        case db::GateType::ScanCell:
        case db::GateType::Blackbox:
            return false;
        }
        return false;
    }

    bool add_equal(const int left, const int right) {
        return solver_.add_clause({-left, right}) && solver_.add_clause({left, -right});
    }

    bool add_and(const int out, const int left, const int right) {
        return solver_.add_clause({-out, left}) && solver_.add_clause({-out, right}) &&
               solver_.add_clause({out, -left, -right});
    }

    bool add_nand(const int out, const int left, const int right) {
        return solver_.add_clause({out, left}) && solver_.add_clause({out, right}) &&
               solver_.add_clause({-out, -left, -right});
    }

    bool add_or(const int out, const int left, const int right) {
        return solver_.add_clause({out, -left}) && solver_.add_clause({out, -right}) &&
               solver_.add_clause({-out, left, right});
    }

    bool add_nor(const int out, const int left, const int right) {
        return solver_.add_clause({-out, -left}) && solver_.add_clause({-out, -right}) &&
               solver_.add_clause({out, left, right});
    }

    bool add_not(const int out, const int input) {
        return solver_.add_clause({out, input}) && solver_.add_clause({-out, -input});
    }

    bool add_xor(const int out, const int left, const int right, const bool invert) {
        if (!invert) {
            return solver_.add_clause({-left, -right, -out}) &&
                   solver_.add_clause({left, right, -out}) &&
                   solver_.add_clause({left, -right, out}) &&
                   solver_.add_clause({-left, right, out});
        }
        return solver_.add_clause({-left, -right, out}) && solver_.add_clause({left, right, out}) &&
               solver_.add_clause({left, -right, -out}) && solver_.add_clause({-left, right, -out});
    }

    bool add_mux(const int out, const int then_value, const int else_value, const int select) {
        return solver_.add_clause({-select, -out, then_value}) &&
               solver_.add_clause({-select, out, -then_value}) &&
               solver_.add_clause({select, -out, else_value}) &&
               solver_.add_clause({select, out, -else_value});
    }

    SatFaultStatus replay_model() const {
        std::vector<sim::Logic4> values(netlist_.net_count(), sim::Logic4::X);
        for (db::NetId net = 0; net < netlist_.net_count(); ++net) {
            if (netlist_.driver[net] == db::kPrimaryInput) {
                values[net] =
                    solver_.model_value(good_[net]) ? sim::Logic4::One : sim::Logic4::Zero;
            }
        }
        sim::FaultResult outcome;
        const db::Status status = sim::simulate_scalar(netlist_, values, observables_,
                                                       std::span<const sim::Fault>(&fault_, 1),
                                                       std::span<sim::FaultResult>(&outcome, 1));
        if (!status.ok() || outcome.state != sim::FaultState::Detected) {
            return SatFaultStatus::Unknown;
        }
        return SatFaultStatus::Detected;
    }

    const db::FlatNetlist& netlist_;
    const sim::Fault& fault_;
    std::vector<db::NetId> observables_;
    const std::vector<db::CaseAssignment> constraints_;
    ISatSolver& solver_;
    std::vector<int> good_;
    std::vector<int> faulty_;
    SatNow now_ = nullptr;
    bool timed_out_ = false;
};

}  // namespace

SatFaultResult check_fault(const db::FlatNetlist& netlist, const sim::Fault& fault,
                           const std::span<const db::NetId> observables,
                           const std::size_t max_conflicts, const std::uint64_t max_time_ms,
                           const SatNow now,
                           const std::span<const db::CaseAssignment> constraints) {
    const std::unique_ptr<ISatSolver> solver = make_internal_sat_solver();
    FaultMiter miter(netlist, fault, observables, now, constraints, *solver);
    return miter.run(max_conflicts, max_time_ms);
}

std::unique_ptr<ISatSolver> make_internal_sat_solver() { return std::make_unique<CdclSolver>(); }

}  // namespace gmax::atpg
