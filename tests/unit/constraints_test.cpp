// ADR-012: the persisted constraint model, its canonical payload and hash, and
// the .dfdb 1.4 section. Framework-free like the other tests.

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/atpg/podem.h"
#include "core/atpg/sat.h"
#include "core/db/constraints.h"
#include "core/db/db.h"
#include "core/db/dfdb.h"
#include "core/db/flat.h"
#include "core/db/string_table.h"
#include "core/frontend/json.h"
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
    {
        // ADR-013 section 5: which endpoints the SDC wrote as [get_clocks] is stored
        // with the path, so a reloaded database still tells a clock endpoint from an
        // object endpoint of the same name. It stays out of the canonical payload.
        db::ConstraintFalsePath provenance;
        provenance.from = {"clk_a"};
        provenance.to = {"clk_b"};
        provenance.from_clocks = {"clk_a"};
        provenance.to_clocks = {"clk_b"};
        database.constraints().false_paths.push_back(provenance);
    }
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
    expect(attached_bytes.size() > 8 && attached_bytes[6] == 6,
           "a database with constraints is written as 1.6");

    db::DesignDb reloaded_bare;
    expect(db::DesignDb::load(bare.string(), reloaded_bare).ok(), "the 1.3 file loads");
    expect(!reloaded_bare.has_constraints(), "a 1.3 file loads with no constraints");

    db::DesignDb reloaded;
    expect(db::DesignDb::load(attached.string(), reloaded).ok(), "the 1.6 file loads");
    expect(reloaded.has_constraints(), "a 1.4 file loads with constraints");
    expect(reloaded.constraints() == database.constraints(),
           "constraints survive the round trip exactly");
    expect(!reloaded.constraints().false_paths.empty() &&
               reloaded.constraints().false_paths.back().from_clocks ==
                   std::vector<std::string>{"clk_a"},
           "false-path clock provenance survives the database");
    expect(db::constraints_hash(reloaded.constraints()) ==
               db::constraints_hash(database.constraints()),
           "the reloaded constraints have the same hash");
}

