// WP-003: Liberty subset import and primitive decomposition. Framework-free for
// the same reason as the other tests: third_party stays empty until TBD-11.

#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "core/db/db.h"
#include "core/frontend/decompose.h"
#include "core/frontend/expr.h"
#include "core/frontend/liberty.h"

namespace db = gmax::db;
namespace frontend = gmax::frontend;

namespace {

int failures = 0;

void expect(bool ok, std::string_view what) {
    if (!ok) {
        std::cerr << "FAIL " << what << std::endl;
        ++failures;
    }
}

// The demo library covers every construct WP-003 reads: attributes, unknown
// groups (delay_model, timing, pg_pin is not used here), comments, quoted
// functions, ff/latch, a statetable and test_cell.
const std::string_view kLibrary = R"LIB(/* demo library */
library (demo) {
  delay_model : table_lookup;
  cell (AND2) {
    area : 1.25;
    pin (A) {
      direction : input;
      timing () { related_pin : "B"; }
    }
    pin (B) { direction : input; }
    pin (Y) { direction : output; function : "A * B"; }
  }
  cell (NAND2) {
    pin (A) { direction : input; }
    pin (B) { direction : input; }
    pin (Y) { direction : output; function : "!(A & B)"; }
  }
  cell (MUX2) {
    pin (A) { direction : input; }
    pin (B) { direction : input; }
    pin (S) { direction : input; }
    pin (Y) { direction : output; function : "S ? A : B"; }
  }
  cell (INV) {
    pin (A) { direction : input; }
    pin (Y) { direction : output; function : "!A"; }
  }
  cell (TIE0) {
    pin (Y) { direction : output; function : "0"; }
  }
  cell (DFF) {
    area : 2.0;
    pin (D) { direction : input; }
    pin (CK) { direction : input; clock : true; }
    pin (Q) {
      direction : output;
      ff (IQ, IQN) { next_state : "D"; clocked_on : "CK"; }
    }
  }
  cell (DFFR) {
    pin (D) { direction : input; }
    pin (CK) { direction : input; }
    pin (CDN) { direction : input; }
    pin (Q) {
      direction : output;
      ff (IQ) { next_state : "D"; clocked_on : "CK"; clear : "CDN"; }
    }
  }
  cell (LAT) {
    statetable ("D", "IQ") {
      table : "0 : 0 : 0, 1 : 0 : 1, 0 : 1 : 0, 1 : 1 : 1";
    }
    pin (D) { direction : input; }
    pin (G) { direction : input; }
    pin (Q) {
      direction : output;
      function : "IQ";
      latch (IQ, IQN) { enable : "G"; }
    }
  }
  cell (BLACK) {
    pin (A) { direction : input; }
    pin (Y) { direction : output; three_state : true; }
  }
  cell (SDFF) {
    test_cell () {
      pin (Q) { signal_type : test_scan_out; }
      pin (SI) { signal_type : test_scan_in; }
      pin (SE) { signal_type : test_scan_enable; }
    }
    pin (D) { direction : input; }
    pin (CK) { direction : input; }
    pin (SI) { direction : input; }
    pin (SE) { direction : input; }
    pin (Q) {
      direction : output;
      ff (IQ) { next_state : "D"; clocked_on : "CK"; }
    }
  }
}
)LIB";

const db::Gate* gate_driving(const db::Module& module, const std::string& net) {
    for (const db::Gate& gate : module.gates) {
        if (gate.out == net) {
            return &gate;
        }
    }
    return nullptr;
}

std::uint32_t count_type(const db::Module& module, db::GateType type) {
    std::uint32_t count = 0;
    for (const db::Gate& gate : module.gates) {
        if (gate.type == type) {
            ++count;
        }
    }
    return count;
}

bool mentions(const db::Status& status, std::string_view text) {
    return status.message().find(text) != std::string::npos;
}

