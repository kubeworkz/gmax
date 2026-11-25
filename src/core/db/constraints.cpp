#include "core/db/constraints.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "core/db/crc32.h"
#include "core/db/flat.h"
#include "core/db/string_table.h"

namespace gmax::db {
namespace {

void append(std::string& out, std::string_view field) {
    out.append(field);
    out.push_back('\t');
}

void append_number(std::string& out, const double value) {
    out.append(canonical_decimal(value));
    out.push_back('\t');
}

void append_number(std::string& out, const int value) {
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

// Records are emitted one per line, then sorted line-wise. Because each line
// begins with its record kind and then the ADR-012 primary sort keys in order, a
// plain lexicographic sort of the lines is exactly the ADR's ordering rule, and
// it cannot leave two equal-keyed records in source order.
void append_block(std::string& out, const std::vector<std::string>& lines) {
    std::vector<std::string> sorted = lines;
    std::sort(sorted.begin(), sorted.end());
    for (const std::string& line : sorted) {
        out.append(line);
        out.push_back('\n');
    }
}

}  // namespace

std::string_view constraints_status_name(const ConstraintsStatus status) {
    switch (status) {
    case ConstraintsStatus::Absent:
        return "absent";
    case ConstraintsStatus::Complete:
        return "complete";
    case ConstraintsStatus::Incomplete:
        return "incomplete";
    }
    return "absent";
}

std::string_view clock_group_kind_name(const ClockGroupKind kind) {
    switch (kind) {
    case ClockGroupKind::Asynchronous:
        return "asynchronous";
    case ClockGroupKind::LogicallyExclusive:
        return "logically_exclusive";
    case ClockGroupKind::PhysicallyExclusive:
        return "physically_exclusive";
    }
    return "asynchronous";
}

ConstraintsStatus constraints_status(const Constraints& constraints) {
    if (!constraints.present) {
        return ConstraintsStatus::Absent;
    }
    return constraints.complete ? ConstraintsStatus::Complete : ConstraintsStatus::Incomplete;
}

std::string canonical_decimal(const double value) {
    // Shortest representation that round-trips to the same double. This is the
    // encoding the hash contract fixes: 10, 10.0 and 1e1 all encode as "10",
    // and the digits never depend on the source file's formatting.
    if (value == 0.0) {
        return "0";  // also normalizes negative zero
    }
    std::array<char, 64> buffer{};
    const auto result = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
    if (result.ec != std::errc{}) {
        return "0";
    }
    return std::string(buffer.data(), result.ptr);
}

std::string canonical_constraints_payload(const Constraints& constraints) {
    std::string out = "gmax-constraints-1\n";
    append(out, "status");
    out.append(constraints.complete ? "complete" : "incomplete");
    out.push_back('\n');

    // Clocks and generated clocks share one name-ordered block; the record kind
    // keeps the two shapes unambiguous.
    std::vector<std::string> clocks;
    for (const ConstraintClock& clock : constraints.clocks) {
        std::string line;
        append(line, clock.generated ? "gclock" : "clock");
        append(line, clock.name);
        append_number(line, clock.period);
        // The waveform flag is always written, so a record's field count never
        // depends on the source text.
        append(line, clock.has_waveform ? "waveform" : "nowaveform");
        append_number(line, clock.rise);
        append_number(line, clock.fall);
        if (clock.generated) {
            append(line, clock.source);
            append(line, clock.master_clock);
            append(line, clock.source_pin);
            append_number(line, clock.divide_by);
            append_number(line, clock.multiply_by);
        }
        clocks.push_back(std::move(line));
    }
    append_block(out, clocks);

    std::vector<std::string> groups;
    for (const ConstraintClockGroup& group : constraints.clock_groups) {
        std::string line;
        append(line, "group");
        append_number(line, static_cast<int>(group.kind));
        append(line, group.name);
        // -group order carries no meaning, so the outer list is sorted too: two
        // files that differ only in -group order are one constraint set.
        std::vector<std::string> members;
        members.reserve(group.groups.size());
        for (const std::vector<std::string>& clocks : group.groups) {
            std::vector<std::string> sorted = clocks;
            std::sort(sorted.begin(), sorted.end());
            members.push_back(join(sorted));
        }
        std::sort(members.begin(), members.end());
        for (const std::string& entry : members) {
            append(line, entry);
        }
        groups.push_back(std::move(line));
    }
    append_block(out, groups);

    std::vector<std::string> false_paths;
    for (const ConstraintFalsePath& path : constraints.false_paths) {
        std::string line;
        append(line, "false");
        std::vector<std::string> from = path.from;
        std::vector<std::string> through = path.through;
        std::vector<std::string> to = path.to;
        std::sort(from.begin(), from.end());
        std::sort(through.begin(), through.end());
        std::sort(to.begin(), to.end());
        append(line, join(from));
        append(line, join(through));
        append(line, join(to));
        false_paths.push_back(std::move(line));
    }
    append_block(out, false_paths);

    std::vector<std::string> case_analysis;
    for (const ConstraintCaseAnalysis& item : constraints.case_analysis) {
        std::string line;
        append(line, "case");
        append(line, item.object);
        append(line, item.value ? "1" : "0");
        case_analysis.push_back(std::move(line));
    }
    append_block(out, case_analysis);

    std::vector<std::string> delays;
    for (const ConstraintDelay& delay : constraints.delays) {
        std::string line;
        append(line, "delay");
        append(line, delay.kind == DelayKind::Input ? "input" : "output");
        append(line, delay.object);
        append(line, delay.clock);
        append(line, delay.max ? "max" : (delay.min ? "min" : "typ"));
        append_number(line, delay.value);
        delays.push_back(std::move(line));
    }
    append_block(out, delays);

    std::vector<std::string> driving_cells;
    for (const ConstraintDrivingCell& cell : constraints.driving_cells) {
        std::string line;
        append(line, "cell");
        append(line, cell.object);
        append(line, cell.lib_cell);
        append(line, cell.pin);
        driving_cells.push_back(std::move(line));
    }
    append_block(out, driving_cells);

    std::vector<std::string> loads;
    for (const ConstraintLoad& load : constraints.loads) {
        std::string line;
        append(line, "load");
        append(line, load.object);
        append_number(line, load.value);
        loads.push_back(std::move(line));
    }
    append_block(out, loads);

    std::vector<std::string> unsupported = constraints.unsupported;
    std::sort(unsupported.begin(), unsupported.end());
    unsupported.erase(std::unique(unsupported.begin(), unsupported.end()), unsupported.end());
    for (const std::string& name : unsupported) {
        append(out, "unsupported");
        append(out, name);
        out.push_back('\n');
    }
    return out;
}

std::uint32_t constraints_hash(const Constraints& constraints) {
    const std::string payload = canonical_constraints_payload(constraints);
    return crc32(reinterpret_cast<const std::uint8_t*>(payload.data()), payload.size());
}

bool enforceable_case_target(const std::string_view name, const FlatNetlist& flat,
                             const StringTable& strings) {
    for (NetId net = 0; net < flat.net_count(); ++net) {
        if (strings.at(flat.name_id[net]) == name) {
            // ADR-012 section 4: only nets that resolve to a top-level input port
            // are enforceable; every other target makes the set incomplete.
            return flat.driver[net] == kPrimaryInput;
        }
    }
    return false;
}

}  // namespace gmax::db
