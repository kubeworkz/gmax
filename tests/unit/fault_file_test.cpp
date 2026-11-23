// WP-006: the .faults container. The header binds a fault list to the exact
// flattened netlist and to its own count/entry payload; the reader validates
// both before handing the list to ATPG. Framework-free for the same reason as
// the other tests: third_party stays empty until TBD-11 closes.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/atpg/fault_file.h"
#include "core/atpg/fault_list.h"
#include "core/db/db.h"
#include "core/sim/fault.h"

namespace db = gmax::db;
namespace atpg = gmax::atpg;
namespace sim = gmax::sim;

namespace {

int failures = 0;

void expect(const bool ok, const std::string_view what) {
    if (!ok) {
        std::cerr << "FAIL " << what << std::endl;
        ++failures;
    }
}

void expect_error(const db::Status& status, const db::Error error, const std::string_view what) {
    if (status.ok() || status.error() != error) {
        std::cerr << "FAIL " << what << ": expected " << db::error_name(error) << ", got "
                  << db::error_name(status.error());
        if (!status.message().empty()) {
            std::cerr << " (" << status.message() << ")";
        }
        std::cerr << std::endl;
        ++failures;
    }
}

db::Gate gate(db::GateType type, std::string name, std::vector<std::string> fanins,
              std::string out) {
    db::Gate result;
    result.type = type;
    result.name = std::move(name);
    result.fanins = std::move(fanins);
    result.out = std::move(out);
    return result;
}

// Two equivalent buffer/inverter chains: twelve raw faults collapse to four
// classes, which makes the raw and collapsed counts different.
db::Design make_design(const bool reverse) {
    db::Design design;
    design.top = "top";

    db::Module top;
    top.name = "top";
    top.inputs = {"a", "b"};
    top.outputs = {"y", "z"};
    std::vector<db::Gate> gates = {
        gate(db::GateType::Buf, "g1", {"a"}, "n1"),
        gate(db::GateType::Buf, "g2", {"n1"}, "y"),
        gate(db::GateType::Not, "g3", {"b"}, "m1"),
        gate(db::GateType::Buf, "g4", {"m1"}, "z"),
    };
    if (reverse) {
        std::reverse(gates.begin(), gates.end());
    }
    top.gates = std::move(gates);
    design.modules = {top};
    return design;
}

db::DesignDb flatten(db::Design design) {
    db::DesignDb database;
    database.design() = std::move(design);
    const db::Status status = database.flatten();
    expect(status.ok(), "the .faults test design flattens");
    if (!status.ok()) {
        std::cerr << "  " << status.message() << std::endl;
    }
    return database;
}

db::NetId net_named(const db::DesignDb& database, const std::string_view name) {
    const db::FlatNetlist& flat = database.flat();
    for (db::NetId net = 0; net < flat.net_count(); ++net) {
        if (database.strings().at(flat.name_id[net]) == name) {
            return net;
        }
    }
    return db::kInvalidId;
}

std::vector<std::uint8_t> read_bytes(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(in)),
                                     std::istreambuf_iterator<char>());
}

std::string read_text(const std::string& path) {
    const std::vector<std::uint8_t> bytes = read_bytes(path);
    return std::string(bytes.begin(), bytes.end());
}

void write_text(const std::string& path, const std::string& text) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
}

struct Fixture {
    db::DesignDb database;
    std::vector<sim::Fault> raw;
    atpg::CollapsedFaultList collapsed;
};

Fixture make_fixture() {
    Fixture fixture;
    fixture.database = flatten(make_design(false));
    const std::vector<db::NetId> observables = {net_named(fixture.database, "y"),
                                                net_named(fixture.database, "z")};
    fixture.raw = atpg::stuck_at_faults(fixture.database.flat());
    fixture.collapsed = atpg::collapse_stuck_at_faults(fixture.database.flat(), observables);
    return fixture;
}

void test_round_trip_and_counts() {
    Fixture fixture = make_fixture();
    expect(fixture.raw.size() == 12, "the .faults design has twelve raw faults");
    expect(fixture.collapsed.collapsed_count() == 4,
           "the .faults design has four collapsed classes");

    const auto directory = std::filesystem::temp_directory_path();
    const std::string path = (directory / "gmax-fault-file-test.faults").string();
    const std::string copy = (directory / "gmax-fault-file-test-copy.faults").string();

    const db::Status written =
        atpg::write_faults(path, fixture.database, fixture.raw, fixture.collapsed);
    expect(written.ok(), "write_faults writes a .faults file");
    if (!written.ok()) {
        std::cerr << "  " << written.message() << std::endl;
    }

    const std::string text = read_text(path);
    expect(text.rfind("gmax-faults 1\n", 0) == 0, "the .faults file starts with its version");
    expect(text.find("raw-count 12\n") != std::string::npos, "the header reports the raw count");
    expect(text.find("collapsed-count 4\n") != std::string::npos,
           "the header reports the collapsed count");

    atpg::FaultListFile file;
    const db::Status read = atpg::read_faults(path, fixture.database, file);
    expect(read.ok(), "read_faults reads a written .faults file");
    if (!read.ok()) {
        std::cerr << "  " << read.message() << std::endl;
    }
    expect(file.header.raw_count == fixture.raw.size(), "the raw count survives the round trip");
    expect(file.header.collapsed_count == fixture.collapsed.faults.size(),
           "the collapsed count survives the round trip");
    expect(file.header.netlist_hash == atpg::netlist_hash(fixture.database),
           "the header carries the netlist hash");
    expect(file.raw == fixture.raw, "the raw fault list survives the round trip in order");
    expect(file.collapsed == fixture.collapsed.faults,
           "the collapsed fault list survives the round trip in order");
    expect(file.raw_to_collapsed == fixture.collapsed.raw_to_collapsed,
           "the raw-to-collapsed mapping survives the round trip");
    expect(file.header.raw_count == file.raw.size() &&
               file.header.collapsed_count == file.collapsed.size(),
           "the header counts match the decoded lists");

    // The same database and lists must produce exactly the same bytes, or the
    // hashes and ordering are not deterministic.
    expect(atpg::write_faults(copy, fixture.database, fixture.raw, fixture.collapsed).ok(),
           "a second write succeeds");
    expect(read_bytes(path) == read_bytes(copy), "writing the same lists is byte-identical");

    expect(text.find("\nR ") != std::string::npos && text.find("\nC ") != std::string::npos,
           "the file writes both fault sections");

    std::filesystem::remove(path);
    std::filesystem::remove(copy);
}