void test_parse() {
    frontend::Library library;
    const db::Status status = frontend::read_library(kLibrary, library);
    expect(status.ok(), "the demo library parses");
    if (!status.ok()) {
        std::cerr << "  " << status.message() << std::endl;
        return;
    }
    expect(library.name == "demo", "the library keeps its name");
    expect(library.cells.size() == 10, "the demo library has ten cells");

    const frontend::Cell* and2 = library.find_cell("AND2");
    expect(and2 != nullptr, "AND2 is present");
    if (and2 != nullptr) {
        expect(and2->area == 1.25, "AND2 keeps its area");
        const frontend::Pin* a = and2->find_pin("A");
        expect(a != nullptr && a->timing.size() == 1, "the timing group on A is retained");
        expect(a != nullptr && a->direction == frontend::Direction::Input, "A is an input");
        const frontend::Pin* y = and2->find_pin("Y");
        expect(y != nullptr && y->direction == frontend::Direction::Output, "Y is an output");
        expect(y != nullptr && y->function == "A * B", "Y keeps its function text");
    }

    const frontend::Cell* dff = library.find_cell("DFF");
    expect(dff != nullptr && dff->find_pin("Q") != nullptr, "DFF has a Q pin");
    if (dff != nullptr && dff->find_pin("Q") != nullptr) {
        const frontend::Pin& q = *dff->find_pin("Q");
        expect(q.ff.kind == frontend::Ff::Kind::FlipFlop, "Q declares an ff group");
        expect(q.ff.state == "IQ", "the ff state variable is IQ");
        expect(q.ff.clock == "CK" && q.ff.next_state == "D", "clocked_on and next_state are read");
    }
    expect(dff != nullptr && dff->find_pin("CK") != nullptr && dff->find_pin("CK")->clock,
           "CK is marked as a clock pin");

    const frontend::Cell* dffr = library.find_cell("DFFR");
    expect(dffr != nullptr && dffr->find_pin("Q") != nullptr &&
               dffr->find_pin("Q")->ff.clear == "CDN",
           "an ff clear is parsed");

    const frontend::Cell* lat = library.find_cell("LAT");
    expect(lat != nullptr, "LAT is present");
    if (lat != nullptr) {
        expect(lat->statetable.input_names == std::vector<std::string>{"D"},
               "the statetable names D");
        expect(lat->statetable.state_name == "IQ", "the statetable names its state variable");
        expect(lat->statetable.rows.size() == 4, "the statetable has four rows");
        expect(lat->statetable.rows.size() == 4 && lat->statetable.rows[1].next == "1",
               "row order and next state survive");
        const frontend::Pin* q = lat->find_pin("Q");
        expect(q != nullptr && q->ff.kind == frontend::Ff::Kind::Latch && q->ff.enable == "G",
               "the latch enable is read");
    }

    const frontend::Cell* black = library.find_cell("BLACK");
    expect(black != nullptr && black->find_pin("Y") != nullptr && black->find_pin("Y")->three_state,
           "three_state is read");

    const frontend::Cell* sdff = library.find_cell("SDFF");
    expect(sdff != nullptr && sdff->test_cell.defined, "test_cell is recorded");
    if (sdff != nullptr) {
        const auto signal = sdff->test_cell.signals.find("test_scan_in");
        expect(signal != sdff->test_cell.signals.end() && signal->second == "SI",
               "test_scan_in maps to SI");
        expect(sdff->test_cell.signals.count("test_scan_enable") == 1,
               "test_scan_enable is recorded");
    }
}

void test_expressions() {
    frontend::Expr expr;
    expect(frontend::parse_expression("A | B & !C", expr).ok(), "a precedence expression parses");
    expect(expr.kind == frontend::ExprKind::Or, "or is the root of A | B & !C");
    expect(expr.args.size() == 2 && expr.args[0].name == "A", "A is the left operand");
    expect(expr.args.size() == 2 && expr.args[1].kind == frontend::ExprKind::And,
           "and binds tighter than or");
    expect(expr.args.size() == 2 && expr.args[1].args.size() == 2 &&
               expr.args[1].args[1].kind == frontend::ExprKind::Not,
           "not binds tightest");

    expect(frontend::parse_expression("S ? A : B", expr).ok(), "a ternary parses");
    expect(expr.kind == frontend::ExprKind::Mux, "s ? a : b is a mux");
    expect(expr.args.size() == 3 && expr.args[0].name == "A" && expr.args[1].name == "B" &&
               expr.args[2].name == "S",
           "mux arguments are then, else, select");

    expect(frontend::parse_expression("A'", expr).ok() && expr.kind == frontend::ExprKind::Not,
           "postfix not parses");

    expect(!frontend::parse_expression("A &", expr).ok(), "a dangling operator is rejected");
    expect(!frontend::parse_expression("A $ B", expr).ok(), "an unknown operator is rejected");
    expect(!frontend::parse_expression("(A", expr).ok(), "an unclosed parenthesis is rejected");
    expect(!frontend::parse_expression("", expr).ok(), "an empty expression is rejected");
}

