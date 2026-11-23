// WP-006/WP-010: the write_faults command must take a persisted flattened
// design and produce a .faults file with both raw and collapsed counts.
// Framework-free for the same reason as the other tests: third_party stays
// empty until TBD-11 closes.

#include <filesystem>
#include <iostream>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/atpg/fault_file.h"
#include "core/atpg/fault_list.h"
#include "core/db/db.h"
#include "core/sim/fault.h"
#include "shell/cli.h"
#include "shell/commands.h"

namespace db = gmax::db;
namespace atpg = gmax::atpg;
namespace shell = gmax::shell;
namespace sim = gmax::sim;

namespace {

int failures = 0;

void expect(const bool ok, const std::string_view what) {
    if (!ok) {
        std::cerr << "FAIL " << what << std::endl;
        ++failures;
    }
}

bool contains(const std::string_view text, const std::string_view needle) {
    return text.find(needle) != std::string_view::npos;
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

// a -> n1 -> y: three sites, six raw faults, two buffer-chain classes.
db::DesignDb make_database() {
    db::Design design;
    design.top = "top";

    db::Module top;
    top.name = "top";
    top.inputs = {"a"};
    top.outputs = {"y"};
    top.gates = {
        gate(db::GateType::Buf, "g1", {"a"}, "n1"),
        gate(db::GateType::Buf, "g2", {"n1"}, "y"),
    };
    design.modules = {top};

    db::DesignDb database;
    database.design() = std::move(design);
    const db::Status flattened = database.flatten();
    expect(flattened.ok(), "the command test design flattens");
    if (!flattened.ok()) {
        std::cerr << "  " << flattened.message() << std::endl;
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

void test_write_faults_end_to_end() {
    const db::DesignDb database = make_database();
    const auto directory = std::filesystem::temp_directory_path();
    const std::string database_path = (directory / "gmax-fault-cli-test.dfdb").string();
    const std::string faults_path = (directory / "gmax-fault-cli-test.faults").string();

    expect(database.save(database_path).ok(), "the command test design saves as .dfdb");
    if (!std::filesystem::exists(database_path)) {
        std::cerr << "FAIL the .dfdb fixture was not created" << std::endl;
        ++failures;
        return;
    }

    shell::Cli cli("0.0.0");
    std::string register_error;
    expect(shell::register_commands(cli, register_error), "write_faults registers");
    expect(register_error.empty(), "command registration leaves no error");
    expect(contains(cli.global_help(), "write_faults <design> <faults>"),
           "global help renders the command schema");

    std::ostringstream out;
    std::ostringstream err;
    const std::vector<std::string> args = {"write_faults", database_path, faults_path};
    const shell::ExitCode code = cli.run(std::span<const std::string>(args), out, err);
    expect(code == shell::ExitCode::Success, "write_faults succeeds");
    if (code != shell::ExitCode::Success) {
        std::cerr << "  " << err.str();
    }
    expect(err.str().empty(), "write_faults writes no errors on success");
    expect(contains(out.str(), "raw faults: 6"), "write_faults reports the raw count");
    expect(contains(out.str(), "collapsed faults: 2"), "write_faults reports the collapsed count");
    expect(contains(out.str(), "wrote '" + faults_path + "'"),
           "write_faults reports the output path");

    db::DesignDb reloaded;
    const db::Status loaded = db::DesignDb::load(database_path, reloaded);
    expect(loaded.ok(), "the test reloads the command's .dfdb input");
    if (!loaded.ok()) {
        std::cerr << "  " << loaded.message() << std::endl;
        return;
    }

    atpg::FaultListFile file;
    const db::Status read = atpg::read_faults(faults_path, reloaded, file);
    expect(read.ok(), "the written .faults file reads back against the .dfdb");
    if (!read.ok()) {
        std::cerr << "  " << read.message() << std::endl;
        return;
    }
    expect(file.header.raw_count == 6, "the .faults header carries six raw faults");
    expect(file.header.collapsed_count == 2, "the .faults header carries two classes");

    const db::NetId y = net_named(reloaded, "y");
    expect(y != db::kInvalidId, "the command test output net exists");
    if (y == db::kInvalidId) {
        return;
    }
    const std::vector<sim::Fault> expected_raw = atpg::stuck_at_faults(reloaded.flat());
    const std::vector<db::NetId> observables = {y};
    const atpg::CollapsedFaultList expected =
        atpg::collapse_stuck_at_faults(reloaded.flat(), observables);
    expect(file.raw == expected_raw, "the command writes the ordered raw list");
    expect(file.collapsed == expected.faults, "the command writes the collapsed list");
    expect(file.raw_to_collapsed == expected.raw_to_collapsed,
           "the command writes the raw-to-collapsed mapping");

    std::filesystem::remove(database_path);
    std::filesystem::remove(faults_path);
}

}  // namespace

int main() {
    test_write_faults_end_to_end();

    if (failures != 0) {
        std::cerr << failures << " expectation(s) failed" << std::endl;
        return 1;
    }
    std::cout << "fault CLI checks passed" << std::endl;
    return 0;
}
