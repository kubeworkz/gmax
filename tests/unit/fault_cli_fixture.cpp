// Process-level CLI fixture: save a known flattened design as .dfdb. Keeping
// the fixture in C++ avoids checking in a binary database and gives the
// write_faults process test a stable six-raw/two-collapsed design.

#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "core/db/db.h"

namespace db = gmax::db;

namespace {

db::Gate gate(db::GateType type, std::string name, std::vector<std::string> fanins,
              std::string out) {
    db::Gate result;
    result.type = type;
    result.name = std::move(name);
    result.fanins = std::move(fanins);
    result.out = std::move(out);
    return result;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: fault_cli_fixture <output.dfdb>" << std::endl;
        return 1;
    }

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
    if (!flattened.ok()) {
        std::cerr << "fixture flatten failed: " << flattened.message() << std::endl;
        return 1;
    }
    const db::Status saved = database.save(argv[1]);
    if (!saved.ok()) {
        std::cerr << "fixture save failed: " << saved.message() << std::endl;
        return 1;
    }
    return 0;
}