void test_decomposition(const frontend::Library& library) {
    const frontend::Cell* and2 = library.find_cell("AND2");
    db::Module and_module;
    if (and2 != nullptr) {
        expect(frontend::decompose_cell(*and2, and_module).ok(), "AND2 decomposes");
        expect(and_module.name == "AND2", "the module keeps the cell name");
        expect(and_module.inputs == std::vector<std::string>({"A", "B"}),
               "the module keeps the input pins");
        expect(and_module.outputs == std::vector<std::string>({"Y"}),
               "the module keeps the output pin");
        expect(and_module.gates.size() == 1 && and_module.gates[0].type == db::GateType::And2,
               "AND2 becomes one And2");
        if (and_module.gates.size() == 1) {
            expect(and_module.gates[0].fanins == std::vector<std::string>({"A", "B"}) &&
                       and_module.gates[0].out == "Y",
                   "And2 wires A and B to Y");
            expect(and_module.gates[0].lib_cell == and2->index,
                   "the gate keeps the library cell index");
        }
    }

    const frontend::Cell* nand2 = library.find_cell("NAND2");
    db::Module nand_module;
    if (nand2 != nullptr) {
        expect(frontend::decompose_cell(*nand2, nand_module).ok(), "NAND2 decomposes");
        expect(nand_module.gates.size() == 2, "NAND2 is two primitives");
        const db::Gate* y = gate_driving(nand_module, "Y");
        expect(y != nullptr && y->type == db::GateType::Not, "NAND2's Y is a Not");
        const db::Gate* inner = y != nullptr && y->fanins.size() == 1
                                    ? gate_driving(nand_module, y->fanins[0])
                                    : nullptr;
        expect(inner != nullptr && inner->type == db::GateType::And2, "NAND2's Not reads an And2");
    }

    const frontend::Cell* mux = library.find_cell("MUX2");
    db::Module mux_module;
    if (mux != nullptr) {
        expect(frontend::decompose_cell(*mux, mux_module).ok(), "MUX2 decomposes");
        const db::Gate* y = gate_driving(mux_module, "Y");
        expect(y != nullptr && y->type == db::GateType::Mux2, "MUX2 becomes Mux2");
        expect(y != nullptr && y->fanins == std::vector<std::string>({"A", "B", "S"}),
               "Mux2 takes then, else, select");
    }

    const frontend::Cell* inv = library.find_cell("INV");
    db::Module inv_module;
    if (inv != nullptr) {
        expect(frontend::decompose_cell(*inv, inv_module).ok(), "INV decomposes");
        const db::Gate* y = gate_driving(inv_module, "Y");
        expect(y != nullptr && y->type == db::GateType::Not &&
                   y->fanins == std::vector<std::string>({"A"}),
               "INV becomes Not A");
    }

    const frontend::Cell* tie = library.find_cell("TIE0");
    db::Module tie_module;
    if (tie != nullptr) {
        expect(frontend::decompose_cell(*tie, tie_module).ok(), "a constant cell decomposes");
        const db::Gate* y = gate_driving(tie_module, "Y");
        expect(y != nullptr && y->type == db::GateType::Tie0 && y->fanins.empty(),
               "TIE0 becomes Tie0 with no fanins");
    }

    const frontend::Cell* black = library.find_cell("BLACK");
    db::Module black_module;
    if (black != nullptr) {
        expect(frontend::decompose_cell(*black, black_module).ok(),
               "a functionless cell decomposes");
        const db::Gate* y = gate_driving(black_module, "Y");
        expect(y != nullptr && y->type == db::GateType::Blackbox,
               "a functionless output is a blackbox");
        expect(y != nullptr && y->fanins == std::vector<std::string>({"A"}),
               "the blackbox keeps its pins as fanins");
    }

    const frontend::Cell* dff = library.find_cell("DFF");
    db::Module dff_module;
    if (dff != nullptr) {
        expect(frontend::decompose_cell(*dff, dff_module).ok(), "DFF decomposes");
        const db::Gate* q = gate_driving(dff_module, "Q");
        expect(q != nullptr && q->type == db::GateType::Dff, "Q is a Dff");
        expect(q != nullptr && q->fanins.size() == 2 && q->fanins[1] == "CK",
               "the Dff's clock is CK");
        const db::Gate* data = q != nullptr && q->fanins.size() == 2
                                   ? gate_driving(dff_module, q->fanins[0])
                                   : nullptr;
        expect(data != nullptr && data->type == db::GateType::Buf &&
                   data->fanins == std::vector<std::string>({"D"}),
               "the Dff's data cone buffers D");
    }

    const frontend::Cell* dffr = library.find_cell("DFFR");
    db::Module dffr_module;
    if (dffr != nullptr) {
        const db::Status status = frontend::decompose_cell(*dffr, dffr_module);
        expect(!status.ok(), "an asynchronous clear is rejected");
        expect(mentions(status, "asynchronous"), "the async error says why");
    }

    const frontend::Cell* lat = library.find_cell("LAT");
    db::Module lat_module;
    if (lat != nullptr) {
        expect(frontend::decompose_cell(*lat, lat_module).ok(), "the statetable latch decomposes");
        const db::Gate* q = gate_driving(lat_module, "Q");
        expect(q != nullptr && q->type == db::GateType::Dlat, "Q is a Dlat");
        expect(q != nullptr && q->fanins.size() == 2 && q->fanins[1] == "G",
               "the Dlat's enable is G");
        const db::Gate* cone = q != nullptr && q->fanins.size() == 2
                                   ? gate_driving(lat_module, q->fanins[0])
                                   : nullptr;
        expect(cone != nullptr && cone->type == db::GateType::Or2,
               "the next-state cone is a sum of products");
        expect(count_type(lat_module, db::GateType::Not) == 1,
               "the table's 0 columns become inversions");
        expect(count_type(lat_module, db::GateType::Dlat) == 1, "the latch is one Dlat");
    }
}

