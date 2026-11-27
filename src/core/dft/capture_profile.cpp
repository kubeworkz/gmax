#include "core/dft/capture_profile.h"

#include <cmath>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/db/constraints.h"
#include "core/dft/capture.h"
#include "core/frontend/diagnostics.h"
#include "core/frontend/json.h"

namespace gmax::dft {
namespace {

using frontend::Diagnostic;
using frontend::DiagnosticCode;
using frontend::DiagnosticSeverity;
using frontend::JsonValue;

std::string at(std::string base, const std::string_view key) {
    base.push_back('.');
    base.append(key);
    return base;
}

std::string at(std::string base, const std::size_t index) {
    base.push_back('[');
    base.append(std::to_string(index));
    base.push_back(']');
    return base;
}

void add(std::vector<Diagnostic>& diagnostics, const DiagnosticCode code, std::string where,
         std::string message, const std::string_view source) {
    Diagnostic diagnostic;
    diagnostic.code = code;
    diagnostic.severity = DiagnosticSeverity::Error;
    diagnostic.message = std::move(message);
    diagnostic.object = std::move(where);
    diagnostic.source.file = std::string(source);
    diagnostics.push_back(std::move(diagnostic));
}

bool declares_clock(const db::Constraints& constraints, const std::string_view name) {
    for (const db::ConstraintClock& clock : constraints.clocks) {
        if (clock.name == name) {
            return true;
        }
    }
    return false;
}

bool parse_kind(const std::string_view name, CaptureKind& kind) {
    for (const CaptureKind candidate :
         {CaptureKind::Simultaneous, CaptureKind::Grouped, CaptureKind::Sequential}) {
        if (capture_kind_name(candidate) == name) {
            kind = candidate;
            return true;
        }
    }
    return false;
}

// Section 1's shapes: simultaneous is one cycle of one group, grouped is one
// cycle of two or more groups, sequential is two or more single-group cycles.
void check_shape(const CaptureProcedure& procedure, std::vector<Diagnostic>& diagnostics,
                 const std::string_view source) {
    const std::size_t cycles = procedure.cycles.size();
    const std::string where = "capture.cycles";
    if (procedure.kind == CaptureKind::Simultaneous) {
        if (cycles != 1 || procedure.cycles[0].groups.size() != 1) {
            add(diagnostics, DiagnosticCode::CaptureProfileInvalid, where,
                "simultaneous capture is one cycle with one group", source);
        }
        return;
    }
    if (procedure.kind == CaptureKind::Grouped) {
        if (cycles != 1 || procedure.cycles[0].groups.size() < 2) {
            add(diagnostics, DiagnosticCode::CaptureProfileInvalid, where,
                "grouped capture is one cycle with at least two groups", source);
        }
        return;
    }
    if (cycles < 2) {
        add(diagnostics, DiagnosticCode::CaptureProfileInvalid, where,
            "sequential capture needs at least two cycles", source);
    }
    for (std::size_t c = 0; c < cycles; ++c) {
        if (procedure.cycles[c].groups.size() != 1) {
            add(diagnostics, DiagnosticCode::CaptureProfileInvalid, at(where, c),
                "sequential capture pulses one group per cycle", source);
        }
    }
}

void read_groups(const JsonValue& cycle_value, const db::Constraints& constraints,
                 const std::string& cycle_path, CaptureCycle& cycle,
                 std::vector<Diagnostic>& diagnostics, const std::string_view source) {
    const JsonValue* groups = cycle_value.find("groups");
    if (groups == nullptr || !groups->is_array() || groups->array.empty()) {
        add(diagnostics, DiagnosticCode::CaptureProfileInvalid, at(cycle_path, "groups"),
            "cycle needs a non-empty groups array", source);
        return;
    }

    std::size_t with_edge = 0;
    for (std::size_t g = 0; g < groups->array.size(); ++g) {
        const JsonValue* group_value = groups->array[g].get();
        const std::string group_path = at(at(cycle_path, "groups"), g);
        if (group_value == nullptr || !group_value->is_object()) {
            add(diagnostics, DiagnosticCode::CaptureProfileSyntax, group_path,
                "group must be an object", source);
            continue;
        }

        const JsonValue* clocks = group_value->find("clocks");
        if (clocks == nullptr || !clocks->is_array() || clocks->array.empty()) {
            add(diagnostics, DiagnosticCode::CaptureProfileInvalid, at(group_path, "clocks"),
                "group names no clock", source);
            continue;
        }

        std::vector<std::string> names;
        names.reserve(clocks->array.size());
        for (std::size_t k = 0; k < clocks->array.size(); ++k) {
            const JsonValue* clock_value = clocks->array[k].get();
            const std::string clock_path = at(at(group_path, "clocks"), k);
            if (clock_value == nullptr || !clock_value->is_string() || clock_value->text.empty()) {
                add(diagnostics, DiagnosticCode::CaptureProfileSyntax, clock_path,
                    "clock name must be a non-empty string", source);
                continue;
            }
            if (!declares_clock(constraints, clock_value->text)) {
                add(diagnostics, DiagnosticCode::CaptureProfileUnknownClock, clock_path,
                    "clock '" + clock_value->text + "' is not declared in the constraints", source);
                continue;
            }
            names.push_back(clock_value->text);
        }
        if (names.empty()) {
            continue;
        }

        double edge = 0.0;
        if (const JsonValue* edge_value = group_value->find("edge")) {
            if (!edge_value->is_number() || !std::isfinite(edge_value->number) ||
                edge_value->number < 0.0) {
                add(diagnostics, DiagnosticCode::CaptureProfileInvalid, at(group_path, "edge"),
                    "edge must be a finite non-negative number", source);
                continue;
            }
            edge = edge_value->number;
            ++with_edge;
        }

        CaptureGroup group;
        if (const JsonValue* name = group_value->find("name")) {
            if (!name->is_string()) {
                add(diagnostics, DiagnosticCode::CaptureProfileSyntax, at(group_path, "name"),
                    "group name must be a string", source);
                continue;
            }
            group.name = name->text;
        }
        group.timing = capture_timing_set(constraints, names, edge);
        cycle.groups.push_back(std::move(group));
    }

    // A half-specified layout is ambiguous: either the profile places every
    // group of the cycle or it leaves section 3's default to place all of them.
    if (with_edge != 0 && with_edge != cycle.groups.size()) {
        add(diagnostics, DiagnosticCode::CaptureProfileInvalid, cycle_path,
            "cycle gives an edge for some groups and not others", source);
    }
    if (with_edge == 0) {
        assign_capture_edges(cycle.groups);
    }
}

}  // namespace

CaptureProfileResult read_capture_profile(const std::string_view text,
                                          const db::Constraints& constraints,
                                          const std::string_view source) {
    CaptureProfileResult result;

    JsonValue root;
    std::string error;
    if (!frontend::parse_json(text, root, error)) {
        add(result.diagnostics, DiagnosticCode::InvalidJson, {}, std::move(error), source);
        return result;
    }
    if (!root.is_object()) {
        add(result.diagnostics, DiagnosticCode::CaptureProfileSyntax, {},
            "profile is not a JSON object", source);
        return result;
    }

    const JsonValue* capture = root.find("capture");
    if (capture == nullptr || capture->kind == JsonValue::Kind::Null) {
        result.ok = true;
        return result;
    }
    result.declared = true;
    if (!capture->is_object()) {
        add(result.diagnostics, DiagnosticCode::CaptureProfileSyntax, "capture",
            "capture must be an object", source);
        return result;
    }

    const JsonValue* kind = capture->find("kind");
    if (kind == nullptr || !kind->is_string()) {
        add(result.diagnostics, DiagnosticCode::CaptureProfileSyntax, "capture.kind",
            "capture kind must be a string", source);
        return result;
    }
    if (!parse_kind(kind->text, result.procedure.kind)) {
        add(result.diagnostics, DiagnosticCode::CaptureProfileInvalid, "capture.kind",
            "unknown capture kind '" + kind->text + "'", source);
        return result;
    }
    result.procedure.derived = false;

    const JsonValue* cycles = capture->find("cycles");
    if (cycles == nullptr || !cycles->is_array() || cycles->array.empty()) {
        add(result.diagnostics, DiagnosticCode::CaptureProfileInvalid, "capture.cycles",
            "capture needs a non-empty cycles array", source);
        return result;
    }

    for (std::size_t c = 0; c < cycles->array.size(); ++c) {
        const JsonValue* cycle_value = cycles->array[c].get();
        const std::string cycle_path = at("capture.cycles", c);
        if (cycle_value == nullptr || !cycle_value->is_object()) {
            add(result.diagnostics, DiagnosticCode::CaptureProfileSyntax, cycle_path,
                "cycle must be an object", source);
            continue;
        }
        CaptureCycle cycle;
        read_groups(*cycle_value, constraints, cycle_path, cycle, result.diagnostics, source);
        if (cycle.groups.empty()) {
            continue;
        }
        result.procedure.cycles.push_back(std::move(cycle));
    }

    if (frontend::has_errors(result.diagnostics)) {
        return result;
    }
    check_shape(result.procedure, result.diagnostics, source);
    result.ok = !frontend::has_errors(result.diagnostics);
    return result;
}

}  // namespace gmax::dft
