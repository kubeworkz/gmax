#include "core/report/report.h"

#include <fstream>
#include <iomanip>
#include <ostream>
#include <sstream>
#include <string_view>
#include <utility>

namespace gmax::report {
namespace {

std::string json_string(const std::string_view text) {
    std::string out;
    out.reserve(text.size() + 2);
    out.push_back('"');
    for (const char raw : text) {
        const auto character = static_cast<unsigned char>(raw);
        switch (character) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\b':
            out += "\\b";
            break;
        case '\f':
            out += "\\f";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if (character < 0x20) {
                constexpr char kHex[] = "0123456789abcdef";
                out += "\\u00";
                out.push_back(kHex[(character >> 4) & 0x0Fu]);
                out.push_back(kHex[character & 0x0Fu]);
            } else {
                out.push_back(raw);
            }
            break;
        }
    }
    out.push_back('"');
    return out;
}

std::string percent(const std::size_t detected, const std::size_t denominator) {
    const double value =
        denominator == 0 ? 0.0 : 100.0 * static_cast<double>(detected) / denominator;
    std::ostringstream out;
    out << std::fixed << std::setprecision(6) << value;
    return out.str();
}

void coverage_entry(std::ostream& out, const std::string_view name, const std::size_t detected,
                    const std::size_t denominator) {
    out << '"' << name << "\":{\"detected\":" << detected << ",\"denominator\":" << denominator
        << ",\"percent\":" << percent(detected, denominator) << '}';
}

void stages_entry(std::ostream& out, const std::string_view name, const std::string_view path,
                  const std::uint32_t hash) {
    out << "{\"name\":" << json_string(name) << ",\"artifact\":" << json_string(path)
        << ",\"hash\":\"" << hex32(hash) << "\"}";
}

}  // namespace

std::string hex32(const std::uint32_t value) {
    std::ostringstream out;
    out << std::hex << std::setfill('0') << std::setw(8) << value;
    return out.str();
}

// ADR-013 section 6's `capture` object. An absent run has no procedure, so the
// kind, provenance and hash are null rather than invented; the manifest also lists
// the procedure body, which the coverage report leaves to its hash.
// ADR-013 section 5's DFT019 findings. One object per path, naming the rule, both
// clocks and the line the path was written on. An empty list is written, not omitted,
// so a reader can tell "no finding" from "not evaluated".
void false_paths_entry(std::ostringstream& out, const std::vector<FalsePathWarningReport>& paths) {
    out << ",\"false_paths\":[";
    for (std::size_t index = 0; index < paths.size(); ++index) {
        if (index != 0) {
            out << ',';
        }
        out << "{\"rule\":\"DFT019\",\"from\":" << json_string(paths[index].from)
            << ",\"to\":" << json_string(paths[index].to) << ",\"line\":" << paths[index].line
            << '}';
    }
    out << ']';
}

void capture_entry(std::ostringstream& out, const CaptureReport& capture, const bool with_cycles) {
    out << "{\"status\":\"" << (capture.absent ? "absent" : "enforced") << "\",\"procedure\":";
    if (capture.absent) {
        out << "null";
    } else {
        out << json_string(capture.procedure);
    }
    out << ",\"derived\":" << (capture.derived ? "true" : "false") << ",\"hash\":";
    if (capture.absent) {
        out << "null";
    } else {
        out << '"' << hex32(capture.hash) << '"';
    }
    if (with_cycles) {
        out << ",\"cycles\":[";
        for (std::size_t c = 0; c < capture.cycles.size(); ++c) {
            if (c != 0) {
                out << ',';
            }
            out << "{\"groups\":[";
            for (std::size_t g = 0; g < capture.cycles[c].groups.size(); ++g) {
                if (g != 0) {
                    out << ',';
                }
                const CaptureGroupReport& group = capture.cycles[c].groups[g];
                out << "{\"clocks\":[";
                for (std::size_t k = 0; k < group.clocks.size(); ++k) {
                    if (k != 0) {
                        out << ',';
                    }
                    out << json_string(group.clocks[k]);
                }
                out << "],\"edge\":" << db::canonical_decimal(group.edge) << '}';
            }
            out << "]}";
        }
        out << ']';
    }
    out << '}';
}

std::vector<std::string> enforced_constructs(const bool case_analysis, const CaptureReport& capture,
                                             const bool false_paths) {
    std::vector<std::string> enforced;
    if (case_analysis) {
        enforced.emplace_back("case_analysis");
    }
    if (!capture.absent) {
        enforced.emplace_back("clock_groups");
    }
    // A procedure that did not run examined no path, so a retained set alone never
    // names the construct.
    if (!capture.absent && false_paths) {
        enforced.emplace_back("false_paths");
    }
    return enforced;
}