void test_errors(const frontend::Library& library) {
    frontend::Cell bad;
    bad.name = "BAD";
    frontend::Pin input;
    input.name = "A";
    input.direction = frontend::Direction::Input;
    bad.pins.push_back(input);
    frontend::Pin output;
    output.name = "Y";
    output.direction = frontend::Direction::Output;
    output.function = "A & Z";
    bad.pins.push_back(output);
    db::Module module;
    const db::Status unknown = frontend::decompose_cell(bad, module);
    expect(!unknown.ok() && mentions(unknown, "Z"),
           "a function that names a missing pin is rejected");

    const frontend::Cell* lat = library.find_cell("LAT");
    if (lat != nullptr) {
        frontend::Cell wide = *lat;
        wide.name = "WIDE";
        wide.statetable.rows[0].inputs = {"00"};
        const db::Status wide_status = frontend::decompose_cell(wide, module);
        expect(!wide_status.ok() && mentions(wide_status, "not scalar"),
               "a multi-bit statetable row is rejected");
    }

    frontend::Library scratch;
    const db::Status no_direction = frontend::read_library(
        "library (x) { cell (C) { pin (A) { function : \"1\"; } } }", scratch);
    expect(!no_direction.ok() && mentions(no_direction, "direction"),
           "a pin without a direction is rejected");

    const db::Status duplicate = frontend::read_library(
        "library (x) {"
        "  cell (C) { pin (A) { direction : input; } pin (Y) { direction : output; "
        "function : \"A\"; } }"
        "  cell (C) { pin (A) { direction : input; } pin (Y) { direction : output; "
        "function : \"A\"; } }"
        "}",
        scratch);
    expect(!duplicate.ok() && mentions(duplicate, "duplicate"), "a duplicate cell is rejected");

    const db::Status unterminated = frontend::read_library("library (x) { cell (C) {", scratch);
    expect(!unterminated.ok(), "an unterminated group is rejected");

    const db::Status no_cells = frontend::read_library("library (x) { }", scratch);
    expect(!no_cells.ok(), "a library without cells is rejected");

    const db::Status bad_direction = frontend::read_library(
        "library (x) { cell (C) { pin (A) { direction : sideways; } } }", scratch);
    expect(!bad_direction.ok() && mentions(bad_direction, "sideways"),
           "an unknown direction is rejected");
}

