# gmax

`gmax` is an in-house, deterministic DFT/ATPG platform for the **grx930** and **G100** chips. It consumes qualified gate-level netlists, libraries and constraints and is being built to cover scan insertion, design-rule checking, stuck-at ATPG, fault simulation, test compression and ATE-ready pattern export.

The implementation is **one C++20 command-line tool**, not a collection of unrelated executables. The separate tools in this repository are command surfaces and owned components that share one versioned design database, the same IDs, names, constraints and provenance. That is deliberate: the netlist that is checked must be the netlist that is tested.

> **Status:** foundation work is in place and the current command surface runs a gate-level netlist through fault-list generation and stuck-at ATPG. This is not yet a production ATPG release: scan insertion/stitching, the DRC engine and pattern export are still planned. See the [architecture](dft-atpg-platform-architecture-grx930-g100.md) and [implementation plan](dft-atpg-implementation-plan.md) for the full roadmap.

## Tools at a glance

### Available now

| Tool | Surface | What it does today |
|---|---|---|
| Netlist intake | `gmax read_verilog` | Runs the Yosys front end, flattens a gate-level Verilog design and writes a `.dfdb` database. |
| Liberty reader | C++ library (`src/core/frontend/liberty`) | Parses the cell, pin, function, scan and timing subset needed by the platform. No standalone CLI command yet. |
| SDC reader | `gmax read_sdc` | Parses the restricted SDC subset: clocks, generated clocks, clock groups, false paths, case analysis, input/output delays, driving cells and loads. Unsupported commands warn with command and line. `--design`/`--output` attaches the set to a `.dfdb`. |
| Fault list | `gmax write_faults` | Builds the collapsed single-stuck-at fault list from a flattened `.dfdb`. |
| ATPG engine | `gmax run_atpg` | Runs PODEM with a SAT closer over the collapsed fault list, with deterministic thread merging and coverage/manifest reports. |
| Fault simulation | C++ library (`src/core/sim`) | 64-lane bit-parallel fault simulation with a scalar-reference differential test. No standalone CLI command yet. |
| SAT smoke check | `gmax sat_smoke` | Runs one SAT and one UNSAT fault through the SAT boundary and writes a diagnostic report. |

### Planned next

| Tool | Planned location | Scope |
|---|---|---|
| Scan replacement and stitching | `src/core/dft` | Mux-D replacement from `test_cell`, dedicated scan-flop path, chain assignment, lockup insertion and scan map output. |
| DRC engine | `src/core/dft` | DFT001–DFT020 rule checks, union-find clock domains, SCC and X-reachability checks, waivers and auto-fix. |
| Pattern IR and writers | `src/core/pattern` | Canonical pattern IR, STIL 1.1 output, WGL and tester cycle/timing files. |
| Test compression | `src/core/compress` | XOR/Illinois/EDT-style network and compression-aware ATPG constraints. |
| Tcl and Python shells | `src/shell/tcl`, `src/shell/python` | Thin front ends over the same command surface; Tcl first, Python API second. |
| Transition, diagnosis and scale work | `src/core/atpg`, `DIAG` | Transition-delay ATPG, chain diagnosis, full-chip partitioning and later AI-assisted heuristics. |

The component codes used by the architecture (`FE`, `DB`, `DFT`, `ATPG`, `FSIM`, `CMP`, `PAT`, `DIAG`, `AI`, `VER`) are ownership boundaries, not separate products or CLI names.

## How the current flow fits together

```text
gate-level Verilog
        │
        ▼
gmax read_verilog ───► design.dfdb
                             │
              ┌──────────────┴──────────────┐
              ▼                             ▼
     gmax write_faults              gmax run_atpg
              │                             │
              └──────► design.faults ───────┘
                                            │
                                            ▼
                              coverage.json + manifest.json
```

`gmax read_sdc constraints.sdc` parses and reports the constraint set, and with `--design`/`--output` it attaches that set to the design database. An attached set is stored with the netlist: both reports carry its status and canonical hash, and `run_atpg` refuses to grade a design whose set is incomplete. A case-analysis set is applied: `run_atpg` pins those values in PODEM and in the SAT miter, so a fault those values block is decided untestable, and the reports name `case_analysis` in their enforced list.

The constraint set is a side input, not another netlist: attaching it changes no flattening, so the netlist hash and an existing fault list stay valid.

The current ATPG path starts from an already-flattened gate-level database. Scan insertion, stitching, DRC, pattern generation and STIL/WGL export are the next stages, not part of the current `run_atpg` command.