std::string coverage_json(const CoverageReport& report) {
    const CoverageSummary& summary = report.summary;
    std::ostringstream out;
    out << "{\"schema\":\"gmax-coverage-4\"";
    out << ",\"design\":" << json_string(report.design);
    out << ",\"netlist_hash\":\"" << hex32(report.netlist_hash) << '"';
    out << ",\"fault_list_hash\":\"" << hex32(report.fault_list_hash) << '"';
    // ADR-012: a separate identity and status. An absent set reports a null
    // hash rather than a hash of nothing. ADR-013 section 6 replaces the
    // not-enforced capture string with a `capture` object beside the enforced
    // list, which names only what the run applied.
    out << ",\"constraints\":{\"status\":\""
        << db::constraints_status_name(report.constraints_status) << "\",\"hash\":";
    if (report.constraints_status == db::ConstraintsStatus::Absent) {
        out << "null";
    } else {
        out << '"' << hex32(report.constraints_hash) << '"';
    }
    out << ",\"algorithm\":\"crc32\",\"enforced\":[";
    for (std::size_t index = 0; index < report.enforced.size(); ++index) {
        if (index != 0) {
            out << ',';
        }
        out << json_string(report.enforced[index]);
    }
    out << ']';
    false_paths_entry(out, report.false_paths);
    out << ",\"capture\":";
    capture_entry(out, report.capture, false);
    out << '}';
    out << ",\"counts\":{\"raw\":" << summary.raw << ",\"collapsed\":" << summary.collapsed
        << ",\"detected\":" << summary.detected << ",\"untestable\":" << summary.untestable
        << ",\"aborted\":" << summary.aborted << ",\"raw_detected\":" << summary.raw_detected
        << ",\"raw_untestable\":" << summary.raw_untestable
        << ",\"raw_aborted\":" << summary.raw_aborted << ",\"masked\":" << summary.masked
        << ",\"raw_masked\":" << summary.raw_masked << '}';
    out << ",\"coverage\":{";
    coverage_entry(out, "raw", summary.raw_detected, summary.raw);
    out << ',';
    coverage_entry(out, "collapsed", summary.detected, summary.collapsed);
    out << ',';
    coverage_entry(out, "testable", summary.detected, summary.detected + summary.aborted);
    out << ',';
    coverage_entry(out, "atpg", summary.detected, summary.collapsed);
    out << '}';
    out << ",\"budget\":{\"max_backtracks\":" << report.budget.max_backtracks
        << ",\"max_time_ms\":" << report.budget.max_time_ms << '}';
    out << ",\"totals\":{\"decisions\":" << summary.decisions
        << ",\"backtracks\":" << summary.backtracks << '}';
    out << ",\"faults\":[";
    for (std::size_t index = 0; index < report.faults.size(); ++index) {
        const FaultOutcome& fault = report.faults[index];
        if (index != 0) {
            out << ',';
        }
        out << "{\"net\":" << fault.fault.net << ",\"name\":" << json_string(fault.net_name)
            << ",\"stuck\":" << (fault.fault.stuck == sim::StuckValue::One ? 1 : 0)
            << ",\"status\":\""
            << (fault.masked ? std::string_view("masked") : atpg::podem_status_name(fault.status))
            << '"' << ",\"method\":" << json_string(fault.method)
            << ",\"decisions\":" << fault.decisions << ",\"backtracks\":" << fault.backtracks
            << '}';
    }
    out << "]}";
    return out.str();
}

