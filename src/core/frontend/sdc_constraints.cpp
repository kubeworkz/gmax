#include "core/frontend/sdc_constraints.h"

#include <algorithm>
#include <string>
#include <vector>

namespace gmax::frontend {
namespace {

db::ClockGroupKind to_db_kind(const SdcClockGroupKind kind) {
    switch (kind) {
    case SdcClockGroupKind::Asynchronous:
        return db::ClockGroupKind::Asynchronous;
    case SdcClockGroupKind::LogicallyExclusive:
        return db::ClockGroupKind::LogicallyExclusive;
    case SdcClockGroupKind::PhysicallyExclusive:
        return db::ClockGroupKind::PhysicallyExclusive;
    }
    return db::ClockGroupKind::Asynchronous;
}

bool is_unsupported(const DiagnosticCode code) {
    return code == DiagnosticCode::SdcUnsupportedCommand ||
           code == DiagnosticCode::SdcUnsupportedOption;
}

}  // namespace

std::vector<std::string> unsupported_construct_names(const std::vector<Diagnostic>& diagnostics) {
    std::vector<std::string> names;
    for (const Diagnostic& diagnostic : diagnostics) {
        if (diagnostic.is_error() || !is_unsupported(diagnostic.code)) {
            continue;
        }
        if (!diagnostic.object.empty()) {
            names.push_back(diagnostic.object);
        }
    }
    std::sort(names.begin(), names.end());
    names.erase(std::unique(names.begin(), names.end()), names.end());
    return names;
}

db::Constraints to_constraints(const SdcConstraints& sdc,
                               const std::vector<Diagnostic>& diagnostics) {
    db::Constraints constraints;
    constraints.present = true;
    constraints.unsupported = unsupported_construct_names(diagnostics);
    constraints.complete = constraints.unsupported.empty();

    constraints.clocks.reserve(sdc.clocks.size());
    for (const SdcClock& clock : sdc.clocks) {
        db::ConstraintClock out;
        out.name = clock.name;
        out.period = clock.period;
        out.has_waveform = clock.has_waveform;
        out.rise = clock.rise;
        out.fall = clock.fall;
        out.source = clock.source;
        out.generated = clock.generated;
        out.master_clock = clock.master_clock;
        out.source_pin = clock.source_pin;
        out.divide_by = clock.divide_by;
        out.multiply_by = clock.multiply_by;
        constraints.clocks.push_back(std::move(out));
    }

    constraints.clock_groups.reserve(sdc.clock_groups.size());
    for (const SdcClockGroup& group : sdc.clock_groups) {
        db::ConstraintClockGroup out;
        out.kind = to_db_kind(group.kind);
        out.name = group.name;
        out.groups = group.groups;
        constraints.clock_groups.push_back(std::move(out));
    }

    constraints.false_paths.reserve(sdc.false_paths.size());
    for (const SdcFalsePath& path : sdc.false_paths) {
        db::ConstraintFalsePath out;
        out.from = path.from;
        out.through = path.through;
        out.to = path.to;
        out.from_clocks = path.from_clocks;
        out.through_clocks = path.through_clocks;
        out.to_clocks = path.to_clocks;
        out.line = path.line;
        constraints.false_paths.push_back(std::move(out));
    }

    constraints.case_analysis.reserve(sdc.case_analysis.size());
    for (const SdcCaseAnalysis& item : sdc.case_analysis) {
        db::ConstraintCaseAnalysis out;
        out.object = item.object;
        out.value = item.value;
        constraints.case_analysis.push_back(std::move(out));
    }

    constraints.delays.reserve(sdc.delays.size());
    for (const SdcDelay& delay : sdc.delays) {
        db::ConstraintDelay out;
        out.kind = delay.kind == SdcDelayKind::Input ? db::DelayKind::Input : db::DelayKind::Output;
        out.object = delay.object;
        out.clock = delay.clock;
        out.value = delay.value;
        out.max = delay.max;
        out.min = delay.min;
        constraints.delays.push_back(std::move(out));
    }

    constraints.driving_cells.reserve(sdc.driving_cells.size());
    for (const SdcDrivingCell& cell : sdc.driving_cells) {
        db::ConstraintDrivingCell out;
        out.object = cell.object;
        out.lib_cell = cell.lib_cell;
        out.pin = cell.pin;
        constraints.driving_cells.push_back(std::move(out));
    }

    constraints.loads.reserve(sdc.loads.size());
    for (const SdcLoad& load : sdc.loads) {
        db::ConstraintLoad out;
        out.object = load.object;
        out.value = load.value;
        constraints.loads.push_back(std::move(out));
    }
    return constraints;
}

}  // namespace gmax::frontend