## Build and test

**Prerequisites**

- CMake 3.20 or newer
- A C++20 compiler with warnings clean under the project's `-Wall -Wextra -Wpedantic -Werror` policy
- Yosys on `PATH`, or `GMAX_YOSYS` pointing at the Yosys executable, for `read_verilog`

The repository provides debug, release and ASan/UBSan presets:

```bash
cmake --preset debug
cmake --build --preset debug -j
ctest --preset debug --output-on-failure
./build/debug/gmax --version
```

The Debug build output is `build/debug/gmax`; Release and ASan use `build/release` and `build/asan`.

### Windows/WSL note for this checkout

This checkout is at `C:\Users\kubew\gmax` and its current build is a WSL build under `build/debug` (`/mnt/c/Users/kubew/gmax/build/debug`). The binary is a Linux executable, so run `build/debug/gmax` from WSL rather than PowerShell. In this WSL environment `cmake` is on `PATH`, but `ctest` is not; invoke the CTest installed beside CMake:

```bash
cd /mnt/c/Users/kubew/gmax
/home/kubew/.local/opt/cmake/bin/ctest --preset debug --output-on-failure
```

The pinned Yosys and optional MiniSat candidates can be populated into a developer cache:

```bash
sh tools/bootstrap-yosys.sh
export GMAX_YOSYS="${XDG_CACHE_HOME:-$HOME/.cache}/gmax/yosys/0.71/bin/yosys"

# Optional independent SAT cross-check for the G0.2 gate
sh tools/bootstrap-minisat.sh
export GMAX_MINISAT="${XDG_CACHE_HOME:-$HOME/.cache}/gmax/minisat/2.2.1/bin/minisat"
```

The bootstrap scripts are developer caches, not frozen dependencies; see [`third_party/README.md`](third_party/README.md) for the dependency policy and open legal/build-vs-buy decisions. Some process tests require the pinned Yosys cache and skip or fail clearly when it is absent.

## Quick start

```bash
# Elaborate a gate-level netlist through Yosys and write the database
./build/debug/gmax read_verilog design.v design.dfdb top

# Build the collapsed single-stuck-at fault list
./build/debug/gmax write_faults design.dfdb design.faults

# Run PODEM with the SAT closer; reports go to reports/
./build/debug/gmax run_atpg design.dfdb design.faults 200 10000 reports/

# Inspect the restricted SDC constraint set; the status and hash are printed
./build/debug/gmax read_sdc constraints.sdc

# Attach the set to the design, then grade the attached copy
./build/debug/gmax read_sdc constraints.sdc --design design.dfdb --output design-sdc.dfdb
./build/debug/gmax run_atpg design-sdc.dfdb design.faults 200 10000 reports/
```

`run_atpg` accepts optional per-fault backtrack and wall-clock limits, `--threads N` for deterministic multi-threaded grading, and `--profile <file>` to declare the capture procedure instead of letting the engine derive one. The profile is a small JSON document because TBD-06 has not frozen the profile format yet (ADR-013 section 1):

```json
{"capture": {"kind": "grouped", "cycles": [{"groups": [
  {"name": "fast", "clocks": ["clk_a"], "edge": 0},
  {"clocks": ["clk_b"], "edge": 20}]}]}}
```

`kind` is `simultaneous`, `grouped` or `sequential`, and the order of `cycles` and of each cycle's `groups` is the procedure. A group may carry `edge`; a cycle places every group or none, and with none the groups are one slowest period apart. A non-final cycle may carry `intermediate` (`"masked"`, or one state per captured clock), which is recorded in the procedure identity and not yet enforced, because whether a domain is fully scanned needs the scan model (WP-104). Unknown keys are ignored, a clock the SDC does not declare is refused, and a procedure section 4 refuses exits `2` and writes nothing. It returns `4` (`ATPG incomplete`) if any fault aborts. Run `gmax help <command>` for the exact arguments.

## Constraints

`read_sdc` reports the parsed set and its identity. Report-only is the default, so inspecting a file never writes anything:

```text
sdc: constraints.sdc
clocks: 1
  clock clk period=10 source=clk
case analysis: 1
  case test_mode=1
constraints status: complete
constraints hash: 1a2b3c4d
```

To carry the set with the design, name the database to read and the database to write:

```bash
gmax read_sdc constraints.sdc --design design.dfdb --output design-sdc.dfdb
```