// ADR-012 section 4: clocks are resolved and reported, not enforced. A clock is
// enforceable only on a top-level input port, and the domain count follows the
// flops a declared clock actually clocks rather than the declarations alone.
void test_clock_sources_and_domains() {
    db::Gate ff_a;
    ff_a.type = db::GateType::Dff;
    ff_a.name = "ff_a";
    ff_a.fanins = {"d_a", "clk_a"};
    ff_a.out = "ra";
    db::Gate ff_b;
    ff_b.type = db::GateType::Dff;
    ff_b.name = "ff_b";
    ff_b.fanins = {"d_b", "clk_b"};
    ff_b.out = "rb";
    db::Gate g_a;
    g_a.type = db::GateType::Buf;
    g_a.name = "g_a";
    g_a.fanins = {"ra"};
    g_a.out = "q_a";
    db::Gate g_b;
    g_b.type = db::GateType::Buf;
    g_b.name = "g_b";
    g_b.fanins = {"rb"};
    g_b.out = "q_b";

    db::Module top;
    top.name = "top";
    top.inputs = {"clk_a", "clk_b", "d_a", "d_b"};
    top.outputs = {"q_a", "q_b"};
    top.gates = {ff_a, ff_b, g_a, g_b};

    db::Design design;
    design.top = "top";
    design.modules = {top};

    db::DesignDb database;
    database.design() = std::move(design);
    expect(database.flatten().ok(), "the two-clock design flattens");

    const db::FlatNetlist& flat = database.flat();
    const db::StringTable& strings = database.strings();
    expect(db::enforceable_clock_source("clk_a", flat, strings),
           "a clock on a top-level input is enforceable");
    expect(!db::enforceable_clock_source("ra", flat, strings),
           "a clock on a flop output is not enforceable");
    expect(!db::enforceable_clock_source("absent", flat, strings),
           "a clock naming no net is not enforceable");

    db::ConstraintClock clk_a;
    clk_a.name = "clk_a";
    clk_a.source = "clk_a";
    db::Constraints one;
    one.clocks = {clk_a};
    const db::ClockDomains first = db::clock_domains(one, flat, strings);
    expect(first.domains == 1 && first.flops == 1, "one declared clock clocks one flop");

    db::ConstraintClock clk_b;
    clk_b.name = "clk_b";
    clk_b.source = "clk_b";
    db::Constraints both = one;
    both.clocks.push_back(clk_b);
    const db::ClockDomains two = db::clock_domains(both, flat, strings);
    expect(two.domains == 2 && two.flops == 2, "two declared clocks clock two flops");

    // A clock on the flop output binds a net no state-holding gate samples, so
    // it adds no domain even though it remains part of the set.
    db::ConstraintClock clk_ra;
    clk_ra.name = "clk_ra";
    clk_ra.source = "ra";
    db::Constraints misplaced = one;
    misplaced.clocks.push_back(clk_ra);
    const db::ClockDomains reported = db::clock_domains(misplaced, flat, strings);
    expect(reported.domains == 1 && reported.flops == 1, "a clock on a non-input binds no domain");

    // Two names on one net are one domain, counted once, while each name still
    // reports the gates that net clocks.
    db::ConstraintClock alias;
    alias.name = "clk_alias";
    alias.source = "clk_a";
    db::Constraints aliased = one;
    aliased.clocks.push_back(alias);
    const db::ClockDomains deduped = db::clock_domains(aliased, flat, strings);
    expect(deduped.domains == 1 && deduped.flops == 1, "two names on one net are one domain");

    const std::vector<db::ClockGateCount> aliased_gates =
        db::clock_gate_counts(aliased, flat, strings);
    expect(aliased_gates.size() == 2, "both names of one net are reported");
    expect(aliased_gates.size() == 2 && aliased_gates[0].clock == "clk_a" &&
               aliased_gates[0].gates == 1,
           "the first name reports its net's gates");
    expect(aliased_gates.size() == 2 && aliased_gates[1].clock == "clk_alias" &&
               aliased_gates[1].gates == 1,
           "the second name reports the same net's gates");

    // A clock whose source names no net still appears with zero gates, so a
    // refusal can name it; a clock with no source is not part of the view.
    db::ConstraintClock absent;
    absent.name = "clk_absent";
    absent.source = "absent";
    db::Constraints unresolvable = one;
    unresolvable.clocks.push_back(absent);
    const std::vector<db::ClockGateCount> unresolved =
        db::clock_gate_counts(unresolvable, flat, strings);
    expect(unresolved.size() == 2 && unresolved[0].clock == "clk_a" && unresolved[0].gates == 1,
           "a resolvable clock keeps its gate count beside an unresolvable one");
    expect(unresolved.size() == 2 && unresolved[1].clock == "clk_absent" &&
               unresolved[1].gates == 0,
           "a clock whose source names no net reports zero gates");
    const db::ClockDomains still = db::clock_domains(unresolvable, flat, strings);
    expect(still.domains == 1 && still.flops == 1, "an unresolvable clock adds no domain");

    db::ConstraintClock sourceless;
    sourceless.name = "clk_bare";
    db::Constraints bare = one;
    bare.clocks.push_back(sourceless);
    const std::vector<db::ClockGateCount> bare_gates = db::clock_gate_counts(bare, flat, strings);
    expect(bare_gates.size() == 1 && bare_gates[0].clock == "clk_a",
           "a clock with no source is not part of the per-clock view");
}

