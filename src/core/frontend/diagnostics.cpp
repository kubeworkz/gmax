#include "core/frontend/diagnostics.h"

namespace gmax::frontend {

std::string_view diagnostic_code_name(const DiagnosticCode code) {
    switch (code) {
    case DiagnosticCode::InvalidJson:
        return "invalid-json";
    case DiagnosticCode::InvalidTop:
        return "invalid-top";
    case DiagnosticCode::YosysNotFound:
        return "yosys-not-found";
    case DiagnosticCode::YosysFailed:
        return "yosys-failed";
    case DiagnosticCode::YosysWarning:
        return "yosys-warning";
    case DiagnosticCode::YosysError:
        return "yosys-error";
    case DiagnosticCode::UnresolvedModule:
        return "unresolved-module";
    case DiagnosticCode::MultipleDrivers:
        return "multiple-drivers";
    case DiagnosticCode::UndrivenInput:
        return "undriven-input";
    case DiagnosticCode::CombinationalLoop:
        return "combinational-loop";
    case DiagnosticCode::WidthMismatch:
        return "width-mismatch";
    case DiagnosticCode::UnmappedCell:
        return "unmapped-cell";
    case DiagnosticCode::UnsupportedPort:
        return "unsupported-port";
    case DiagnosticCode::UnsupportedConstant:
        return "unsupported-constant";
    case DiagnosticCode::CellCountMismatch:
        return "cell-count-mismatch";
    case DiagnosticCode::IoError:
        return "io-error";
    case DiagnosticCode::SdcSyntax:
        return "sdc-syntax";
    case DiagnosticCode::SdcUnsupportedCommand:
        return "sdc-unsupported-command";
    case DiagnosticCode::SdcUnsupportedOption:
        return "sdc-unsupported-option";
    case DiagnosticCode::SdcInvalidValue:
        return "sdc-invalid-value";
    case DiagnosticCode::SdcDuplicateClock:
        return "sdc-duplicate-clock";
    case DiagnosticCode::SdcClockGroupUnknownMember:
        return "sdc-clock-group-unknown-member";
    case DiagnosticCode::SdcClockGroupConflictingMember:
        return "sdc-clock-group-conflicting-member";
    case DiagnosticCode::SdcGeneratedClockUnknownMaster:
        return "sdc-generated-clock-unknown-master";
    case DiagnosticCode::SdcGeneratedClockConflictingMaster:
        return "sdc-generated-clock-conflicting-master";
    case DiagnosticCode::SdcGeneratedClockAmbiguousSource:
        return "sdc-generated-clock-ambiguous-source";
    case DiagnosticCode::SdcGeneratedClockMasterCycle:
        return "sdc-generated-clock-master-cycle";
    }
    return "unknown";
}

std::string_view diagnostic_severity_name(const DiagnosticSeverity severity) {
    return severity == DiagnosticSeverity::Error ? "error" : "warning";
}

}  // namespace gmax::frontend