void test_netlist_identity() {
    const db::DesignDb forward = flatten(make_design(false));
    const db::DesignDb reverse = flatten(make_design(true));
    expect(atpg::netlist_hash(forward) == atpg::netlist_hash(reverse),
           "gate insertion order does not change the netlist hash");

    Fixture fixture = make_fixture();
    const auto directory = std::filesystem::temp_directory_path();
    const std::string path = (directory / "gmax-fault-file-test-netlist.faults").string();
    expect(atpg::write_faults(path, fixture.database, fixture.raw, fixture.collapsed).ok(),
           "the netlist mismatch fixture writes");

    // Add one input and one buffer: the flattened netlist, and therefore its
    // hash, is different even though the fault list bytes on disk are untouched.
    db::Design other_design = make_design(false);
    other_design.modules[0].inputs.push_back("c");
    other_design.modules[0].outputs.push_back("w");
    other_design.modules[0].gates.push_back(gate(db::GateType::Buf, "g5", {"c"}, "w"));
    const db::DesignDb other = flatten(std::move(other_design));
    expect(atpg::netlist_hash(other) != atpg::netlist_hash(fixture.database),
           "a different netlist has a different hash");

    atpg::FaultListFile file;
    const db::Status read = atpg::read_faults(path, other, file);
    expect_error(read, db::Error::BadPayload, "a .faults file is rejected by a different netlist");
    std::filesystem::remove(path);
}

void test_payload_corruption_is_detected() {
    Fixture fixture = make_fixture();
    const auto directory = std::filesystem::temp_directory_path();
    const std::string path = (directory / "gmax-fault-file-test-corrupt.faults").string();
    expect(atpg::write_faults(path, fixture.database, fixture.raw, fixture.collapsed).ok(),
           "the corruption fixture writes");

    std::string text = read_text(path);
    const std::size_t raw_line = text.find("R ");
    const std::size_t digit = raw_line == std::string::npos
                                  ? std::string::npos
                                  : text.find_first_of("0123456789", raw_line + 2);
    expect(digit != std::string::npos, "the corruption fixture has a raw entry");
    if (digit != std::string::npos) {
        // Keep the edited line valid (a one-bit net-id change) so only the
        // payload hash can catch the corruption.
        text[digit] = text[digit] == '0' ? '1' : '0';
    }
    write_text(path, text);

    atpg::FaultListFile file;
    const db::Status read = atpg::read_faults(path, fixture.database, file);
    expect_error(read, db::Error::CrcMismatch,
                 "a changed raw entry is rejected by the fault-list hash");
    std::filesystem::remove(path);
}

void test_empty_lists() {
    const db::DesignDb database = flatten(make_design(false));
    const atpg::CollapsedFaultList empty;
    const auto directory = std::filesystem::temp_directory_path();
    const std::string path = (directory / "gmax-fault-file-test-empty.faults").string();

    expect(atpg::write_faults(path, database, {}, empty).ok(), "an empty fault list writes");
    atpg::FaultListFile file;
    const db::Status read = atpg::read_faults(path, database, file);
    expect(read.ok(), "an empty fault list reads back");
    if (!read.ok()) {
        std::cerr << "  " << read.message() << std::endl;
    }
    expect(file.header.raw_count == 0 && file.header.collapsed_count == 0,
           "an empty .faults file reports zero counts");
    expect(file.raw.empty() && file.collapsed.empty() && file.raw_to_collapsed.empty(),
           "an empty .faults file decodes to empty lists");
    std::filesystem::remove(path);
}

void test_invalid_mapping_is_refused() {
    Fixture fixture = make_fixture();
    atpg::CollapsedFaultList bad = fixture.collapsed;
    bad.raw_to_collapsed.pop_back();
    const auto directory = std::filesystem::temp_directory_path();
    const std::string path = (directory / "gmax-fault-file-test-bad.faults").string();
    const db::Status written = atpg::write_faults(path, fixture.database, fixture.raw, bad);
    expect_error(written, db::Error::BadPayload,
                 "a raw count without a matching mapping is refused");
}

}  // namespace

int main() {
    test_round_trip_and_counts();
    test_netlist_identity();
    test_payload_corruption_is_detected();
    test_empty_lists();
    test_invalid_mapping_is_refused();

    if (failures != 0) {
        std::cerr << failures << " expectation(s) failed" << std::endl;
        return 1;
    }
    std::cout << "fault file checks passed" << std::endl;
    return 0;
}