// ADR-013 section 2: only an edge-triggered element can be captured, so the
// per-clock view separates every state-holding element that samples the net from
// the ones a capture cycle can pulse, and counts how many of those the netlist
// marks as scan cells. A lockup latch is state but is captured by no cycle, so it
// raises `gates` alone; that is what keeps a design with a latch on a crossing
// from reading as a partially scanned domain.
void test_scan_cell_counts() {
    const auto flop = [](const db::GateType type, const std::string& name,
                         const std::string& clock) {
        db::Gate gate;
        gate.type = type;
        gate.name = name;
        gate.fanins = {"d", clock};
        gate.out = "n_" + name;
        return gate;
    };
    const auto buffer = [](const std::string& name) {
        db::Gate gate;
        gate.type = db::GateType::Buf;
        gate.name = "g_" + name;
        gate.fanins = {"n_" + name};
        gate.out = "q_" + name;
        return gate;
    };

    db::Module top;
    top.name = "top";
    top.inputs = {"clk_a", "clk_b", "clk_c", "d"};
    const auto add = [&top, &flop, &buffer](const db::GateType type, const std::string& name,
                                            const std::string& clock) {
        top.gates.push_back(flop(type, name, clock));
        top.gates.push_back(buffer(name));
        top.outputs.push_back("q_" + name);
    };
    // clk_a: three scan cells, five flops and one lockup latch.
    add(db::GateType::ScanCell, "sc_a0", "clk_a");
    add(db::GateType::ScanCell, "sc_a1", "clk_a");
    add(db::GateType::ScanCell, "sc_a2", "clk_a");
    add(db::GateType::Dff, "ff_a0", "clk_a");
    add(db::GateType::Dff, "ff_a1", "clk_a");
    add(db::GateType::Dff, "ff_a2", "clk_a");
    add(db::GateType::Dff, "ff_a3", "clk_a");
    add(db::GateType::Dff, "ff_a4", "clk_a");
    add(db::GateType::Dlat, "dl_a", "clk_a");
    // clk_b: unscanned; clk_c: fully scanned.
    add(db::GateType::Dff, "ff_b", "clk_b");
    add(db::GateType::ScanCell, "sc_c0", "clk_c");
    add(db::GateType::ScanCell, "sc_c1", "clk_c");

    db::Design design;
    design.top = "top";
    design.modules = {top};

    db::DesignDb database;
    database.design() = std::move(design);
    expect(database.flatten().ok(), "the partially scanned design flattens");

    db::ConstraintClock clk_a;
    clk_a.name = "clk_a";
    clk_a.source = "clk_a";
    db::ConstraintClock clk_b;
    clk_b.name = "clk_b";
    clk_b.source = "clk_b";
    db::ConstraintClock clk_c;
    clk_c.name = "clk_c";
    clk_c.source = "clk_c";
    db::Constraints constraints;
    constraints.clocks = {clk_a, clk_b, clk_c};

    const std::vector<db::ClockGateCount> counts =
        db::clock_gate_counts(constraints, database.flat(), database.strings());
    expect(counts.size() == 3, "every declared clock is reported");
    expect(counts.size() == 3 && counts[0].clock == "clk_a" && counts[0].gates == 9 &&
               counts[0].captured == 8 && counts[0].scan_cells == 3,
           "a partially scanned domain reports all three counts");
    expect(counts.size() == 3 && counts[1].clock == "clk_b" && counts[1].gates == 1 &&
               counts[1].captured == 1 && counts[1].scan_cells == 0,
           "an unscanned domain captures one element and scans none");
    expect(counts.size() == 3 && counts[2].clock == "clk_c" && counts[2].gates == 2 &&
               counts[2].captured == 2 && counts[2].scan_cells == 2,
           "a fully scanned domain captures only scan cells");

    // The domain view keeps its old meaning: every state-holding element, latches
    // included, so a counted latch still reaches the reported flop total.
    const db::ClockDomains domains =
        db::clock_domains(constraints, database.flat(), database.strings());
    expect(domains.domains == 3 && domains.flops == 12,
           "the domain view still counts every state-holding gate");
}