std::string manifest_json(const RunManifest& manifest) {
    const CoverageSummary& summary = manifest.summary;
    std::ostringstream out;
    out << "{\"schema\":\"gmax-run-manifest-4\"";
    out << ",\"tool\":{\"name\":\"gmax\",\"version\":" << json_string(manifest.tool_version) << '}';
    out << ",\"command\":\"run_atpg\"";
    out << ",\"design\":{\"name\":" << json_string(manifest.design)
        << ",\"gates\":" << manifest.gates << ",\"nets\":" << manifest.nets << '}';
    // ADR-013 section 6: a run with a profile reports it as an input with the same
    // path, hash and algorithm shape the netlist and fault list use, so a masked run
    // can be audited from its own report instead of the command line that produced it.
    out << ",\"inputs\":{\"profile\":";
    if (manifest.profile_path.empty()) {
        out << "null";
    } else {
        out << "{\"path\":" << json_string(manifest.profile_path) << ",\"hash\":\""
            << hex32(manifest.profile_hash) << "\",\"algorithm\":\"crc32\"}";
    }
    out << ",\"libraries\":[]";
    out << ",\"netlist\":{\"path\":" << json_string(manifest.design_path) << ",\"hash\":\""
        << hex32(manifest.netlist_hash) << "\",\"algorithm\":\"crc32\"}";
    out << ",\"fault_list\":{\"path\":" << json_string(manifest.faults_path) << ",\"hash\":\""
        << hex32(manifest.fault_list_hash) << "\",\"algorithm\":\"crc32\""
        << ",\"raw_count\":" << summary.raw << ",\"collapsed_count\":" << summary.collapsed << "}";
    out << ",\"constraints\":{\"path\":" << json_string(manifest.constraints_path)
        << ",\"status\":\"" << db::constraints_status_name(manifest.constraints_status)
        << "\",\"hash\":";
    if (manifest.constraints_status == db::ConstraintsStatus::Absent) {
        out << "null";
    } else {
        out << '"' << hex32(manifest.constraints_hash) << '"';
    }
    out << ",\"algorithm\":\"crc32\"";
    false_paths_entry(out, manifest.false_paths);
    out << ",\"capture\":";
    capture_entry(out, manifest.capture, true);
    // The capture object closes itself; these close the constraints object and the
    // inputs object that holds it.
    out << "}}";
    out << ",\"coverage\":{\"path\":" << json_string(manifest.coverage_path) << ",\"hash\":\""
        << hex32(manifest.coverage_hash) << "\",\"algorithm\":\"crc32\"}";
    out << ",\"options\":{\"max_backtracks\":" << manifest.budget.max_backtracks
        << ",\"max_time_ms\":" << manifest.budget.max_time_ms << ",\"threads\":" << manifest.threads
        << ",\"seed\":0}";
    out << ",\"stages\":[";
    stages_entry(out, "elaborate", manifest.design_path, manifest.netlist_hash);
    out << ',';
    stages_entry(out, "fault_list", manifest.faults_path, manifest.fault_list_hash);
    out << ',';
    stages_entry(out, "atpg", manifest.coverage_path, manifest.coverage_hash);
    out << ']';
    out << ",\"result\":{\"status\":\"" << (summary.aborted == 0 ? "complete" : "incomplete")
        << "\",\"detected\":" << summary.detected << ",\"untestable\":" << summary.untestable
        << ",\"aborted\":" << summary.aborted << "}}";
    return out.str();
}

std::string sat_status_name(const atpg::SatFaultStatus status) {
    switch (status) {
    case atpg::SatFaultStatus::Detected:
        return "detected";
    case atpg::SatFaultStatus::Untestable:
        return "untestable";
    case atpg::SatFaultStatus::Unsupported:
        return "unsupported";
    case atpg::SatFaultStatus::Unknown:
        return "unknown";
    }
    return "unknown";
}

std::string sat_verdict(const atpg::SatFaultStatus status) {
    if (status == atpg::SatFaultStatus::Detected) {
        return "sat";
    }
    if (status == atpg::SatFaultStatus::Untestable) {
        return "unsat";
    }
    return "unknown";
}

std::string sat_smoke_json(const SatSmokeReport& report) {
    std::size_t sat = 0;
    std::size_t unsat = 0;
    std::size_t unknown = 0;
    for (const SatSmokeCase& item : report.cases) {
        if (item.status == atpg::SatFaultStatus::Detected) {
            ++sat;
        } else if (item.status == atpg::SatFaultStatus::Untestable) {
            ++unsat;
        } else {
            ++unknown;
        }
    }

    std::ostringstream out;
    out << "{\"schema\":\"gmax-sat-smoke-1\"";
    out << ",\"design\":" << json_string(report.design);
    out << ",\"netlist_hash\":\"" << hex32(report.netlist_hash) << '"';
    out << ",\"fault_list_hash\":\"" << hex32(report.fault_list_hash) << '"';
    out << ",\"solver\":{\"backend\":\"internal-cdcl\",\"max_conflicts\":1000000}";
    out << ",\"cases\":[";
    for (std::size_t index = 0; index < report.cases.size(); ++index) {
        const SatSmokeCase& item = report.cases[index];
        if (index != 0) {
            out << ',';
        }
        out << "{\"net\":" << item.fault.net << ",\"name\":" << json_string(item.net_name)
            << ",\"stuck\":" << (item.fault.stuck == sim::StuckValue::One ? 1 : 0)
            << ",\"expected\":" << json_string(item.expected)
            << ",\"status\":" << json_string(sat_status_name(item.status))
            << ",\"verdict\":" << json_string(sat_verdict(item.status))
            << ",\"conflicts\":" << item.conflicts << ",\"variables\":" << item.variables
            << ",\"clauses\":" << item.clauses
            << ",\"solver_timed_out\":" << (item.solver_timed_out ? "true" : "false") << '}';
    }
    out << "],\"summary\":{\"sat\":" << sat << ",\"unsat\":" << unsat << ",\"unknown\":" << unknown
        << "}}";
    return out.str();
}

db::Status write_text(const std::string& path, const std::string_view text) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        return db::Status::failure(db::Error::IoError, "cannot open '" + path + "' for writing");
    }
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!out) {
        return db::Status::failure(db::Error::IoError, "cannot write '" + path + "'");
    }
    return {};
}

}  // namespace gmax::report
