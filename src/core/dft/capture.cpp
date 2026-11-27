#include "core/dft/capture.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/db/constraints.h"
#include "core/db/crc32.h"

namespace gmax::dft {
namespace {

void append(std::string& out, std::string_view field) {
    out.append(field);
    out.push_back('\t');
}

void append_number(std::string& out, const double value) {
    out.append(db::canonical_decimal(value));
    out.push_back('\t');
}

void append_number(std::string& out, const std::size_t value) {
    out.append(std::to_string(value));
    out.push_back('\t');
}

std::string join(const std::vector<std::string>& names) {
    std::string out;
    for (std::size_t i = 0; i < names.size(); ++i) {
        if (i != 0) {
            out.push_back(',');
        }
        out.append(names[i]);
    }
    return out;
}

// ADR-013 section 1's shapes, read off the cycles themselves: one cycle with one
// group is simultaneous, one cycle with several groups is grouped, and two or more
// cycles are sequential. None of the three describes a procedure with no cycle, so
// an empty procedure carries the `none` marker: it makes no capture claim, which
// section 6 reports as `absent`, and it does not borrow a shape it fails.
CaptureKind kind_from_cycles(const std::vector<CaptureCycle>& cycles) {
    if (cycles.empty()) {
        return CaptureKind::None;
    }
    if (cycles.size() != 1) {
        return CaptureKind::Sequential;
    }
    return cycles[0].groups.size() > 1 ? CaptureKind::Grouped : CaptureKind::Simultaneous;
}

const db::ConstraintClock* find_clock(const db::Constraints& constraints,
                                      const std::string_view name) {
    for (const db::ConstraintClock& clock : constraints.clocks) {
        if (clock.name == name) {
            return &clock;
        }
    }
    return nullptr;
}

}  // namespace

std::string_view capture_kind_name(const CaptureKind kind) {
    switch (kind) {
    case CaptureKind::Simultaneous:
        return "simultaneous";
    case CaptureKind::Grouped:
        return "grouped";
    case CaptureKind::Sequential:
        return "sequential";
    case CaptureKind::None:
        return "none";
    }
    return "none";
}

std::string_view capture_intermediate_state_name(const CaptureIntermediateState state) {
    switch (state) {
    case CaptureIntermediateState::Scanned:
        return "scanned";
    case CaptureIntermediateState::Masked:
        return "masked";
    }
    return "scanned";
}

CaptureIntermediateState capture_intermediate_state(const CaptureCycle& cycle,
                                                    const std::string_view clock) {
    for (const auto& entry : cycle.intermediate) {
        if (entry.first == clock) {
            return entry.second;
        }
    }
    return CaptureIntermediateState::Scanned;
}

std::string canonical_capture_payload(const CaptureProcedure& procedure) {
    // Unlike the constraint payload, nothing here is sorted by line: cycle and
    // group order carry meaning (section 1), so they are written in order. Only
    // the clock list inside a timing set is sorted, here rather than at
    // construction, because a group is a set and the bytes must not depend on
    // how the procedure was built.
    // Version 2 dropped the group label: section 6 excludes provenance, and a
    // label is a diagnostic rather than part of the procedure. Version 3 writes
    // each cycle's groups in edge order, so the same groups listed differently
    // by a profile produce the same bytes. Version 4 adds the intermediate
    // state of the domains a non-final cycle captures (section 2).
    std::string out = "gmax-capture-4\n";
    append(out, "kind");
    out.append(capture_kind_name(procedure.kind));
    out.push_back('\n');

    for (std::size_t c = 0; c < procedure.cycles.size(); ++c) {
        const CaptureCycle& cycle = procedure.cycles[c];
        std::string header;
        append(header, "cycle");
        append_number(header, c);
        append_number(header, cycle.groups.size());
        header.push_back('\n');
        out.append(header);

        // The edge time is what places a group, so two orderings of one cycle
        // are one procedure. Writing the groups in edge order makes their bytes
        // identical and the index below a canonical position rather than the
        // position the profile happened to use. Edges are strictly increasing in
        // a valid procedure; the clock list breaks an edge tie so the order stays
        // total even for one the refusal pass would reject.
        struct Entry {
            double edge = 0.0;
            std::string clocks;
            const CaptureGroup* group = nullptr;
        };
        std::vector<Entry> entries;
        entries.reserve(cycle.groups.size());
        for (const CaptureGroup& group : cycle.groups) {
            Entry entry;
            entry.edge = group.timing.edge;
            std::vector<std::string> clocks = group.timing.clocks;
            std::sort(clocks.begin(), clocks.end());
            entry.clocks = join(clocks);
            entry.group = &group;
            entries.push_back(std::move(entry));
        }
        std::sort(entries.begin(), entries.end(), [](const Entry& left, const Entry& right) {
            if (left.edge != right.edge) {
                return left.edge < right.edge;
            }
            return left.clocks < right.clocks;
        });

        for (std::size_t g = 0; g < entries.size(); ++g) {
            const CaptureTimingSet& timing = entries[g].group->timing;
            std::string line;
            append(line, "group");
            append_number(line, c);
            append_number(line, g);
            append_number(line, timing.edge);
            append_number(line, timing.period);
            // The waveform flag is always written, so a group's field count
            // never depends on the source text, as in section 2 of ADR-012.
            append(line, timing.has_waveform ? "waveform" : "nowaveform");
            append_number(line, timing.rise);
            append_number(line, timing.fall);
            append(line, entries[g].clocks);
            line.push_back('\n');
            out.append(line);
        }

        // Section 2: a non-final cycle records the state of the domains it
        // captures, because those are the domains a later cycle reads. A domain
        // it leaves unpulsed is not intermediate at this cycle and is not
        // written. The domains come from the cycle's own groups and are written
        // sorted, so the bytes never depend on how the groups were ordered. The
        // final cycle owns no intermediate value, so it writes no state at all.
        if (c + 1 < procedure.cycles.size()) {
            std::vector<std::string> domains;
            for (const CaptureGroup& group : cycle.groups) {
                for (const std::string& clock : group.timing.clocks) {
                    if (std::find(domains.begin(), domains.end(), clock) == domains.end()) {
                        domains.push_back(clock);
                    }
                }
            }
            std::sort(domains.begin(), domains.end());
            for (const std::string& domain : domains) {
                CaptureIntermediateState state = CaptureIntermediateState::Scanned;
                for (const auto& entry : cycle.intermediate) {
                    if (entry.first == domain) {
                        state = entry.second;
                        break;
                    }
                }
                std::string line;
                append(line, "intermediate");
                append_number(line, c);
                append(line, domain);
                append(line, capture_intermediate_state_name(state));
                line.push_back('\n');
                out.append(line);
            }
        }
    }
    return out;
}

std::uint32_t capture_hash(const CaptureProcedure& procedure) {
    const std::string payload = canonical_capture_payload(procedure);
    return db::crc32(std::string_view(payload));
}

CaptureProcedure resolve_capture_procedure(const db::Constraints& constraints) {
    CaptureProcedure procedure;
    procedure.derived = true;

    std::vector<std::string> names;
    names.reserve(constraints.clocks.size());
    for (const db::ConstraintClock& clock : constraints.clocks) {
        names.push_back(clock.name);
    }
    // A repeated name is kept: section 4 condition 4 refuses it, and dropping a
    // copy here would hide the duplicate from that check.
    std::sort(names.begin(), names.end());

    procedure.cycles.reserve(names.size());
    for (std::size_t i = 0; i < names.size(); ++i) {
        CaptureGroup group;
        group.timing = capture_timing_set(constraints, {names[i]}, 0.0);
        CaptureCycle cycle;
        cycle.groups.push_back(std::move(group));
        // A derived cycle captures exactly its own clock, so section 2's default
        // is stated rather than left implicit on every non-final cycle.
        if (i + 1 < names.size()) {
            cycle.intermediate.push_back({names[i], CaptureIntermediateState::Scanned});
        }
        procedure.cycles.push_back(std::move(cycle));
    }
    // Section 1 reads the kind off the cycles, and a design with one clock makes
    // one cycle of one group: simultaneous, not a sequential procedure that happens
    // to have a single cycle. No cycle means no capture claim at all, marked `none`.
    // Deriving it here means every caller gets a shape the ADR allows, whichever
    // entry point built the procedure.
    procedure.kind = kind_from_cycles(procedure.cycles);
    return procedure;
}

CaptureTimingSet capture_timing_set(const db::Constraints& constraints,
                                    const std::vector<std::string>& clocks, const double edge) {
    CaptureTimingSet timing;
    timing.clocks = clocks;
    std::sort(timing.clocks.begin(), timing.clocks.end());
    timing.edge = edge;
    if (timing.clocks.empty()) {
        return timing;
    }
    // The bytewise smallest clock supplies the numbers, so the encoding never
    // depends on the order the group's names were written in.
    const db::ConstraintClock* clock = find_clock(constraints, timing.clocks.front());
    if (clock == nullptr) {
        return timing;
    }
    timing.period = clock->period;
    timing.has_waveform = clock->has_waveform;
    timing.rise = clock->rise;
    timing.fall = clock->fall;
    return timing;
}

bool capture_group_timing_agrees(const db::Constraints& constraints,
                                 const std::vector<std::string>& clocks) {
    const db::ConstraintClock* first = nullptr;
    for (const std::string& name : clocks) {
        const db::ConstraintClock* clock = find_clock(constraints, name);
        if (clock == nullptr) {
            return false;
        }
        if (first == nullptr) {
            first = clock;
            continue;
        }
        if (clock->period != first->period || clock->has_waveform != first->has_waveform ||
            clock->rise != first->rise || clock->fall != first->fall) {
            return false;
        }
    }
    return true;
}

CaptureProcedure exclude_idle_clocks(const CaptureProcedure& procedure,
                                     const db::Constraints& constraints,
                                     const std::vector<db::ClockGateCount>& clock_gates,
                                     std::vector<std::string>& dropped) {
    dropped.clear();
    for (const db::ClockGateCount& clock : clock_gates) {
        if (clock.gates == 0) {
            dropped.push_back(clock.clock);
        }
    }
    if (dropped.empty()) {
        // Nothing leaves, so nothing changes: the shape the caller resolved or read
        // already matches its cycles, and it is not re-derived here.
        return procedure;
    }
    std::sort(dropped.begin(), dropped.end());

    CaptureProcedure kept;
    kept.derived = procedure.derived;
    for (const CaptureCycle& cycle : procedure.cycles) {
        CaptureCycle next;
        for (const CaptureGroup& group : cycle.groups) {
            std::vector<std::string> clocks;
            for (const std::string& clock : group.timing.clocks) {
                if (!std::binary_search(dropped.begin(), dropped.end(), clock)) {
                    clocks.push_back(clock);
                }
            }
            if (clocks.empty()) {
                continue;
            }
            CaptureGroup kept_group;
            kept_group.name = group.name;
            // The period and waveform come from the clocks that remain, so the
            // encoding never describes a clock the run does not pulse.
            kept_group.timing = capture_timing_set(constraints, clocks, group.timing.edge);
            next.groups.push_back(std::move(kept_group));
        }
        if (next.groups.empty()) {
            continue;
        }
        for (const auto& entry : cycle.intermediate) {
            if (!std::binary_search(dropped.begin(), dropped.end(), entry.first)) {
                next.intermediate.push_back(entry);
            }
        }
        kept.cycles.push_back(std::move(next));
    }
    // A different cycle can be last after the drop, and no final cycle carries an
    // intermediate state (section 2).
    if (!kept.cycles.empty()) {
        kept.cycles.back().intermediate.clear();
    }
    kept.kind = kind_from_cycles(kept.cycles);
    return kept;
}

void assign_capture_edges(std::vector<CaptureGroup>& groups) {
    double slowest = 0.0;
    for (const CaptureGroup& group : groups) {
        slowest = std::max(slowest, group.timing.period);
    }
    double edge = 0.0;
    for (CaptureGroup& group : groups) {
        group.timing.edge = edge;
        edge += slowest;
    }
}

}  // namespace gmax::dft