// ADR-012 section 4: a case value is a hard assignment applied before the fault
// is activated. y = ~a with y stuck-at 1 activates only when a = 1, so a case
// value of 0 makes the fault untestable without touching the unconstrained
// verdict.
// The flattened inverter both engines grade: a -> NOT -> y, so net 0 is the
// input and net 1 the output.
void flatten_inverter(db::DesignDb& database) {
    database.design() = make_inverter().design;
    expect(database.flatten().ok(), "the inverter flattens for ATPG");
    expect(database.flat().net_count() == 2, "the flattened inverter has two nets");
}

void test_podem_applies_case_analysis() {
    db::DesignDb database;
    flatten_inverter(database);
    const db::FlatNetlist& netlist = database.flat();

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

// The miter asserts the same values as unit clauses on both copies, so a case
// value that blocks activation makes the query UNSAT instead of undecided.
void test_sat_applies_case_analysis() {
    db::DesignDb database;
    flatten_inverter(database);
    const db::FlatNetlist& netlist = database.flat();

    const std::vector<db::NetId> observables = {1};
    const gmax::sim::Fault fault{1, gmax::sim::StuckValue::One};

    const gmax::atpg::SatFaultResult free = gmax::atpg::check_fault(netlist, fault, observables);
    expect(free.status == gmax::atpg::SatFaultStatus::Detected,
           "the unconstrained miter detects the inverter fault");

    const std::vector<db::CaseAssignment> a_low = {{0, false}};
    const gmax::atpg::SatFaultResult constrained =
        gmax::atpg::check_fault(netlist, fault, observables, 1000000, 0, nullptr, a_low);
    expect(constrained.status == gmax::atpg::SatFaultStatus::Untestable,
           "the case value makes the miter UNSAT, which is untestable not unknown");
}

// ADR-004's schema policy at the 1.5/1.6 boundary: the false path's line landed after
// 1.5 had been written by a release, so it is its own minor rather than a longer 1.5
// record. A 1.5 file therefore still loads, keeping its clock provenance and reading
// no line, while the current layout keeps both.
void test_false_path_layouts() {
    Fixture fixture = make_inverter();
    db::DesignDb database;
    database.design() = fixture.design;
    expect(database.flatten().ok(), "the inverter flattens");

    database.constraints() = make_constraints();
    db::ConstraintFalsePath path;
    path.from = {"clk_a"};
    path.to = {"clk_b"};
    path.from_clocks = {"clk_a"};
    path.to_clocks = {"clk_b"};
    path.line = 9;
    database.constraints().false_paths = {path};

    const std::filesystem::path older =
        std::filesystem::temp_directory_path() / "gmax-constraints-1.5.dfdb";
    const std::filesystem::path current =
        std::filesystem::temp_directory_path() / "gmax-constraints-1.6.dfdb";
    const auto write = [&database](const std::uint16_t minor, const std::filesystem::path& target) {
        const std::vector<std::uint8_t> bytes = db::dfdb::serialize(
            database.design(), database.flat(), database.strings(), &database.constraints(), minor);
        std::ofstream out(target, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(bytes.data()),
                  static_cast<std::streamsize>(bytes.size()));
        return bytes;
    };

    const std::vector<std::uint8_t> older_bytes = write(5, older);
    const std::vector<std::uint8_t> current_bytes = write(db::dfdb::kSchemaMinor, current);
    expect(older_bytes.size() > 8 && older_bytes[6] == 5, "the older layout writes 1.5");
    expect(current_bytes.size() > 8 && current_bytes[6] == 6, "the current layout writes 1.6");
    expect(current_bytes.size() == older_bytes.size() + 4,
           "1.6 is exactly the line field longer than 1.5");

    db::DesignDb loaded_older;
    expect(db::DesignDb::load(older.string(), loaded_older).ok(),
           "a 1.5 database loads under the 1.6 reader");
    expect(loaded_older.constraints().false_paths.size() == 1, "the 1.5 file keeps its false path");
    if (loaded_older.constraints().false_paths.size() == 1) {
        const db::ConstraintFalsePath& reloaded = loaded_older.constraints().false_paths[0];
        expect(reloaded.from == path.from && reloaded.to == path.to,
               "the 1.5 layout's endpoints survive");
        expect(reloaded.from_clocks == std::vector<std::string>{"clk_a"} &&
                   reloaded.to_clocks == std::vector<std::string>{"clk_b"},
               "the 1.5 layout's clock provenance survives");
        expect(reloaded.line == 0, "a 1.5 path carries no line");
    }

    db::DesignDb loaded_current;
    expect(db::DesignDb::load(current.string(), loaded_current).ok(), "the 1.6 database loads");
    expect(loaded_current.constraints() == database.constraints(),
           "the 1.6 round trip keeps the line and the provenance exactly");
}

// ADR-013 section 6: the clock provenance of a false-path endpoint is part of the
// model but not of the canonical payload, so recording it never moves the hash.
void test_false_path_provenance() {
    db::Constraints plain;
    plain.present = true;
    plain.complete = true;
    db::ConstraintFalsePath path;
    path.from = {"clk_a"};
    path.to = {"clk_b"};
    plain.false_paths = {path};

    db::Constraints tagged = plain;
    tagged.false_paths[0].from_clocks = {"clk_a"};
    tagged.false_paths[0].to_clocks = {"clk_b"};
    expect(tagged != plain, "the provenance is part of the model");
    expect(db::constraints_hash(tagged) == db::constraints_hash(plain),
           "clock provenance does not move the constraint hash");
}

// ADR-012: constraints carry their own identity in the report, and this slice
// claims no enforced construct until the engines apply the resolved values.
void test_report_claims_no_enforcement() {
    gmax::report::CoverageReport report;
    report.design = "top";
    report.constraints_status = db::ConstraintsStatus::Complete;
    report.constraints_hash = 0x1234abcdu;
    const std::string json = gmax::report::coverage_json(report);
    expect(json.find("\"schema\":\"gmax-coverage-4\"") != std::string::npos,
           "the coverage schema is the warning-aware one");
    std::string constraints_object = "\"constraints\":{\"status\":\"complete\",\"hash\":\"";
    constraints_object += gmax::report::hex32(0x1234abcdu);
    constraints_object += '"';
    expect(json.find(constraints_object) != std::string::npos,
           "a complete set reports its canonical hash");
    expect(json.find("\"enforced\":[]") != std::string::npos,
           "the enforced set is empty until enforcement lands");
    expect(json.find("\"enforced\":[\"case_analysis\"]") == std::string::npos,
           "the report never claims case analysis");
    expect(json.find("\"capture\":{\"status\":\"absent\",\"procedure\":null,"
                     "\"derived\":false,\"hash\":null}") != std::string::npos,
           "a report with no procedure claims no capture");
}

// ADR-013 section 6: the enforced list names what ran, in a fixed order, and an
// enforced capture names the clock-group model it applied.
void test_enforced_constructs() {
    gmax::report::CaptureReport absent;
    expect(gmax::report::enforced_constructs(false, absent, false).empty(),
           "a run that applied nothing claims nothing");

    gmax::report::CaptureReport capture;
    capture.absent = false;
    capture.procedure = "sequential";
    const std::vector<std::string> both = gmax::report::enforced_constructs(true, capture, false);
    expect(both == (std::vector<std::string>{"case_analysis", "clock_groups"}),
           "case analysis comes first and the applied capture follows");
    expect(gmax::report::enforced_constructs(false, capture, false) ==
               (std::vector<std::string>{"clock_groups"}),
           "a capture-only run names the clock-group model");
    expect(gmax::report::enforced_constructs(true, absent, false) ==
               (std::vector<std::string>{"case_analysis"}),
           "a case-only run names only case analysis");
    expect(gmax::report::enforced_constructs(true, capture, true) ==
               (std::vector<std::string>{"case_analysis", "clock_groups", "false_paths"}),
           "an examined false-path set follows the capture model");
    expect(gmax::report::enforced_constructs(true, absent, true) ==
               (std::vector<std::string>{"case_analysis"}),
           "a run with no procedure examined no false path");
}

// ADR-013 section 5's DFT019: a false path between two clocks the run captures apart
// is a warning, so the run still grades; both reports carry the finding, its line and
// the rule, and the enforced set names the set the run examined.
void test_false_path_warning_report() {
    gmax::report::CoverageReport coverage;
    coverage.design = "top";
    coverage.constraints_status = db::ConstraintsStatus::Complete;
    coverage.constraints_hash = 0x1234abcdu;
    coverage.capture.absent = false;
    coverage.capture.procedure = "sequential";
    coverage.capture.hash = 0x00abcdefu;
    coverage.enforced = gmax::report::enforced_constructs(false, coverage.capture, true);
    gmax::report::FalsePathWarningReport warning;
    warning.from = "clk_a";
    warning.to = "clk_b";
    warning.line = 3;
    coverage.false_paths = {warning};

    const std::string json = gmax::report::coverage_json(coverage);
    expect(json.find("\"enforced\":[\"clock_groups\",\"false_paths\"]") != std::string::npos,
           "the enforced set names the false paths the run examined");

    gmax::frontend::JsonValue parsed;
    std::string parse_error;
    expect(gmax::frontend::parse_json(json, parsed, parse_error),
           "a report with a warning is well-formed JSON");
    const gmax::frontend::JsonValue* constraints = parsed.find("constraints");
    const gmax::frontend::JsonValue* paths =
        constraints == nullptr ? nullptr : constraints->find("false_paths");
    expect(paths != nullptr && paths->is_array() && paths->array.size() == 1,
           "the coverage report lists the DFT019 finding");
    if (paths != nullptr && paths->array.size() == 1) {
        const gmax::frontend::JsonValue& finding = *paths->array[0];
        const gmax::frontend::JsonValue* rule = finding.find("rule");
        const gmax::frontend::JsonValue* from = finding.find("from");
        const gmax::frontend::JsonValue* to = finding.find("to");
        const gmax::frontend::JsonValue* line = finding.find("line");
        expect(rule != nullptr && rule->text == "DFT019", "the finding names its rule");
        expect(from != nullptr && from->text == "clk_a" && to != nullptr && to->text == "clk_b",
               "the finding names both clocks");
        expect(line != nullptr && line->is_number() && line->number == 3.0,
               "the finding names the path's line");
    }

    gmax::report::RunManifest manifest;
    manifest.tool_version = "0.1.0";
    manifest.capture = coverage.capture;
    manifest.false_paths = coverage.false_paths;
    const std::string manifest_text = gmax::report::manifest_json(manifest);
    expect(manifest_text.find("\"schema\":\"gmax-run-manifest-4\"") != std::string::npos,
           "the manifest schema carries the warning list");
    expect(manifest_text.find("\"false_paths\":[{\"rule\":\"DFT019\",\"from\":\"clk_a\","
                              "\"to\":\"clk_b\",\"line\":3}]") != std::string::npos,
           "the manifest repeats the finding");
}

// ADR-013 section 6: the capture object names the procedure the run applied, its
// provenance and its payload hash, and only the manifest carries the body.
void test_report_capture_object() {
    gmax::report::CoverageReport coverage;
    coverage.design = "top";
    coverage.constraints_status = db::ConstraintsStatus::Complete;
    coverage.constraints_hash = 0x1234abcdu;
    coverage.capture.absent = false;
    coverage.capture.procedure = "grouped";
    coverage.capture.derived = true;
    coverage.capture.hash = 0x00abcdefu;
    gmax::report::CaptureGroupReport group;
    group.clocks = {"clk_a", "clk_b"};
    group.edge = 12.5;
    gmax::report::CaptureCycleReport cycle;
    cycle.groups = {group};
    coverage.capture.cycles = {cycle};

    const std::string json = gmax::report::coverage_json(coverage);
    expect(json.find("\"capture\":{\"status\":\"enforced\",\"procedure\":\"grouped\","
                     "\"derived\":true,\"hash\":\"00abcdef\"}") != std::string::npos,
           "a graded run reports the procedure it applied");
    expect(json.find("cycles") == std::string::npos,
           "the coverage report carries no procedure body");

    // The reports are hand-built, so they are parsed rather than matched: a broken
    // brace or a comma in the wrong place would pass every substring check.
    gmax::frontend::JsonValue coverage_parse;
    std::string parse_error;
    expect(gmax::frontend::parse_json(json, coverage_parse, parse_error),
           "the coverage report is well-formed JSON");
    const gmax::frontend::JsonValue* coverage_capture = coverage_parse.find("constraints");
    if (coverage_capture != nullptr) {
        coverage_capture = coverage_capture->find("capture");
    }
    expect(coverage_capture != nullptr, "the coverage report carries the capture object");
    expect(coverage_capture != nullptr && coverage_capture->find("status") != nullptr,
           "the capture object carries its status");
    expect(coverage_capture != nullptr && coverage_capture->find("cycles") == nullptr,
           "the coverage capture object carries no procedure body");

    gmax::report::RunManifest manifest;
    manifest.tool_version = "0.1.0";
    manifest.capture = coverage.capture;
    const std::string manifest_text = gmax::report::manifest_json(manifest);
    expect(manifest_text.find("\"schema\":\"gmax-run-manifest-4\"") != std::string::npos,
           "the manifest schema carries the capture object");
    gmax::frontend::JsonValue manifest_parse;
    expect(gmax::frontend::parse_json(manifest_text, manifest_parse, parse_error),
           "the manifest is well-formed JSON");
    const gmax::frontend::JsonValue* inputs = manifest_parse.find("inputs");
    const gmax::frontend::JsonValue* manifest_capture =
        inputs == nullptr ? nullptr : inputs->find("constraints");
    if (manifest_capture != nullptr) {
        manifest_capture = manifest_capture->find("capture");
    }
    expect(manifest_capture != nullptr,
           "the manifest carries the capture object inside inputs.constraints");
    const gmax::frontend::JsonValue* cycles =
        manifest_capture == nullptr ? nullptr : manifest_capture->find("cycles");
    expect(cycles != nullptr && cycles->is_array() && cycles->array.size() == 1,
           "the manifest lists one cycle of the applied procedure");

    expect(manifest_text.find("\"capture\":{\"status\":\"enforced\","
                              "\"procedure\":\"grouped\",\"derived\":true,"
                              "\"hash\":\"00abcdef\",\"cycles\":[{\"groups\":"
                              "[{\"clocks\":[\"clk_a\",\"clk_b\"],\"edge\":12.5}]}]}") !=
               std::string::npos,
           "the manifest carries the canonical procedure with its clocks and edge");
}

}  // namespace

int main() {
    test_canonical_decimal();
    test_hash_is_order_independent();
    test_incomplete_sets();
    test_status_and_absent();
    test_clock_sources_and_domains();
    test_scan_cell_counts();
    test_schema_round_trip();
    test_podem_applies_case_analysis();
    test_sat_applies_case_analysis();
    test_report_claims_no_enforcement();
    test_report_capture_object();
    test_false_path_provenance();
    test_false_path_layouts();
    test_enforced_constructs();
    test_false_path_warning_report();
    if (failures != 0) {
        std::cerr << failures << " expectation(s) failed" << std::endl;
        return 1;
    }
    std::cout << "constraints: all tests passed" << std::endl;
    return 0;
}
