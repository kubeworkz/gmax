#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "core/db/design.h"
#include "core/frontend/diagnostics.h"

namespace gmax::frontend {

// One entry in the cell-count reconciliation report. `expected` comes from the
// design team's reference report (or is zero when the report omits a type);
// `actual` is the count of that Yosys cell type in the JSON.
struct CellCountDelta {
    std::string type;
    std::uint64_t expected = 0;
    std::uint64_t actual = 0;
};

struct CellCountReport {
    // Yosys cell types as written in the JSON, e.g. "$and" or "AND2X1".
    std::map<std::string, std::uint64_t> yosys_cells;
    // Primitive gates after import/flatten, e.g. "AND2" or "DFF".
    std::map<std::string, std::uint64_t> primitives;
    // The reference report supplied by the caller.
    std::map<std::string, std::uint64_t> expected_cells;
    std::vector<CellCountDelta> deltas;

    bool reconciled() const { return deltas.empty(); }
};

struct ImportResult {
    bool ok = false;
    db::Design design;
    std::vector<Diagnostic> diagnostics;
    CellCountReport counts;
};

struct YosysOptions {
    // Empty means $GMAX_YOSYS, then "yosys" from PATH.
    std::string yosys;
    std::string top;
    std::string netlist;
    std::vector<std::string> libraries;

    // Empty means a unique directory under the system temp directory. A
    // supplied directory is used as-is and its files are kept.
    std::filesystem::path work_directory;
    bool keep_work_directory = false;
    bool techmap = false;

    // Complete reference report keyed by Yosys cell type. Empty disables
    // reconciliation; a non-empty report must match every type exactly.
    std::map<std::string, std::uint64_t> expected_cells;
};

struct YosysResult {
    bool ok = false;
    // -1 when the executable could not be started.
    int exit_code = -1;
    std::string log;
    std::string json_path;
    std::vector<Diagnostic> diagnostics;
    db::Design design;
    CellCountReport counts;
};

// Reads a Yosys write_json document. `library_design` may already contain the
// decomposed library modules from WP-003; cells whose type names one of those
// modules become instances, so flatten() can expand them to primitives.
//
// The importer validates and flattens its own copy. Errors are reported as
// structured diagnostics with the source locations Yosys recorded in the JSON.
ImportResult import_yosys_json(std::string_view json, std::string_view top);
ImportResult import_yosys_json(std::string_view json, std::string_view top,
                               const db::Design& library_design,
                               const std::map<std::string, std::uint64_t>& expected_cells);

// Runs the pinned phase 0-1 recipe:
//
//   read_verilog -sv -lib <libraries>
//   read_verilog -sv <netlist>
//   hierarchy -check -top <top>
//   flatten
//   opt_clean
//   [techmap]
//   write_json <work>/design.json
//
// The Yosys process is spawned directly (no shell), its log is parsed into
// diagnostics, and the JSON is imported. A nonzero exit is never imported.
YosysResult elaborate_with_yosys(const YosysOptions& options);

// Exposed for the recipe test: the exact script written for `options`.
std::string yosys_recipe_script(const YosysOptions& options, std::string_view json_path);

}  // namespace gmax::frontend
