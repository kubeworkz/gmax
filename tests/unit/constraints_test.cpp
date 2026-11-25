// ADR-012: the persisted constraint model, its canonical payload and hash, and
// the .dfdb 1.4 section. Framework-free like the other tests.

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "core/atpg/podem.h"
#include "core/atpg/sat.h"
#include "core/db/constraints.h"
#include "core/db/db.h"
#include "core/db/dfdb.h"
#include "core/db/flat.h"
#include "core/db/string_table.h"
#include "core/report/report.h"

namespace db = gmax::db;

namespace {

int failures = 0;

void expect(const bool ok, const std::string_view what) {
    if (!ok) {
        std::cerr << "FAIL " << what << std::endl;
        ++failures;
    }
}

db::Constraints make_constraints() {
    db::Constraints constraints;
    constraints.present = true;
    constraints.complete = true;

    db::ConstraintClock fast;
    fast.name = "clk_a";
    fast.period = 10.0;
    fast.has_waveform = true;
    fast.rise = 0.0;
    fast.fall = 5.0;
    fast.source = "clk_a";

    db::ConstraintClock slow;
    slow.name = "clk_b";
    slow.period = 20.0;
    slow.source = "clk_b";

    constraints.clocks = {fast, slow};

    db::ConstraintCaseAnalysis mode;
    mode.object = "test_mode";
    mode.value = true;
    constraints.case_analysis = {mode};

    db::ConstraintClockGroup group;
    group.kind = db::ClockGroupKind::Asynchronous;
    group.groups = {{"clk_b"}, {"clk_a"}};
    constraints.clock_groups = {group};

    db::ConstraintFalsePath path;
    path.from = {"test_mode"};
    path.to = {"clk_a"};
    constraints.false_paths = {path};

    return constraints;
}

void test_canonical_decimal() {
    expect(db::canonical_decimal(10.0) == "10", "10.0 encodes as 10");
    expect(db::canonical_decimal(1e1) == "10", "1e1 encodes as 10, like 10.0");
    expect(db::canonical_decimal(0.0) == "0", "zero encodes as 0");
    expect(db::canonical_decimal(-0.0) == "0", "negative zero normalizes to 0");
    expect(db::canonical_decimal(0.5) == "0.5", "a half encodes as 0.5");
    expect(db::canonical_decimal(10.0) == db::canonical_decimal(10.00),
           "source formatting does not reach the encoding");
}

void test_hash_is_order_independent() {
    const db::Constraints first = make_constraints();
    db::Constraints second = first;
    std::swap(second.clocks[0], second.clocks[1]);
    std::swap(second.clock_groups[0].groups[0], second.clock_groups[0].groups[1]);
    second.false_paths[0].from = {"test_mode"};
    expect(db::canonical_constraints_payload(first) == db::canonical_constraints_payload(second),
           "record insertion order does not reach the canonical payload");
    expect(db::constraints_hash(first) == db::constraints_hash(second),
           "insertion order does not reach the constraints hash");

    db::Constraints reordered = first;
    reordered.clocks[0].name = "clk_a";
    reordered.clocks[0].period = 100.0;
    expect(db::constraints_hash(reordered) != db::constraints_hash(first),
           "a different period is a different set");
}

void test_incomplete_sets() {
    db::Constraints first = make_constraints();
    first.complete = false;
    first.unsupported = {"set_multicycle_path", "set_max_delay"};
    db::Constraints second = first;
    std::swap(second.unsupported[0], second.unsupported[1]);
    expect(db::constraints_hash(first) == db::constraints_hash(second),
           "unsupported names are deduplicated and sorted");

    db::Constraints third = first;
    third.unsupported = {"set_multicycle_path", "set_min_delay"};
    expect(db::constraints_hash(first) != db::constraints_hash(third),
           "a different unsupported construct name is a different payload");

    db::Constraints complete = make_constraints();
    expect(db::constraints_hash(first) != db::constraints_hash(complete),
           "completeness is part of the hash");
}

void test_status_and_absent() {
    db::Constraints absent;
    expect(db::constraints_status(absent) == db::ConstraintsStatus::Absent,
           "a database with no section is absent");
    db::Constraints empty;
    empty.present = true;
    expect(db::constraints_status(empty) == db::ConstraintsStatus::Complete,
           "an attached empty set is complete, not absent");
    db::Constraints incomplete = empty;
    incomplete.complete = false;
    expect(db::constraints_status(incomplete) == db::ConstraintsStatus::Incomplete,
           "an incomplete set reports incomplete");
}

// A two-net inverter: y = ~a. Enough to exercise the schema and the engines.
struct Fixture {
    db::Design design;
    db::FlatNetlist flat;
    db::StringTable strings;
};

Fixture make_inverter() {
    Fixture fixture;
    fixture.design.top = "top";
    db::Module module;
    module.name = "top";
    module.inputs = {"a"};
    module.outputs = {"y"};
    db::Gate inverter;
    inverter.type = db::GateType::Not;
    inverter.name = "g0";
    inverter.fanins = {"a"};
    inverter.out = "y";
    module.gates = {inverter};
    fixture.design.modules = {module};
    return fixture;
}

void test_schema_round_trip() {
    Fixture fixture = make_inverter();
    db::DesignDb database;
    database.design() = fixture.design;
    expect(database.flatten().ok(), "the inverter flattens");

    const std::filesystem::path bare =
        std::filesystem::temp_directory_path() / "gmax-constraints-bare.dfdb";
    const std::filesystem::path attached =
        std::filesystem::temp_directory_path() / "gmax-constraints-attached.dfdb";

    expect(database.save(bare.string()).ok(), "an unconstrained database saves");
    database.constraints() = make_constraints();
    expect(database.save(attached.string()).ok(), "a constrained database saves");

    const auto bytes = [](const std::filesystem::path& path) {
        std::ifstream in(path, std::ios::binary);
        return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(in),
                                         std::istreambuf_iterator<char>());
    };
    const std::vector<std::uint8_t> bare_bytes = bytes(bare);
    const std::vector<std::uint8_t> attached_bytes = bytes(attached);
    expect(bare_bytes.size() > 8 && bare_bytes[6] == 3,
           "a database without constraints is still written as 1.3");
    expect(attached_bytes.size() > 8 && attached_bytes[6] == 4,
           "a database with constraints is written as 1.4");

