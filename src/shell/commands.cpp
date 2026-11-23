#include "shell/commands.h"

#include <ostream>
#include <string>
#include <utility>
#include <vector>

#include "core/atpg/fault_file.h"
#include "core/atpg/fault_list.h"
#include "core/db/db.h"
#include "core/db/design.h"
#include "core/db/status.h"
#include "core/sim/fault.h"
#include "shell/cli.h"

namespace gmax::shell {
namespace {

// The flattened graph does not carry a primary-output flag, so resolve the
// top module's output names through the string table. These are the observation
// points the structural collapse must not merge away.
std::vector<db::NetId> primary_outputs(const db::DesignDb& database, std::string& error) {
    const db::Module* top = db::find_module(database.design(), database.design().top);
    if (top == nullptr) {
        error = "top module '" + database.design().top + "' is not defined";
        return {};
    }

    const db::FlatNetlist& flat = database.flat();
    std::vector<db::NetId> outputs;
    outputs.reserve(top->outputs.size());
    for (const std::string& name : top->outputs) {
        db::NetId found = db::kInvalidId;
        for (db::NetId net = 0; net < flat.net_count(); ++net) {
            if (database.strings().at(flat.name_id[net]) == name) {
                found = net;
                break;
            }
        }
        if (found == db::kInvalidId) {
            error = "top output '" + name + "' has no flattened net";
            return {};
        }
        outputs.push_back(found);
    }
    return outputs;
}

ExitCode write_faults_handler(const std::vector<std::string>& positionals, std::ostream& out,
                              std::ostream& err) {
    const std::string& design_path = positionals[0];
    const std::string& output_path = positionals[1];

    db::DesignDb database;
    const db::Status loaded = db::DesignDb::load(design_path, database);
    if (!loaded.ok()) {
        err << "gmax: cannot load design '" << design_path << "': " << loaded.message() << "\n";
        return ExitCode::Design;
    }

    std::string primary_output_error;
    const std::vector<db::NetId> observables = primary_outputs(database, primary_output_error);
    if (!primary_output_error.empty()) {
        err << "gmax: " << primary_output_error << "\n";
        return ExitCode::Design;
    }

    const db::FlatNetlist& flat = database.flat();
    const std::vector<sim::Fault> raw = atpg::stuck_at_faults(flat);
    const atpg::CollapsedFaultList collapsed = atpg::collapse_stuck_at_faults(flat, observables);
    const db::Status written = atpg::write_faults(output_path, database, raw, collapsed);
    if (!written.ok()) {
        err << "gmax: cannot write faults '" << output_path << "': " << written.message() << "\n";
        return ExitCode::Design;
    }

    out << "wrote '" << output_path << "'\n";
    out << "raw faults: " << raw.size() << "\n";
    out << "collapsed faults: " << collapsed.collapsed_count() << "\n";
    return ExitCode::Success;
}

}  // namespace

bool register_commands(Cli& cli, std::string& error) {
    CommandSpec spec;
    spec.name = "write_faults";
    spec.summary = "write a .faults fault list from a flattened .dfdb";
    spec.positionals = {
        {"design", "flattened .dfdb design database", true},
        {"faults", "output .faults file", true},
    };
    return cli.add_command(std::move(spec), write_faults_handler, error);
}

}  // namespace gmax::shell