db::NetId net_named(const db::DesignDb& database, const std::string& name) {
    for (db::NetId net = 0; net < database.flat().net_count(); ++net) {
        if (database.strings().at(database.flat().name_id[net]) == name) {
            return net;
        }
    }
    return static_cast<db::NetId>(db::kInvalidId);
}

void test_integration(const frontend::Library& library) {
    frontend::Library subset;
    subset.name = "subset";
    for (const char* name : {"AND2", "DFF", "LAT"}) {
        const frontend::Cell* cell = library.find_cell(name);
        expect(cell != nullptr, "the subset cell exists");
        if (cell != nullptr) {
            subset.cells.push_back(*cell);
        }
    }

    db::Design design;
    design.top = "top";
    const db::Status decomposed = frontend::decompose_library(subset, design);
    expect(decomposed.ok(), "the subset library decomposes into modules");
    if (!decomposed.ok()) {
        std::cerr << "  " << decomposed.message() << std::endl;
        return;
    }
    expect(design.modules.size() == 3, "three modules are added");

    db::Module top;
    top.name = "top";
    top.inputs = {"a", "b", "ck", "en"};
    top.outputs = {"y", "q", "q2"};
    design.modules.push_back(top);

    db::Instance and_instance;
    and_instance.path = "u00";
    and_instance.module = "AND2";
    and_instance.connections = {{"A", "a"}, {"B", "b"}, {"Y", "y"}};

    db::Instance dff_instance;
    dff_instance.path = "u01";
    dff_instance.module = "DFF";
    dff_instance.connections = {{"D", "y"}, {"CK", "ck"}, {"Q", "q"}};

    db::Instance lat_instance;
    lat_instance.path = "u02";
    lat_instance.module = "LAT";
    lat_instance.connections = {{"D", "a"}, {"G", "en"}, {"Q", "q2"}};
    design.instances = {and_instance, dff_instance, lat_instance};

    db::DesignDb db;
    db.design() = design;
    const db::Status flattened = db.flatten();
    expect(flattened.ok(), "a design built from decomposed cells flattens");
    if (!flattened.ok()) {
        std::cerr << "  " << flattened.message() << std::endl;
        return;
    }
    // And2, the Dff with its data buffer, and the latch's Not, two And2s, Or2
    // and Dlat.
    expect(db.flat().gate_count() == 8, "the design has eight primitive gates");
    // Four top inputs, three top outputs and the five internal cones' nets.
    expect(db.flat().net_count() == 12, "the design has twelve nets");

    const db::NetId q2 = net_named(db, "q2");
    expect(q2 != db::kInvalidId, "the latch output net exists");
    if (q2 != db::kInvalidId) {
        const db::GateId driver = db.flat().driver[q2];
        expect(driver != db::kInvalidId && driver != db::kPrimaryInput &&
                   db.flat().type[driver] == db::GateType::Dlat,
               "q2 is driven by the latch");
    }
}

}  // namespace

int main() {
    test_parse();
    test_expressions();

    frontend::Library library;
    const db::Status status = frontend::read_library(kLibrary, library);
    if (status.ok()) {
        test_decomposition(library);
        test_errors(library);
        test_integration(library);
    }

    if (failures != 0) {
        std::cerr << failures << " expectation(s) failed" << std::endl;
        return 1;
    }
    std::cout << "lib_test: ok" << std::endl;
    return 0;
}
