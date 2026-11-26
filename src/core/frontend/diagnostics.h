#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace gmax::frontend {

enum class DiagnosticSeverity : std::uint8_t { Warning, Error };

// One code per distinct front-end finding. The names are kebab-case so a report
// can carry them without knowing the C++ enum.
enum class DiagnosticCode : std::uint8_t {
    InvalidJson,
    InvalidTop,
    YosysNotFound,
    YosysFailed,
    YosysWarning,
    YosysError,
    UnresolvedModule,
    MultipleDrivers,
    UndrivenInput,
    CombinationalLoop,
    WidthMismatch,
    UnmappedCell,
    UnsupportedPort,
    UnsupportedConstant,
    CellCountMismatch,
    IoError,
    SdcSyntax,
    SdcUnsupportedCommand,
    SdcUnsupportedOption,
    SdcInvalidValue,
    SdcDuplicateClock,
    SdcClockGroupUnknownMember,
    SdcClockGroupConflictingMember,
    SdcGeneratedClockUnknownMaster,
    SdcGeneratedClockConflictingMaster,
    SdcGeneratedClockAmbiguousSource,
    SdcGeneratedClockMasterCycle,
};

std::string_view diagnostic_code_name(DiagnosticCode code);
std::string_view diagnostic_severity_name(DiagnosticSeverity severity);

struct SourceLocation {
    std::string file;
    std::uint32_t line = 0;
    std::uint32_t column = 0;

    bool empty() const { return file.empty() && line == 0 && column == 0; }
};

// A finding is data, not a log line: the caller can render it, count it, or
// fail a gate on it. `object` is the module, cell or net the finding is about.
struct Diagnostic {
    DiagnosticCode code = DiagnosticCode::YosysError;
    DiagnosticSeverity severity = DiagnosticSeverity::Error;
    std::string message;
    std::string object;
    SourceLocation source;

    bool is_error() const { return severity == DiagnosticSeverity::Error; }
};

inline bool has_errors(const std::vector<Diagnostic>& diagnostics) {
    for (const Diagnostic& diagnostic : diagnostics) {
        if (diagnostic.is_error()) {
            return true;
        }
    }
    return false;
}

}  // namespace gmax::frontend
