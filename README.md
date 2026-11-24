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
| Fault list | `gmax write_faults` | Builds the collapsed single-stuck-at fault list from a flattened `.dfdb`. |
| ATPG engine | `gmax run_atpg` | Runs PODEM with a SAT closer over the collapsed fault list, with deterministic thread merging and coverage/manifest reports. |
| Fault simulation | C++ library (`src/core/sim`) | 64-lane bit-parallel fault simulation with a scalar-reference differential test. No standalone CLI command yet. |
| SAT smoke check | `gmax sat_smoke` | Runs one SAT and one UNSAT fault through the SAT boundary and writes a diagnostic report. |

### In review

| Tool | Surface | Current state |
|---|---|---|
| SDC reader (WP-110) | `gmax read_sdc` on `feature/wp-110-sdc-subset` | Parses clocks, generated clocks, clock groups, false paths, case analysis, input/output delays, driving cells and loads, with warnings for unsupported commands. Not in `main` yet. |

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

The WP-110 SDC reader on `feature/wp-110-sdc-subset` parses, validates and reports the constraint set as a separate side input; it is not in `main` yet. Wiring those constraints into the persisted design database and the ATPG run is follow-on work; `run_atpg` today consumes the flattened database and fault list only.

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

# The SDC inspection command is part of the WP-110 branch, not this checkout yet.
```

`run_atpg` accepts optional per-fault backtrack and wall-clock limits, and `--threads N` for deterministic multi-threaded grading. It returns `4` (`ATPG incomplete`) if any fault aborts. Run `gmax help <command>` for the exact arguments.

## Repository layout

```text
src/core/
  db/          versioned design database and .dfdb persistence
  frontend/    Yosys and Liberty readers; SDC reader in review
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
