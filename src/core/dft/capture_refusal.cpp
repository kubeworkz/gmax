#include "core/dft/capture_refusal.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/db/constraints.h"
#include "core/dft/capture.h"

namespace gmax::dft {
namespace {

using db::ConstraintClock;
using db::ConstraintClockGroup;

const ConstraintClock* find_clock(const db::Constraints& constraints, const std::string_view name) {
    for (const ConstraintClock& clock : constraints.clocks) {
        if (clock.name == name) {
            return &clock;
        }
    }
    return nullptr;
}

std::string join(const std::vector<std::string>& names) {
    std::string out;
    for (std::size_t i = 0; i < names.size(); ++i) {
        if (i != 0) {
            out.append(", ");
        }
        out.append(names[i]);
    }
    return out;
}

// Where a clock sits: the row and the inner group of set_clock_groups that names
// it. Clocks in two inner groups of one row are different groups, which is the
// point of the command, so the position is the pair rather than the row.
struct GroupPosition {
    std::size_t row = 0;
    std::size_t group = 0;
};

bool group_position(const db::Constraints& constraints, const std::string_view clock,
                    GroupPosition& position) {
    for (std::size_t row = 0; row < constraints.clock_groups.size(); ++row) {
        const std::vector<std::vector<std::string>>& groups = constraints.clock_groups[row].groups;
        for (std::size_t group = 0; group < groups.size(); ++group) {
            for (const std::string& member : groups[group]) {
                if (member == clock) {
                    position.row = row;
                    position.group = group;
                    return true;
                }
            }
        }
    }
    return false;
}

std::string declared_group_name(const db::Constraints& constraints, const std::string_view clock) {
    GroupPosition position;
    if (!group_position(constraints, clock, position)) {
        return std::string();
    }
    return constraints.clock_groups[position.row].name;
}

// Two clocks are captured together only when the SDC puts them in one declared
// group; two clocks the SDC never groups are each their own group, so they are
// apart unless they are the same clock.
bool same_declared_group(const db::Constraints& constraints, const std::string& left,
                         const std::string& right) {
    GroupPosition a;
    GroupPosition b;
    const bool left_grouped = group_position(constraints, left, a);
    const bool right_grouped = group_position(constraints, right, b);
    if (left_grouped && right_grouped) {
        return a.row == b.row && a.group == b.group;
    }
    if (!left_grouped && !right_grouped) {
        return left == right;
    }
    return false;
}

// `'clk_a' (group 'fast')`, or `'clk_a' (no declared group)`.
std::string clock_phrase(const db::Constraints& constraints, const std::string& clock) {
    const std::string name = declared_group_name(constraints, clock);
    if (name.empty()) {
        return "'" + clock + "' (no declared group)";
    }
    return "'" + clock + "' (group '" + name + "')";
}

// The group's label when the profile gave one, otherwise its clock list.
std::string group_phrase(const CaptureGroup& group) {
    if (!group.name.empty()) {
        return "group '" + group.name + "'";
    }
    return "group [" + join(group.timing.clocks) + "]";
}

std::string waveform_phrase(const ConstraintClock& clock, const std::string& name) {
    if (!clock.has_waveform) {
        return name + " has no waveform";
    }
    return name + " rise " + db::canonical_decimal(clock.rise) + " ns fall " +
           db::canonical_decimal(clock.fall) + " ns";
}

void add(std::vector<CaptureRefusal>& refusals, const CaptureRefusalCode code, std::string detail) {
    CaptureRefusal refusal;
    refusal.code = code;
    refusal.detail = std::move(detail);
    refusals.push_back(std::move(refusal));
}

// Condition 1, first trigger: a simultaneous cycle pulses clocks the SDC keeps
// apart. Grouped cycles reach the second trigger below rather than this one.
void check_simultaneous(const CaptureProcedure& procedure, const db::Constraints& constraints,
                        std::vector<CaptureRefusal>& refusals) {
    if (procedure.kind != CaptureKind::Simultaneous) {
        return;
    }
    for (std::size_t c = 0; c < procedure.cycles.size(); ++c) {
        for (const CaptureGroup& group : procedure.cycles[c].groups) {
            const std::vector<std::string>& clocks = group.timing.clocks;
            std::size_t apart = 0;
            for (std::size_t i = 1; i < clocks.size(); ++i) {
                if (!same_declared_group(constraints, clocks.front(), clocks[i])) {
                    apart = i;
                    break;
                }
            }
            if (apart == 0) {
                continue;
            }
            add(refusals, CaptureRefusalCode::SimultaneousAcrossGroups,
                "cycle " + std::to_string(c) + " pulses " +
                    clock_phrase(constraints, clocks.front()) + " and " +
                    clock_phrase(constraints, clocks[apart]) +
                    ", which set_clock_groups keeps apart");
        }
    }
}

// Condition 1, second trigger: two groups of one cycle share an edge time, which
// is simultaneous capture by timing even when the SDC groups them together.
void check_shared_edge(const CaptureProcedure& procedure, std::vector<CaptureRefusal>& refusals) {
    for (std::size_t c = 0; c < procedure.cycles.size(); ++c) {
        const std::vector<CaptureGroup>& groups = procedure.cycles[c].groups;
        for (std::size_t i = 0; i < groups.size(); ++i) {
            for (std::size_t j = i + 1; j < groups.size(); ++j) {
                if (groups[i].timing.edge != groups[j].timing.edge ||
                    groups[i].timing.clocks.empty() || groups[j].timing.clocks.empty()) {
                    continue;
                }
                add(refusals, CaptureRefusalCode::SimultaneousAcrossGroups,
                    "cycle " + std::to_string(c) + " gives '" + groups[i].timing.clocks.front() +
                        "' and '" + groups[j].timing.clocks.front() + "' one edge time");
            }
        }
    }
}

// Condition 3: a group's clocks disagree on period or waveform.
void check_group_timing(const CaptureProcedure& procedure, const db::Constraints& constraints,
                        std::vector<CaptureRefusal>& refusals) {
    for (const CaptureCycle& cycle : procedure.cycles) {
        for (const CaptureGroup& group : cycle.groups) {
            const std::vector<std::string>& clocks = group.timing.clocks;
            if (clocks.size() < 2 || capture_group_timing_agrees(constraints, clocks)) {
                continue;
            }
            const ConstraintClock* first = find_clock(constraints, clocks.front());
            if (first == nullptr) {
                continue;
            }
            for (std::size_t i = 1; i < clocks.size(); ++i) {
                const ConstraintClock* other = find_clock(constraints, clocks[i]);
                if (other == nullptr) {
                    continue;
                }
                if (other->period != first->period) {
                    add(refusals, CaptureRefusalCode::GroupTimingDisagrees,
                        group_phrase(group) + " mixes periods " +
                            db::canonical_decimal(first->period) + " ns (" + clocks.front() +
                            ") and " + db::canonical_decimal(other->period) + " ns (" + clocks[i] +
                            ")");
                    break;
                }
                if (other->has_waveform != first->has_waveform || other->rise != first->rise ||
                    other->fall != first->fall) {
                    add(refusals, CaptureRefusalCode::GroupTimingDisagrees,
                        group_phrase(group) + " mixes waveforms (" +
                            waveform_phrase(*first, clocks.front()) + ", " +
                            waveform_phrase(*other, clocks[i]) + ")");
                    break;
                }
            }
        }
    }
}

// Condition 4: a clock in more than one cycle, or twice in one cycle. Both
// positions are named because the fix depends on which one is the typo.
void check_clock_repeated(const CaptureProcedure& procedure,
                          std::vector<CaptureRefusal>& refusals) {
    std::vector<std::pair<std::string, std::size_t>> seen;
    std::vector<std::string> reported;
    for (std::size_t c = 0; c < procedure.cycles.size(); ++c) {
        for (const CaptureGroup& group : procedure.cycles[c].groups) {
            for (const std::string& clock : group.timing.clocks) {
                const auto found =
                    std::find_if(seen.begin(), seen.end(),
                                 [&clock](const std::pair<std::string, std::size_t>& entry) {
                                     return entry.first == clock;
                                 });
                if (found == seen.end()) {
                    seen.push_back({clock, c});
                    continue;
                }
                if (std::find(reported.begin(), reported.end(), clock) != reported.end()) {
                    continue;
                }
                reported.push_back(clock);
                if (found->second == c) {
                    add(refusals, CaptureRefusalCode::ClockRepeated,
                        "'" + clock + "' appears twice in cycle " + std::to_string(c));
                } else {
                    add(refusals, CaptureRefusalCode::ClockRepeated,
                        "'" + clock + "' appears in cycle " + std::to_string(found->second) +
                            " and cycle " + std::to_string(c));
                }
            }
        }
    }
}

// Condition 5: section 3's clock-level exclusion emptied the procedure, so nothing
// is left to pulse. `procedure` is the excluded procedure the run would apply;
// each clock the exclusion dropped gets its own line, in name order, and a run
// that keeps even one clock is warned about the others instead of refused.
void check_no_capture_clock(const CaptureProcedure& procedure,
                            const std::vector<db::ClockGateCount>& clock_gates,
                            std::vector<CaptureRefusal>& refusals) {
    for (const CaptureCycle& cycle : procedure.cycles) {
        if (!cycle.groups.empty()) {
            return;
        }
    }
    if (clock_gates.empty()) {
        return;
    }
    for (const db::ClockGateCount& clock : clock_gates) {
        if (clock.gates != 0) {
            continue;
        }
        add(refusals, CaptureRefusalCode::NoCaptureClock,
            "'" + clock.clock + "' clocks no state-holding gate and was dropped");
    }
}

}  // namespace

std::string_view capture_refusal_code_name(const CaptureRefusalCode code) {
    switch (code) {
    case CaptureRefusalCode::SimultaneousAcrossGroups:
        return "simultaneous-across-groups";
    case CaptureRefusalCode::IntermediateState:
        return "intermediate-state";
    case CaptureRefusalCode::GroupTimingDisagrees:
        return "group-timing-disagrees";
    case CaptureRefusalCode::ClockRepeated:
        return "clock-repeated";
    case CaptureRefusalCode::NoCaptureClock:
        return "no-capture-clock";
    case CaptureRefusalCode::CrossingWithoutLockup:
        return "crossing-without-lockup";
    }
    return "simultaneous-across-groups";
}

std::vector<CaptureRefusal>
find_capture_refusals(const CaptureProcedure& procedure, const db::Constraints& constraints,
                      const std::vector<db::ClockGateCount>& clock_gates) {
    std::vector<CaptureRefusal> refusals;
    // Every decidable condition is evaluated before anything is printed, so one
    // run reports every reason instead of one reason per run.
    check_simultaneous(procedure, constraints, refusals);
    check_shared_edge(procedure, refusals);
    check_group_timing(procedure, constraints, refusals);
    check_clock_repeated(procedure, refusals);
    check_no_capture_clock(procedure, clock_gates, refusals);

    // Contract order: condition, then the line, so fixing one finding never
    // reshuffles the others.
    std::sort(refusals.begin(), refusals.end(),
              [](const CaptureRefusal& left, const CaptureRefusal& right) {
                  if (left.code != right.code) {
                      return static_cast<int>(left.code) < static_cast<int>(right.code);
                  }
                  return left.detail < right.detail;
              });
    refusals.erase(std::unique(refusals.begin(), refusals.end()), refusals.end());
    return refusals;
}

void write_capture_refusals(std::ostream& out, const std::vector<CaptureRefusal>& refusals) {
    if (refusals.empty()) {
        return;
    }
    out << "gmax: cannot apply the capture procedure; refusing to grade\n";
    for (const CaptureRefusal& refusal : refusals) {
        out << "gmax:   " << capture_refusal_code_name(refusal.code) << ": " << refusal.detail
            << '\n';
    }
}

}  // namespace gmax::dft