`--design` and `--output` are used together or not at all. The output must name a different file from the input: the command refuses to write over the database it read, so a typo cannot destroy a design. The database is loaded and validated before anything is written, and a parse or resolution error leaves no file behind (exit `2`).

Attachment stores the set in the design database and changes nothing else — the flattened netlist, its `netlist_hash` and any fault list built from it stay valid. The constraint section is a minor-version extension of the file format:

- a database with **no** constraints is written as `.dfdb` **1.3** and is byte-identical to what earlier versions wrote, so existing designs and stored evidence do not change;
- an **attached** database is written as `.dfdb` **1.4**, whose only addition is the constraint section.

Constraint objects are named the way the netlist names them: a top-level port, or an internal net by the name the Verilog source gave it, queried with `get_nets`:

```tcl
set_case_analysis 0 [get_nets N118]
```

The frontend keeps a source name in preference to a Yosys-generated alias for the same net, so an internal net can be addressed by the name it has in the design rather than by an implementation detail that changes with the Yosys version.

While attaching, every constraint target is examined against the flattened design. A case value on a net that is not a top-level input port cannot be applied by the current engine, so the record is kept but the set is marked **incomplete** rather than dropped or silently ignored.

### Status and hash

| Status | Meaning |
|---|---|
| `absent` | The database carries no constraint set. |
| `complete` | Every target can be applied, so the ATPG engines may grade the design. |
| `incomplete` | A target cannot be applied, or the SDC used an unsupported construct. `run_atpg` refuses such a design with exit `2` and prints the reasons; there is no implicit "ignore the SDC" mode. |

`constraints_hash` is a CRC32 over a canonical form of the set: records sorted by their primary keys, member lists and `-group` lists sorted, numbers in shortest round-trip decimal, and the source path, comments, whitespace, line numbers and warning text excluded. Two SDC files that describe the same set hash the same, so reformatting a file or moving it does not change its identity. The value sits beside `netlist_hash` and `fault_list_hash` and is never folded into them, so one netlist graded with and without constraints keeps one netlist identity.

## Reports

`run_atpg` with a report directory writes `coverage.json` (`gmax-coverage-2`) and `manifest.json` (`gmax-run-manifest-2`). Both carry a `constraints` block beside the hashes:

```json
"constraints":{"status":"complete","hash":"1a2b3c4d","algorithm":"crc32","enforced":["case_analysis"],"capture":"not-enforced"}
```

- `status` and `hash` report the set the run actually loaded, so a run can be reproduced from its report. An `absent` set reports `"hash":null` rather than a hash of nothing.
- `enforced` lists the construct kinds the run applied. `case_analysis` appears when a complete set was resolved and handed to the engines; an absent set, an incomplete one, or one with no case values still reports `[]`, and that empty list is what tells a reader the attached set did not influence the result. The other construct kinds are parsed, persisted and reported but not consumed yet, so they never appear here.
- `capture` stays `not-enforced` until the scan and multi-clock capture engines land.

The manifest repeats the same status and hash under `constraints`. Its `path` field is currently empty because the source SDC file is not stored in the database; the ADR makes provenance optional and it is never part of the hash.

## Repository layout

```text
src/core/
  db/          versioned design database and .dfdb persistence
  frontend/    Yosys, Liberty and restricted SDC readers
  dft/         scan replacement, stitching and DRC        (planned)
  atpg/        fault list, PODEM, SAT, implication
  sim/         four-state and bit-parallel fault simulation
  compress/    XOR/Illinois/EDT constraints               (planned)
  pattern/     pattern IR and ATE writers                 (planned)
  report/      coverage, SAT smoke and run manifests
src/shell/     command-line parser and commands; Tcl/Python front ends (planned)
tests/         unit, differential and process tests; ISCAS'85 benchmark fixtures
profiles/      grx930 and G100 profile locations
docs/          ADRs and gate evidence
tools/         dependency bootstrap, formatting, offline-build checks
```

## Design documents

- [Platform architecture](dft-atpg-platform-architecture-grx930-g100.md) — component model, data model, command surface and verification strategy.
- [Implementation plan](dft-atpg-implementation-plan.md) — work packages, gates, schedule and ownership.
- [`docs/adr/`](docs/adr) — decision records for SAT integration, the pattern IR and deterministic threading.
- [`docs/gates/`](docs/gates) — G0 gate pack and sign-off evidence.

The governing rule across all tools is determinism: the same netlist, library, constraints and seed must produce byte-identical patterns and reports, and multithreaded results must merge in fault-ID order.