    db::DesignDb reloaded_bare;
    expect(db::DesignDb::load(bare.string(), reloaded_bare).ok(), "the 1.3 file loads");
    expect(!reloaded_bare.has_constraints(), "a 1.3 file loads with no constraints");

    db::DesignDb reloaded;
    expect(db::DesignDb::load(attached.string(), reloaded).ok(), "the 1.4 file loads");
    expect(reloaded.has_constraints(), "a 1.4 file loads with constraints");
    expect(reloaded.constraints() == database.constraints(),
           "constraints survive the round trip exactly");
    expect(db::constraints_hash(reloaded.constraints()) ==
               db::constraints_hash(database.constraints()),
           "the reloaded constraints have the same hash");
}

// ADR-012 section 4: a case value is a hard assignment applied before the fault
// is activated. y = ~a with y stuck-at 1 activates only when a = 1, so a case
// value of 0 makes the fault untestable without touching the unconstrained
// verdict.
void test_podem_applies_case_analysis() {
    db::FlatNetlist netlist;
    db::StringTable strings;
    strings.intern("a");
    strings.intern("y");

    netlist.type = {db::GateType::Not};
    netlist.fanin_begin = {0, 1};
    netlist.fanins = {0};
    netlist.fanout_begin = {0, 1};
    netlist.fanouts = {1};
    netlist.out_net = {1};
    netlist.flags = {0};
    netlist.level = {1};
    netlist.lib_cell = {db::kInvalidId};
    netlist.clock_edge = {db::ClockEdge::Rising};
    netlist.enable = {db::kInvalidId};
    netlist.async_clear = {db::kInvalidId};
    netlist.async_preset = {db::kInvalidId};
    netlist.driver = {db::kPrimaryInput, 0};
    netlist.name_id = {0, 1};
    netlist.source_loc = {db::kInvalidId, db::kInvalidId};

    const std::vector<db::NetId> observables = {1};
    const gmax::sim::Fault fault{1, gmax::sim::StuckValue::One};

    const gmax::atpg::PodemResult free =
        gmax::atpg::run_podem(netlist, fault, observables, gmax::atpg::PodemBudget{});
    expect(free.status == gmax::atpg::PodemStatus::Detected,
           "the unconstrained inverter fault is detected");

    const std::vector<db::CaseAssignment> a_low = {{0, false}};
    const gmax::atpg::PodemResult constrained =
        gmax::atpg::run_podem(netlist, fault, observables, gmax::atpg::PodemBudget{}, a_low);
    expect(constrained.status == gmax::atpg::PodemStatus::Untestable,
           "a case value that blocks activation is untestable, not aborted");

    const gmax::atpg::PodemResult again =
        gmax::atpg::run_podem(netlist, fault, observables, gmax::atpg::PodemBudget{}, a_low);
    expect(again.status == constrained.status && again.decisions == constrained.decisions,
           "the constrained verdict and its counters are repeatable");
}

// ADR-012: constraints carry their own identity in the report, and this slice
// claims no enforced construct until the engines apply the resolved values.
void test_report_claims_no_enforcement() {
    gmax::report::CoverageReport report;
    report.design = "top";
    report.constraints_status = db::ConstraintsStatus::Complete;
    report.constraints_hash = 0x1234abcdu;
    const std::string json = gmax::report::coverage_json(report);
    expect(json.find("\"schema\":\"gmax-coverage-2\"") != std::string::npos,
           "the coverage schema is the constraint-aware one");
    std::string constraints_object = "\"constraints\":{\"status\":\"complete\",\"hash\":\"";
    constraints_object += gmax::report::hex32(0x1234abcdu);
    constraints_object += '"';
    expect(json.find(constraints_object) != std::string::npos,
           "a complete set reports its canonical hash");
    expect(json.find("\"enforced\":[]") != std::string::npos,
           "the enforced set is empty until enforcement lands");
    expect(json.find("\"enforced\":[\"case_analysis\"]") == std::string::npos,
           "the report never claims case analysis");
    expect(json.find("\"capture\":\"not-enforced\"") != std::string::npos,
           "capture constraints remain not enforced");
}

}  // namespace

int main() {
    test_canonical_decimal();
    test_hash_is_order_independent();
    test_incomplete_sets();
    test_status_and_absent();
    test_schema_round_trip();
    test_podem_applies_case_analysis();
    test_report_claims_no_enforcement();
    if (failures != 0) {
        std::cerr << failures << " expectation(s) failed" << std::endl;
        return 1;
    }
    std::cout << "constraints: all tests passed" << std::endl;
    return 0;
}
