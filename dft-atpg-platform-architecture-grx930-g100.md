# DFT/ATPG Platform Architecture — grx930 & G100

**Status:** Draft v0.1 for review · **Date:** 22 Nov 2026 · **Owner:** DFT Tools / Silicon Engineering

---

## 0. Document control and assumptions

This document specifies an in-house, TMAX-class DFT/ATPG platform covering scan insertion, design-rule checking, stuck-at ATPG, fault simulation, test compression, and pattern export for the **grx930** and **G100** chips.

The workspace contains no chip data, so the following are working assumptions. Items marked **[INTAKE]** must be confirmed before detailed design of that area is frozen.

| Assumption | Working position | Impact if wrong |
|---|---|---|
| Netlist form | Gate-level Verilog, flat or hierarchical, mapped to a qualified standard-cell library | RTL-only flow needs synthesis integration earlier |
| Library | Liberty (`.lib`) with scan-cell definitions and `test_cell` attributes | Missing scan attributes forces cell mapping work |
| Methodology | Full-scan, mux-D scan cells, one or more test clocks, shared or per-domain scan enable | Partial-scan changes the ATPG core substantially |
| First fault model | Single stuck-at | Transition/path delay is a later phase |
| Constraints | SDC for clocks and test-mode timing exceptions | Missing clock intent blocks multi-clock ATPG |
| ATE output | STIL 1.1 subset; tester-specific cycle file as a second writer | WGL-first would reorder the writer work |
| Design scale | 1–10 M gate instances per chip; blocks of 50 k–2 M gates for bring-up | Above 20 M gates changes partitioning and memory budget |
| Team | 8–12 engineers, C++/EDA background | Staffing is the largest schedule risk |

**Chip-specific inputs still required [INTAKE]:** netlists, Liberty libraries (including scan cells), SDC, scan insertion spec (chain count, length targets, clocking), test-mode pin list, memory/analog black-box list, compression IP decision, ATE platform and pattern format, and any existing TetraMAX scripts and golden patterns.

> **Guiding principle:** the ATPG engine is deterministic Boolean reasoning. AI is an offline coding accelerator and heuristic advisor. No LLM and no learned model sits in the fault-propagation decision path in v1.

---

## 1. Executive summary

We will build a **deterministic C++20 core** with Tcl and Python front ends, reusing proven open-source infrastructure where it reduces risk: **Yosys** for Verilog elaboration and equivalence checks, **ABC** for combinational representation and optimization, and **CaDiCaL** (with MiniSat as a second solver) for SAT-based ATPG. We will not reimplement a Verilog parser, a SAT solver, or a synthesis engine.

The system is organized around one **versioned design database** with two views: a hierarchical view used for scan insertion and DRC, and a flattened, word-parallel view used for ATPG and fault simulation. All tools — DRC, stitching, ATPG, simulation, pattern writing — operate on the same IDs, names, and constraints, which is the main defense against the classic DFT-tool failure mode of "the netlist the tool checked is not the netlist the tool tested."

Delivery is deliberately narrow and vertical:

1. **Phase 0:** prove the core on ISCAS'85 combinational circuits — read, build, fault-collapse, PODEM/SAT, simulate, report.
2. **Phase 1:** one small G100 block through the complete flow — read, DRC, stitch 8 scan chains, full-scan stuck-at ATPG, STIL out, gate-level pattern replay.
3. **Phase 2:** production hardening on real blocks, multi-clock support, compression baseline, and measured parity against TetraMAX on the same netlist and fault list.
4. **Phase 3+:** transition delay, diagnosis, full-chip scale, then learned heuristics in shadow mode.

The first production-quality milestone is **full-scan stuck-at ATPG with zero DRC escapes and pattern counts within 10–15% of TMAX**. Everything else is sequenced behind that.

### Key architectural decisions

| # | Decision | Rationale |
|---|---|---|
| 1 | C++20 core; Tcl + Python shells | Predictable performance, direct integration with Yosys/ABC/CaDiCaL, fast iteration for DFT engineers |
| 2 | Reuse Yosys for front-end elaboration | A correct Verilog-2005/SystemVerilog-subset parser is a multi-year project by itself; Yosys is ISC-licensed |
| 3 | Reuse CaDiCaL as primary SAT engine | MIT-licensed, incremental assumptions, strong CDCL performance; MiniSat for cross-checking |
| 4 | One design DB, hierarchical + flattened views | Prevents divergence between insertion, DRC, ATPG, and export |
| 5 | Structural engine first, SAT as hard-fault closer | PODEM/FAN are fast on easy/medium faults; SAT proves untestability and cracks hard faults |
| 6 | Bit-parallel fault simulation, 64 faults/word | Two orders of magnitude faster than scalar simulation at modest memory cost |
| 7 | Uncompressed scan baseline before compression | Compression-aware ATPG is a constraint layer, not a prerequisite for correctness |
| 8 | AI offline only, versioned, shadow-tested | Learned models are brittle across designs; determinism and reproducibility are non-negotiable for test |

### What this is not (v1)

Not a synthesis tool, not a silicon tester, not a memory-BIST or analog-test tool, not a replacement for foundry-qualified libraries, and not a formal equivalence checker. It consumes qualified netlists and libraries and produces qualified test patterns.

---

## 2. Goals, non-goals, and success metrics

### 2.1 Goals

- Generate stuck-at test patterns for full-scan grx930 and G100 designs with coverage and pattern counts competitive with TetraMAX.
- Insert and verify scan chains, including lockup latches and clock-domain rules, with a deterministic, auditable DRC report.
- Produce STIL (and a second ATE-specific format) that replays correctly in gate-level simulation and is accepted by the target ATE.
- Scale to full-chip netlists without per-fault allocation or Python-level loops in the hot path.
- Keep every result reproducible: same netlist + library + constraints + seed ⇒ byte-identical patterns and reports.
- Provide a clean extension point for transition-delay ATPG, compression, diagnosis, and ML heuristics.

### 2.2 Non-goals for v1

- Transition-delay, path-delay, IDDQ, cell-aware, or bridge fault models.
- Memory BIST, boundary scan insertion, analog test, and multi-die test.
- Automatic test-program generation for a specific ATE (we generate vectors and cycle data; the test program remains an ATE-team responsibility).
- Full UPF/power-domain-aware ATPG.
- Bit-exact TetraMAX command compatibility. We target a **TMAX-like command surface** and a script-migration layer, not undocumented behavior.

### 2.3 Success metrics

| Metric | Phase 1 gate | Production target |
|---|---|---|
| Stuck-at coverage (testable faults) | ≥ 95% on bring-up block | ≥ 99.5%, within 0.5 pt of TMAX |
| Pattern count vs. TMAX | measured, no gate | ≤ 1.10× on same constraints |
| ATPG runtime vs. TMAX | ≤ 5× | ≤ 1.5×, then ≤ 1.0× |
| DRC false negatives on golden set | 0 | 0 |
| DRC false positives | < 5% of rules fired | < 1% |
| Pattern replay in gate-level sim | 100% match | 100% match |
| Determinism | same seed ⇒ same output | byte-identical |
| Memory, 10 M-gate flat design | ≤ 16 GB | ≤ 32 GB |
| STIL acceptance by ATE simulator | one block | both chips, all modes |

---

## 3. Scope and chip profiles

Both chips are handled by the same engine; differences live in **per-chip profile files**, not in code branches. A profile is a versioned YAML file checked into the project and referenced by every run.

```yaml
# profiles/g100.yaml
chip: G100
netlist: netlists/g100_top.v
libraries:
  - libs/tsmc_n5_stdcells.lib
  - libs/tsmc_n5_scan.lib
sdc: constraints/g100_functional.sdc
scan:
  style: mux_d
  max_chains: 64
  target_chain_length: 2500
  lockup: auto
  scan_enable: shared_per_clock_domain
  test_clocks: [tck_a, tck_b]
  async_controls: [rst_n, por_n]
constraints:
  test_mode_ports: [test_mode]
  x_sources: [sram_wrapper*, analog_*]
fault_model: stuck_at
pattern_format: stil
tester: TBD
```

### 3.1 Design intake checklist

Before a chip can be brought up, the DFT team supplies:

1. Gate-level netlist(s) per mode, with black boxes explicitly declared.
2. Liberty libraries, including scan-cell `test_cell` groups and pin directions.
3. SDC with generated clocks, test clocks, and false paths for test mode.
4. Scan insertion specification: chain count, max length skew, lockup policy, clock mixing policy, test-mode pin assignment.
5. List of X sources: uninitialized memories, analog macros, clock gaters, synchronizers, black boxes.
6. Existing TetraMAX run scripts, fault lists, and golden patterns for differential testing.
7. ATE platform, pin map, and required pattern format.
8. Package/floorplan data if physical-aware chain ordering is in scope (phase 3).

### 3.2 What "done" means per chip

A chip is considered supported when: the full flow runs unattended on its top-level netlist; DRC is clean or every waiver is documented; stuck-at coverage meets the block-level target; patterns replay in gate-level simulation; the ATE team accepts the STIL; and a TMAX-vs-in-house comparison report exists for at least three representative blocks.

---

## 4. System architecture

### 4.1 Component view

```
┌─────────────────────────────────────────────────────────────────────┐
│ Shell & automation                                                  │
│   Tcl command interpreter · Python API · CI/regression dashboard    │
├─────────────────────────────────────────────────────────────────────┤
│ Front end                                                           │
│   Verilog (Yosys) → internal DB ← Liberty · SDC · DEF/LEF · SPF     │
├─────────────────────────────────────────────────────────────────────┤
│ Design database                                                     │
│   hierarchical view · flattened graph · constraints · X sources     │
│   name map · source locations · versioned binary persistence        │
├──────────────────┬──────────────────────┬───────────────────────────┤
│ DFT insertion    │ DRC engine           │ Fault list & collapsing   │
│ scan replacement │ rule graph traversal │ stuck-at, checkpoint      │
│ chain stitching  │ auto-fix + waivers   │ equivalence/dominance     │
├──────────────────┴──────────┬───────────┴───────────────────────────┤
│ ATPG scheduler              │ Fault simulation                       │
│ random → PODEM/FAN → SAT    │ bit-parallel, event-driven, threaded   │
│ backtracking · learning     │ fault dropping · X handling            │
├─────────────────────────────┴───────────────────────────────────────┤
│ Compaction and compression-aware ATPG                               │
│ static/dynamic merge · X-fill · chain masking · XOR network         │
├─────────────────────────────────────────────────────────────────────┤
│ Pattern writers                                                     │
│ STIL 1.1 · WGL · VCD debug · ATE cycle/timing files                 │
├─────────────────────────────────────────────────────────────────────┤
│ Reporting                                                           │
│ coverage · DRC · pattern stats · runtime/memory · diagnosis         │
└─────────────────────────────────────────────────────────────────────┘
        ▲
        │ offline, versioned, shadow-tested
┌───────┴─────────────────────────────────────────────────────────────┐
│ AI heuristic service: fault difficulty, ordering, chain assignment  │
└─────────────────────────────────────────────────────────────────────┘
```

### 4.2 Control flow

```
read profile → read libs → read netlist → elaborate/link → build DB
   → check design → insert scan (optional) → run DRC
   → build fault list → collapse → random-pattern pass
   → deterministic ATPG (PODEM/FAN) → SAT closure
   → compact → compression constraints → write patterns
   → replay/verify → report coverage and provenance
```

Each stage writes a **stage artifact** (DB snapshot or JSON manifest) with a content hash. A run manifest records tool version, profile hash, library hashes, netlist hash, seed, and stage hashes. This is what makes results reproducible and TMAX comparisons defensible.

### 4.3 Process and threading model

- One process per run; a run is a DAG of stages, checkpointable and restartable.
- ATPG work is partitioned by fault bucket and dispatched to a thread pool. Results are merged in **fault-ID order**, never completion order, so multithreaded runs match single-threaded runs.
- Fault simulation is pattern-parallel or fault-partition-parallel depending on mode; both produce deterministic reductions.
- No global mutable state in the engine; each worker owns its simulation state and SAT solver instance.
- Long-running stages emit progress events and can be interrupted without corrupting the checkpoint.

---

## 5. Technology decisions (ADRs)

### ADR-001 — Core language: C++20

**Decision:** C++20 for the engine, with C ABI boundaries around third-party libraries.

**Alternatives:** Rust (memory safety, modern tooling), Python (iteration speed, unacceptable runtime).

**Rationale:** The EDA ecosystem we depend on — Yosys, ABC, CaDiCaL, MiniSat — is C/C++. C++20 gives us `std::span`, concepts, designated initializers, and coroutines without a foreign-function boundary on the hot path. We can still use Rust for peripheral services (dashboards, pattern viewers, CI tooling) where its safety pays off.

**Mitigations for C++ risk:** AddressSanitizer/UBSan in CI, `-Wall -Wextra -Werror`, fuzzing on all parsers, no raw owning pointers, `std::span` for array views, bounds assertions in debug builds, and a small set of reviewed container wrappers. The hot loops are simple array math, which is easy to audit.

### ADR-002 — Front end: Yosys, with a staged integration

**Decision:** Use Yosys for Verilog elaboration and structural checks. Phase 0–1: invoke `yosys` as a subprocess and consume a documented JSON/ILANG netlist. Phase 2: add a small Yosys pass plugin that emits our compact binary DB directly, avoiding multi-GB JSON for full-chip designs. Keep the DB reader independent of Yosys so the engine can also ingest other netlist sources.

**Rationale:** Writing a correct Verilog/SystemVerilog parser is a multi-year effort and not where our differentiation lies. Yosys is ISC-licensed and handles parameters, generate blocks, buses, and hierarchy.

**Boundary:** Yosys elaborates and flattens; it does not decide DFT legality or synthesize our test logic. The exact Yosys version and script are pinned in the run manifest.

### ADR-003 — SAT engine: CaDiCaL primary, MiniSat secondary

**Decision:** CaDiCaL (MIT) through its C++ API for incremental SAT; MiniSat (MIT) as an independent cross-check on small instances and for regression diversity. Abstract both behind an `ISatSolver` interface so Kissat or a future solver can be swapped in.

**Rationale:** CaDiCaL is a top-tier CDCL solver with assumptions, efficient restarts, and a clean API. MiniSat's simplicity makes it valuable for differential testing of encodings.

**Caveat:** SAT is not automatically faster than structural ATPG for easy faults. It is the closer for hard faults and the proof engine for untestability, not the default path.

### ADR-004 — Design database: in-process, versioned binary

**Decision:** Custom C++ data structures with a versioned `.dfdb` binary persistence format. No SQL, no external database, no per-run text serialization in the hot path.

**Rationale:** ATPG is pointer-chasing and array-scanning; a relational store would add latency and complexity with no benefit. A binary snapshot supports checkpoint/restart and reproducible comparisons.

**Schema policy:** magic + major/minor version, section table, CRC per section, stable ID ordering. Minor versions are additive; a major-version converter is required before release.

### ADR-005 — Parallelism: explicit work queues, deterministic merge

**Decision:** `std::thread` pool with a bounded work queue and per-worker state. Add TBB only if profiling shows scheduler overhead. Results merged by fault ID or pattern ID, not completion order.

**Rationale:** Determinism is a hard requirement for test sign-off. A work-stealing scheduler that changes reduction order would make runs non-reproducible.

### ADR-006 — Command surface: Tcl first, Python API second

**Decision:** Embed Tcl 8.6 for the interactive/scripted flow, matching the mental model of DFT engineers and existing TMAX scripts. Expose a Python API via pybind11 for data analysis, regression, and ML feature extraction.

**Rationale:** Test engineers live in Tcl. Python is the better language for the surrounding analytics and model training. Both call the same C++ API; neither reimplements flow logic.

### ADR-007 — Pattern IR and writers

**Decision:** A canonical internal pattern IR (signals, cycles, scan frames, pin states, masks, timing sets) with writers for STIL 1.1, WGL, VCD (debug), and tester-specific cycle files. STIL is the reference format for round-trip tests.

**Rationale:** Coupling ATPG to a single ATE format makes every tester change an ATPG change. The IR also lets us replay and compare patterns from TetraMAX.

### ADR-008 — AI runtime is optional and outside the core binary

**Decision:** Train models in Python (PyTorch/scikit-learn), export to ONNX, and load them through ONNX Runtime only when a heuristic model is enabled. The core ATPG binary has no Python dependency and behaves identically when the model is absent.

**Rationale:** A model failure must degrade to the deterministic baseline, not stop a tape-out run.

### Dependency and license summary

| Component | Use | License (verify with legal) |
|---|---|---|
| Yosys | Verilog elaboration, equivalence | ISC |
| ABC | AIG, combinational optimization, CEC | MIT-style (UC Berkeley) |
| CaDiCaL | SAT | MIT |
| MiniSat | SAT cross-check | MIT |
| Tcl | Command shell | BSD-style |
| pybind11 | Python bindings | BSD-3-Clause |
| GoogleTest | Unit tests | BSD-3-Clause |
| ONNX Runtime | Optional ML inference | MIT |
| TBB (optional) | Task parallelism | Apache-2.0 |

A license review is required before any code is distributed outside the company. Internal use is lower risk, but attribution and notice obligations still apply.

---

## 6. Design data model

### 6.1 Two views, one source of truth

**Hierarchical view.** Preserves `Design → Module → Instance → Port/Net` for scan insertion, name mapping, and incremental work. Each instance knows its module, its connections, and its source location.

**Flattened view.** Produced after elaboration by deterministic depth-first traversal from the top instance, with siblings sorted by instance name. Every gate and net gets a stable 32-bit ID. The flattened view is what ATPG, fault simulation, and DRC traverse.

Stable IDs are essential: they make fault lists, checkpoints, scan maps, and coverage reports comparable across runs and across tools. Never derive an ID from a hash of a pointer or an unordered-map iteration order.

### 6.2 Primitive set and cell decomposition

ATPG needs a small, well-understood primitive set:

| Class | Primitives |
|---|---|
| Combinational | `AND2`, `OR2`, `NAND2`, `NOR2`, `XOR2`, `XNOR2`, `NOT`, `BUF`, `MUX2` |
| Constant | `TIE0`, `TIE1` |
| Sequential | `DFF` (with clock, enable, async set/reset), `DLAT` |
| Boundary | `PI`, `PO`, `BLACKBOX`, `SCAN_CELL` (a DFF with scan mux) |

Complex Liberty cells are decomposed into these primitives using their `function` attribute, with the original cell ID retained on each resulting gate. This keeps the ATPG engine small while preserving library-level reporting and pattern names. Sequential cells keep their original next-state semantics; a `statetable` is converted into a next-state function over the primitive set.

Cells with no defined function become `BLACKBOX` and are X-bounded at their outputs unless the user supplies a bypass model.

### 6.3 Flattened graph layout

The hot structures are structure-of-arrays, not vectors of objects with pointers:

```cpp
struct FlatNetlist {
  // Per gate
  std::vector<GateType>   type;        // 1 byte
  std::vector<uint32_t>   fanin_begin; // G + 1 offsets into fanins
  std::vector<NetId>      fanins;      // ~2.2 entries/gate
  std::vector<uint32_t>   fanout_begin;
  std::vector<NetId>      fanouts;
  std::vector<NetId>      out_net;
  std::vector<uint8_t>    flags;       // scan, blackbox, x_source, inserted
  std::vector<uint32_t>   level;       // topological level
  std::vector<uint32_t>   lib_cell;    // original Liberty cell index

  // Per net
  std::vector<uint32_t>   driver;      // gate id or kPrimaryInput
  std::vector<uint32_t>   name_id;     // string table
  std::vector<uint32_t>   source_loc;  // file/line table
};
```

Fanout lists are built once after elaboration; incremental insertion rebuilds only affected regions. Levelization is a single topological pass; cycles are reported as DRC errors rather than silently ordered.

### 6.4 Four-state values and word-parallel encoding

Every net carries two bit-planes per 64-lane word:

```
known[63:0]  — 1 if the lane has a defined 0/1 value
value[63:0]  — the value where known = 1; undefined where known = 0
```

This encodes 0, 1, and X without a third plane. Z is treated as X for ATPG (the tester masks it); the pattern writer preserves Z where the pin is high-impedance.

Gate evaluation uses bitwise formulas that propagate X correctly. Examples:

```
AND:  known = a.known & b.known | (a.known & ~a.value) | (b.known & ~b.value)
      value = a.value & b.value
OR:   known = a.known & b.known | (a.known & a.value) | (b.known & b.value)
      value = a.value | b.value
NOT:  known = a.known; value = ~a.value
MUX:  out = (s & a) | (~s & b) with X propagation on s
```

The exact formulas are unit-tested exhaustively against scalar 4-state evaluation for all input combinations.

**Fault simulation mode:** the good machine is scalar (one pattern at a time); the faulty machine packs 64 faults into a word. Each net stores a good bit and 64 faulty bits. Fault insertion masks the fault site for inactive lanes.

**Pattern-parallel mode (PPSFP):** pack 64 patterns per word; use for coverage verification and statistical sampling, not for the main ATPG loop where fault dropping is essential.

### 6.5 Memory budget

For a 10 M-gate flat design with ~22 M nets and 64-fault words:

| Structure | Estimate |
|---|---|
| Gate arrays (type, offsets, flags, level) | ~250 MB |
| Fanin/fanout arrays | ~350 MB |
| Net metadata and string table | ~300 MB |
| Good-machine values (1 bit/net) | ~3 MB |
| Faulty-machine values (16 B/net) | ~350 MB |
| SAT clauses for hard-fault subset | 1–8 GB, workload-dependent |
| Checkpoint and pattern IR | ~100 MB |

Target resident set: **≤ 16 GB for 10 M gates** in structural mode, **≤ 32 GB** with SAT closure on a large hard-fault set. If the design exceeds this, partition by clock/power domain or by top-level block; do not rely on swap.

### 6.6 Persistence and traceability

`.dfdb` layout: header (magic, version, endianness, section count), section table (offset, size, CRC32), then typed sections. Strings are stored once in a string table; all maps are serialized in sorted key order. A run manifest (JSON) records tool version, profile hash, library hashes, netlist hash, seed, and stage hashes.

Every gate and net retains:

- hierarchical name and source file/line,
- original Liberty cell and pin names,
- insertion provenance (`inserted_by`, parent cell, rule ID),
- scan mapping (chain ID, bit position) where applicable.

This is what lets a failing ATE pattern be traced back to a specific scan cell and a specific line of Verilog.

---

## 7. Front end

### 7.1 Verilog elaboration

Supported input: gate-level Verilog-2005 and a structural SystemVerilog subset (parameters, generate, buses, continuous assignments, constants, `celldefine`, black-box declarations). Behavioral RTL is accepted only through a pinned, versioned synthesis script that produces a mapped netlist; the DFT tool does not synthesize.

Yosys recipe (phase 0–1):

```
read_verilog -sv -lib libs/*.v
read_verilog netlist.v
hierarchy -check -top <top>
flatten
opt_clean
techmap          # only if a mapping was requested
write_json design.json
```

The importer then validates: unresolved modules, multiple drivers, undriven inputs, combinational loops, non-mapped cells, and width mismatches. Each finding becomes a structured diagnostic with a source location, not a log line.

### 7.2 Liberty

Read the subset needed for ATPG and scan: `cell`, `area`, `pin`, `direction`, `function`, `three_state`, `clock`, `next_state`, `clear`, `preset`, `ff`/`latch`, `statetable`, and `test_cell`. Timing arcs are parsed and retained for later timing-aware work but ignored by v1 ATPG.

Validation: for every cell with ≤ 6 inputs, exhaustively evaluate its Liberty `function` against the decomposed primitive network; a mismatch is a hard error. Scan-cell detection prefers `test_cell`; if absent, fall back to explicit attributes and naming conventions, and emit a warning that must be acknowledged in the profile.

### 7.3 SDC

A restricted Tcl interpreter executes SDC commands: `create_clock`, `create_generated_clock`, `set_clock_groups`, `set_false_path`, `set_case_analysis`, `set_input_delay`, `set_output_delay`, `set_driving_cell`, `set_load`. The engine stores clock domains, generated-clock relationships, and case analysis; it does not perform full timing analysis in v1.

SDC is used for: identifying capture/launch clocks, detecting clock-domain crossings for scan DRC, excluding false paths from capture checks, and constraining multi-clock ATPG. An SDC command outside the supported subset is a warning with a precise command and line number; unsupported constraints must never be silently dropped.

### 7.4 DEF/LEF and STIL/SPF

DEF/LEF reading is phase 3, for physical-aware chain ordering and wirelength estimates. SPF/STIL reading is phase 1 for importing existing scan procedures and replaying TetraMAX patterns; the writer is needed earlier than the reader for chip bring-up.

### 7.5 Parser validation

- Golden-file round trips on ISCAS'85/89, ITC'99, and internal blocks.
- Differential equivalence: original vs. re-exported netlist through ABC `cec` or Yosys `equiv_make` + `equiv_simple`.
- Grammar-aware fuzzing for Verilog, Liberty, and STIL readers; no crashes, no unbounded memory growth.
- Cell/net/DFF/black-box statistics compared against Yosys and TetraMAX reports; unexplained deltas block the run.

---

## 8. DFT insertion, scan stitching, and DRC

### 8.1 Scan replacement

Each flop is replaced by its scan-equivalent cell from Liberty (`test_cell`), preserving data, clock, enable, and asynchronous behavior. The default style is mux-D: a 2:1 mux is inserted before the D input, controlled by scan enable. If the library provides a dedicated scan flop, its scan pins are used directly and the mux is not inserted.

Rules:

- Functional mode (`SE = 0`) must be logically equivalent to the original netlist; this is proven, not assumed.
- Latches are not scanned in v1; they are either bypassed in test mode or reported as a DRC error. LSSD support is a later extension.
- Asynchronous set/reset must be controllable or disabled in test mode; uncontrolled async pins are a DRC error.
- Scan enable is static during capture. Per-clock-domain scan enables are allowed and are the default for multi-clock designs.
- Test-mode port, scan enable, and scan clocks are added to the pin interface with explicit directions and default values.

### 8.2 Chain assignment and ordering

Chain stitching is a constrained partitioning problem, not a graph walk:

**Inputs:** scan cells with clock domain, power domain, optional physical location, and async control group.

**Constraints:**

- never mix cells from incompatible clock domains in one chain without a lockup latch,
- chain length skew within the configured limit (default 5%),
- target chain count from the profile,
- one scan enable per compatible domain group,
- scan input/output pins available on the boundary.

**Algorithm:**

1. Partition cells by clock domain and power domain.
2. Within each partition, first-fit-decreasing bin packing into the requested number of chains, tie-broken by instance name.
3. Local swap pass to reduce max-length skew and lockup count.
4. Order cells within a chain by domain, then by physical proximity when DEF is available, then by name.
5. Insert lockup latches at domain crossings and at segment boundaries where the clock skew budget requires them.

Every step is deterministic. The cost function and all tie-breakers are recorded in the scan report so a human can explain why a cell landed where it did.

### 8.3 Scan DRC engine

The DRC engine is a deterministic rule evaluator over the flattened graph. It uses union-find for clock/power domains, Tarjan SCC for combinational loops, and fanin-cone traversal for X reachability. Every rule has an ID, severity, message template, source location, and optional auto-fix.

| Rule | Check | Severity | Auto-fix |
|---|---|---|---|
| DFT001 | Scan cell not assigned to a chain | Error | Assign or waive |
| DFT002 | Chain length skew exceeds limit | Error | Rebalance |
| DFT003 | Clock-domain crossing without lockup latch | Error | Insert lockup |
| DFT004 | Async set/reset not controlled in test mode | Error | Add test bypass |
| DFT005 | Gated clock not bypassed in test mode | Error | Insert test mux |
| DFT006 | Combinational loop | Error | None |
| DFT007 | Black box not bounded/bypassed | Error | Insert X-boundary |
| DFT008 | Undriven input | Error | Tie or waive |
| DFT009 | Multiple drivers on a net | Error | None |
| DFT010 | X source reaches scan cell D or observation path unmasked | Error | Insert mask/bypass |
| DFT011 | Scan enable reaches non-scan logic | Warning | Review |
| DFT012 | No test-mode control or conflicting test port | Error | None |
| DFT013 | Chain input/output floating | Error | None |
| DFT014 | Scan cell clock is not a defined test clock | Error | None |
| DFT015 | Bidirectional pin used as scan I/O | Error | None |
| DFT016 | Chain mixes incompatible clock groups | Error | Reassign |
| DFT017 | Latch present in scan path | Error | Bypass |
| DFT018 | RAM/analog output not X-bounded | Warning | Mask |
| DFT019 | False path hides a capture clock | Warning | Review |
| DFT020 | Chain length below minimum observable threshold | Warning | Merge |

Reports are emitted as JSON for tooling and as a human-readable table. Waivers carry an owner, reason, and expiry; expired waivers fail the run. The DRC golden set is mutation-tested: for each rule, a synthetic netlist is generated that violates exactly that rule, and the test fails if the rule does not fire.

### 8.4 Insertion outputs

- Modified netlist (Verilog export and updated `.dfdb`).
- Scan map: chain ID → ordered list of (instance, bit position, clock domain).
- SPF/STIL scan procedure with chain definitions.
- DRC report, waiver file, and insertion diff (inserted cells, rewired nets).
- Equivalence report for functional mode and a structural check for test mode.

---

## 9. ATPG core

### 9.1 Fault model and fault list

V1 targets the **single stuck-at** fault model. A fault is a pair `(net, stuck_value)` where the net is forced to 0 or 1. Fault sites are gate inputs, gate outputs, primary inputs, primary outputs, scan-cell data inputs, scan-cell outputs, and scan-chain segments. Tie cells, black-box internals, and unobservable test logic are excluded with a reason code.

**Checkpoint faults.** Generating a fault at every gate input and output is wasteful; many are equivalent. We generate faults at gate outputs, fanout branches, primary inputs/outputs, and scan-cell boundaries, then collapse.

**Collapsing.**

1. **Equivalence collapsing:** two faults are equivalent if every input vector produces identical behavior at all observation points. Structural signatures (gate type, fanin signatures) collapse most cases; SAT proves the remainder when needed.
2. **Dominance collapsing:** if detecting fault A always detects fault B, B can be removed from the target list. Applied conservatively, with proof, because an incorrect dominance merge hides real coverage loss.

Typical collapse is 3–5× on structured logic. The collapsed list is deterministic: sort by net ID, then stuck value 0 before 1. Report both the raw and collapsed counts, and keep the mapping so coverage can be reported at either granularity.

**Classification.** Every fault ends in exactly one state:

| State | Meaning | Evidence required |
|---|---|---|
| `DETECTED` | At least one pattern detects it | Pattern ID + simulation |
| `UNTESTABLE` | Proven undetectable | SAT UNSAT proof or structural rule |
| `ABORTED` | Budget exhausted | Backtrack/time budget recorded |
| `NOT_DETECTED` | Search completed without success, not proven | Run stats |
| `EXCLUDED` | Tied, masked, black-box, or out of scope | Reason code |

Never label a fault untestable because ATPG gave up. That distinction is the difference between a coverage report and a work of fiction.

### 9.2 Engine pipeline

The ATPG scheduler runs four passes over the fault list:

1. **Random-pattern pass.** Weighted pseudo-random patterns (LFSR or PCG with a fixed seed) plus fault simulation. Catches 60–80% of stuck-at faults on typical scan logic in seconds.
2. **Structural ATPG.** PODEM first, FAN for circuits where PODEM backtracks excessively. Each fault gets a backtrack limit and a wall-clock budget.
3. **SAT closure.** Faults that were aborted or are structurally hard go to SAT. SAT either finds a pattern or proves untestability; a timeout is reported as `ABORTED`, not guessed.
4. **Compaction.** Dynamic compaction during ATPG, static merge after, X-fill and chain masking before write-out.

Faults are distributed to worker threads in deterministic buckets. Within a bucket, order is shuffled with a fixed seed to avoid pathological structural ordering; results are merged by fault ID. The same seed and limits must produce the same pattern set regardless of thread count.

Per-fault budgets are explicit and recorded: `max_backtracks`, `max_conflicts`, `max_time_ms`, `max_patterns_per_fault`. A fault that exhausts its budget is `ABORTED`, not silently dropped.

### 9.3 Structural engine: PODEM

PODEM works on a 5-valued algebra (`0`, `1`, `X`, `D`, `D̄`) where `D` is 1 in the good circuit and 0 in the faulty circuit. The implication engine maintains values on nets and a queue of gates whose outputs may change. Each controlling-value gate (AND/OR/NAND/NOR) keeps occurrence counts of controlling/non-controlling inputs so implication is O(1) per affected gate; XOR/XNOR/MUX gates use direct evaluation.

The loop:

```
atpg_podem(fault f):
  if not activate(f): return CONFLICT
  if not imply(): return CONFLICT
  return search()

search():
  if fault_is_detected_at_po(): return SUCCESS
  if conflict(): return CONFLICT
  if not x_path_exists_to_po(): return CONFLICT

  objective = choose_objective()          # activate or propagate
  (pi, value) = backtrace(objective)      # walk to a primary input or scan cell
  if pi is None: return CONFLICT

  for assignment in [(pi, value), (pi, 1 - value)]: # try the chosen value, then the opposite 0/1 value
    checkpoint()
    assign(pi, assignment)
    if imply() and search(): return SUCCESS
    restore()
  return CONFLICT
```

Production details that the pseudocode hides:

- **X-path check** must account for controlled gates, reconvergent fanout, and X sources; a naive path search produces optimistic patterns that fail simulation.
- **Backtrace** tracks multiple objectives through fanout-free logic, choosing the easiest-to-control path; it must stop at scan cells and primary inputs.
- **Conflict handling** is chronological in v1 with a backtrack limit: when a primary-input assignment conflicts, the search tries the opposite 0/1 value before backtracking further. Conflict-driven learning and non-chronological backtracking are phase 3 improvements, gated by benchmark gains.
- **Static learning** (implications such as `net=0 ⇒ downstream=1`) is computed once per design and stored sparsely; it is optional and disabled when it costs more memory than it saves.
- **Redundant faults** are pruned before search by structural analysis (tied nets, unobservable nets, blocked X paths).

### 9.4 Structural engine: FAN

FAN reduces backtracks by working on **fanout-free regions (FFRs)** and **head lines**, performing multiple backtrace and using dominators to focus propagation. It is implemented after PODEM is correct and measured, not instead of it. The FAN engine shares the implication engine, fault activation, and pattern recording with PODEM; only the search strategy differs. This keeps the correctness surface small and allows per-fault engine selection based on circuit structure.

### 9.5 SAT-based ATPG

The SAT engine is the closer and the proof engine. It uses a **two-copy miter**:

- Copy G: the good circuit.
- Copy F: the faulty circuit, identical except the fault net is forced to the stuck value.
- Primary inputs are shared variables between the copies, except for a fault on a primary input: the faulty copy uses a separate variable for that input (or the fault is modeled on its fanout branch) so the good copy still sees the applied value.
- A detection clause requires at least one observable output—a primary output or a scan-cell data input in the capture frame—to differ between G and F.

Each primitive gate becomes Tseitin clauses. For `y = a AND b`:

```
(¬a ∨ ¬b ∨ y)
(a ∨ ¬y)
(b ∨ ¬y)
```

Constants are unit clauses. Fault activation adds a unit assumption on the faulty-copy net. The detection clause is a disjunction over XOR indicators for observable outputs (primary outputs and scan-cell data inputs in the capture frame).

Flow per fault:

```
build_good_cnf(design)                  # once per design/mode
for each fault f with budget:
    cnf_f = clone_or_extend(good_cnf)
    encode_fault_copy(cnf_f, f)
    add_detection_clause(cnf_f, observable_outputs)
    result = solve(cnf_f, assumptions={activate_f}, limits)
    SAT   -> extract PI assignment, X-fill, simulate, record pattern
    UNSAT -> mark proven_untestable with proof metadata
    UNKNOWN -> mark aborted, preserve budget reason
```

Implementation notes:

- **Solver reuse.** Cloning the full good CNF per fault is expensive. Benchmark three strategies: (a) CaDiCaL solver copy, (b) one solver with activation literals and permanent fault clauses, (c) per-fault solver with shared good clauses via a compact re-encoding. Pick the fastest that preserves soundness; do not share learned clauses across faults unless proven safe.
- **X handling.** A SAT model is a total assignment, which is bad for pattern compaction because it fills don't-cares with arbitrary 0/1 values. After a model is found, run ternary simulation and recursive ODC analysis to identify don't-cares, then X-fill for mergeability. A partial-assignment SAT approach (the PA-MiniSat line of work) is a research option for phase 3, not a dependency for v1.
- **Constraints.** Test mode, scan enable, clock configuration, and X-source masks are encoded as unit clauses or assumptions. A pattern that violates a constraint is invalid even if SAT says it detects the fault.
- **Proofs.** An UNSAT result is only accepted when the encoding is validated against the structural engine on small circuits. Record solver version, assumptions hash, conflicts, and time. Untestable claims without this metadata are rejected by the report layer.
- **Timeouts.** SAT timeouts are normal. A timeout is `ABORTED`, never `UNTESTABLE`.
### 9.6 Hybrid scheduling

The scheduler chooses an engine per fault based on structure and history, not on a model in v1:

| Fault class | First engine | Fallback |
|---|---|---|
| Easy, high observability | Random + fault sim | PODEM |
| Medium, reconvergent | PODEM | FAN, then SAT |
| Hard, deep/dominator-heavy | SAT | Longer SAT budget |
| Proven untestable structurally | None | Report evidence |
| Aborted after all budgets | None | Report with budget |

A learned difficulty classifier is a later optimization (section 13); the deterministic routing above must stand on its own.

### 9.7 Full-scan and multi-clock semantics

For full-scan stuck-at ATPG, each scan cell's Q output is a pseudo-primary input (the loaded or captured state), and its D input is a pseudo-primary output (the next state captured into the cell). A test pattern has four phases:

1. **Load:** shift the scan frame through the chains with `SE=1`.
2. **Capture:** pulse the capture clock(s) with `SE=0`; the combinational logic from scan-cell Q outputs and primary inputs to scan-cell D inputs and primary outputs is evaluated.
3. **Unload:** shift the captured state out with `SE=1` while loading the next frame.
4. **Compare:** observe primary outputs and unloaded scan values against expected values.

Multi-clock rules:

- Capture clocks are defined by the profile and SDC; clocks in different groups are not pulsed simultaneously unless explicitly allowed.
- Sequential capture (pulse clock A, then B) requires that the intermediate state is either scanned or X-masked; this is a constrained ATPG mode, not an afterthought.
- Clock gaters must be bypassed in test mode; otherwise their domains are untestable and must be reported as such.
- Asynchronous resets are held inactive during capture unless a test procedure explicitly exercises them.

X sources are masked at the earliest legal point: chain bit, observation pin, or capture cycle. Every mask is recorded with the fault coverage it costs, so the coverage report can distinguish "untestable logic" from "we chose to mask it."

### 9.8 Compaction

Three layers, in order:

1. **Dynamic compaction:** while a pattern is still in the tester, add objectives for other undetected faults that are compatible with the current assignments. Stop when no compatible objective exists or the pattern becomes too constrained.
2. **Static merge:** merge patterns whose input assignments are compatible (equal where both are specified). Greedy merging first; for large pattern sets, a set-cover ILP on a reduced universe. Cap merge runtime; pattern count matters more than a perfect cover.
3. **X-fill and masking:** assign don't-cares to maximize mergeability and minimize toggles, then insert chain masks for X sources. For compressed patterns, X-fill must also satisfy the decompressor's linear constraints.

Compaction never changes fault detection without re-simulation. Every merged or filled pattern is re-graded before it is written.

### 9.9 Coverage and reporting

Reports distinguish:

```
raw coverage          = detected / all faults
collapsed coverage    = detected / collapsed faults
testable coverage     = detected / (detected + not_detected + aborted)
ATPG coverage         = detected / (detected + not_detected + aborted + untestable)
```

Never publish a single "coverage" number without its denominator and fault-list hash. Break coverage down by clock domain, block, chain, fault class, and exclusion reason. Export the fault list with per-fault state, pattern IDs, and proof metadata so another tool can audit it.

---

## 10. Fault simulation

Fault simulation is used for random-pattern grading, fault dropping during ATPG, final coverage verification, pattern replay, and compression analysis. It is the referee: a pattern is only real if simulation detects the fault.

### 10.1 Parallel-fault simulation

The primary mode packs **64 faults per word**. For each net, keep:

- the good value for the current pattern (scalar 2-bit 4-state), and
- 64 faulty values in two 64-bit planes (`known`, `value`).

At a fault site, the faulty plane is forced for the lanes where that fault is active. Gates evaluate their faulty output word with the same bitwise formulas as the good machine. Faults that reach an observable output with a value different from the good machine are detected and dropped.

Event-driven evaluation: a gate enters the queue when any fanin word changes. The queue is level-ordered to avoid repeated evaluation in the same pattern. Activity masks skip lanes that are already dropped.

### 10.2 Pattern-parallel simulation (PPSFP)

For coverage verification and sampling, pack **64 patterns per word** and simulate one fault at a time (or a small fault group). This is faster when the pattern set is large and fault dropping is not needed. It is also the natural mode for GPU experiments later. PPSFP and parallel-fault must agree exactly on small circuits; that differential test is part of CI.

### 10.3 Threading and determinism

Fault groups are independent. Each worker owns its good/faulty state, simulates the full pattern set for its groups, and returns a detected-fault bitmap. The union is order-independent. Pattern generation itself is parallelized by fault bucket, with deterministic merge as described earlier.

### 10.4 Validation

- Scalar reference simulator on circuits ≤ 10 k gates: exhaustive or random vectors, compare every net value including X.
- ISCAS'85 and ITC'99 coverage compared against Atalanta and the open-source Fault toolchain.
- Random netlist fuzzing: generate small acyclic netlists with known faults, compare detection sets against brute-force enumeration.
- Golden TetraMAX patterns replayed through our simulator: detection sets must match on the same netlist and fault list, or the difference must be explained by a documented constraint.

### 10.5 GPU path (later)

A GPU fault simulator can be 5–20× faster for large pattern sets, but it is a separate engineering project: memory layout, divergence, X handling, and determinism all need care. Do not put it on the critical path to first silicon. The CPU bit-parallel engine is the reference implementation against which any GPU kernel is validated.

---

## 11. Test compression

Compression is phase 2, after uncompressed stuck-at ATPG is trusted. The architecture is **EDT-class**: a broadcaster loads a small number of compressed channels; an XOR decompressor expands them to many internal scan chains; a compactor XORs chain outputs into a smaller number of observation channels; masks handle X.

### 11.1 Why in-house XOR compression

Licensed compression IP (Synopsys, Siemens) comes with constraints, formats, and legal obligations. A simple in-house XOR/Illinois-scan network gets most of the pattern-count benefit for stuck-at and keeps the ATPG engine in control of the constraints. If the chips already use a licensed compressor, the ATPG engine must accept its decompressor equations and compactor structure as configuration; it should not assume its own network.

### 11.2 ATPG constraints

- **Decompressor:** each internal chain input is a linear combination (XOR) of compressed channel variables. A compressed pattern must satisfy these equations; ATPG can solve them by Gaussian elimination over GF(2) or by adding them to the SAT encoding.
- **Compactor:** multiple chains XOR into one observation channel, which creates aliasing. ATPG must know the compactor matrix to predict observability and to avoid claiming detection that the compactor masks.
- **X-masking:** compactor inputs with X values must be masked; the mask controller is part of the pattern IR, not an afterthought.
- **Chain balancing:** compressed chains are typically short and equal-length; stitching must target that structure.

### 11.3 Metrics and sequencing

Measure compression ratio (uncompressed patterns ÷ compressed patterns), coverage delta, pattern count, test time, and mask overhead. Ship in this order: uncompressed baseline → Illinois scan (simple, no compactor) → XOR decompressor/compactor → compression-aware ATPG → advanced seed selection. Each step must beat the previous one on a real block, not just on ISCAS.
---

## 12. Pattern output and ATE interface

### 12.1 Canonical pattern IR

All patterns are stored in a tester-independent IR:

- signal map (logical name, direction, physical pin),
- timing sets (period, drive edges, strobe, clock pulses),
- cycles (drive, compare, mask, high-Z),
- scan frames (chain, bit order, load/unload),
- expected values and per-cycle masks,
- annotations (pattern ID, detected faults, coverage contribution).

Writers consume the IR. Readers (STIL/SPF import) produce it. Replay and comparison operate on it. No writer is allowed to invent semantics that are not in the IR.

### 12.2 STIL 1.1

The first writer targets a validated STIL 1.1 subset: `Signals`, `SignalGroups`, `Timing`/`WaveformTable`, `Pattern` with waveform-table references, `ScanStructures`, and procedures for load/unload/capture. Use standard constructs; keep vendor extensions behind a flag. The output is parsed back into the IR and compared before it is accepted.

### 12.3 WGL and tester cycle files

WGL is the second writer, driven by the same IR. Tester-specific cycle/timing files are added once the ATE platform is known **[INTAKE]**. A test-program generator is out of scope; the ATE team consumes our vectors and cycle data.

### 12.4 Pin and timing semantics

- Logical-to-physical pin mapping is explicit and versioned.
- Directions, drive/receive, and bidirectional pins are validated against the netlist.
- Multi-clock timing sets are generated per capture procedure; no implicit simultaneous capture.
- X is exported as a mask, never as a random 0/1 value.
- High-Z is preserved where the tester supports it; otherwise it is masked and flagged.

### 12.5 Verification before handoff

1. Parse the written STIL back into the IR; structural equality with the source IR.
2. Replay through the gate-level simulator with timing-agnostic semantics; every pattern must detect exactly the faults recorded.
3. Run the ATE vendor's simulator; resolve every syntax/semantic warning.
4. Compare pattern count, coverage, and runtime against TetraMAX on the same netlist and constraints.

---

## 13. AI and ML architecture

### 13.1 The boundary

| Component | Approach | Why | Runtime location |
|---|---|---|---|
| Netlist/Liberty/STIL parsers | AI-assisted code generation, human-reviewed | Boilerplate-heavy, testable | Build time only |
| Scan stitching and DRC | Deterministic rules | Legal/illegal test structures must be exact | Core |
| Fault collapsing | Deterministic + SAT proofs | Coverage accounting must be auditable | Core |
| ATPG search | Deterministic PODEM/FAN/SAT | Boolean reasoning; hallucination is fatal | Core |
| Fault simulation | Deterministic bit-parallel | Coverage referee | Core |
| Pattern compaction | Deterministic set cover + greedy | Must preserve detection | Core |
| Fault difficulty prediction | Gradient-boosted model | Cheap, measurable routing gain | Optional sidecar |
| Backtrace/objective ordering | Learned ranking, shadow mode | Search heuristic only | Optional sidecar |
| Chain assignment | Graph heuristic / GNN experiment | Partitioning quality | Offline recommendation |
| Compression seed/network | Offline search + ML surrogate | Expensive combinatorial tuning | Offline |
| Log triage and diagnosis | NLP clustering | Human workflow acceleration | Offline |
| Code, tests, docs | LLM assistance | Productivity | Build time only |

**Hard rule:** no LLM output is ever emitted as a test vector, a fault classification, or an untestable claim. A pattern exists only after deterministic simulation detects the fault. An untestable claim exists only after a SAT proof or a structural rule.

### 13.2 Highest-value ML use cases

1. **Fault difficulty classifier.** Predict whether a fault will need SAT, using structural features (reconvergence, fanout size, depth, dominator count, SCOAP-like controllability/observability, number of X paths, prior ATPG outcome on similar structures). Route hard faults directly to SAT and easy faults to PODEM. Target: 20–40% fewer SAT calls at equal coverage; measure on held-out blocks.
2. **Backtrace ordering.** Learn a ranking over candidate backtrace paths from successful ATPG traces. Start supervised; consider RL only after the supervised baseline is stable. Inference must be deterministic and bounded (microseconds per decision).
3. **Chain assignment.** Learn a cost model for partitioning scan cells by domain/locality; use it to seed the deterministic bin packer, never to replace the legality checks.
4. **Compression seed selection.** Offline search over decompressor seeds and XOR networks, with a surrogate model to prune candidates; verify every candidate by linear-algebra constraints and ATPG.
5. **Pattern selection.** Predict which patterns are most valuable for a reduced pattern set, then confirm by fault simulation. Useful for test-time reduction, not for coverage sign-off.
6. **Failure-log triage.** Cluster ATE failures and rank suspect scan chains/blocks; a human confirms with diagnosis tools.

### 13.3 Data and training pipeline

- Features are extracted from the design DB and deterministic run statistics; no raw netlist text is sent to a model.
- Labels come from completed ATPG runs (engine used, backtracks, SAT result, time).
- Split by **design**, not by pattern or fault, to prevent leakage. Keep a held-out chip/block that the model never sees.
- Store model hash, training data hash, feature schema version, and evaluation report in the model registry.
- Inference is CPU-first (ONNX Runtime); no GPU dependency in the core flow.

### 13.4 Guardrails and promotion

1. **Shadow mode:** run the model alongside the baseline for at least three blocks or one full chip; log its recommendations and outcomes without acting on them.
2. **Promotion gate:** the model must improve the target metric (SAT calls, runtime, or pattern count) with a statistically significant margin on held-out designs, with no coverage loss.
3. **Fallback:** disabling the model restores the deterministic baseline exactly; the run manifest records whether it was enabled.
4. **Bounds:** model inference has a fixed time budget; a timeout falls back silently.
5. **Monitoring:** feature drift, fallback rate, and recommendation acceptance rate are tracked per design.
6. **No feedback loops:** never train on data generated by a model that was itself used to label faults; use deterministic engine outcomes only.

### 13.5 AI-assisted coding policy

LLMs are useful for parser skeletons, unit tests, STIL/WGL formatting, documentation, and refactoring. They are not trusted with scan legality, fault collapsing, or ATPG search. Every AI-generated change must pass the same gates as human code: unit tests, differential tests, fuzzing for parsers, sanitizer builds, license scan, and review by the component owner. Generated code is never merged solely because it compiles.

---

## 14. Verification and validation

The tool is verified at four levels: unit/property, differential, benchmark, and silicon. A level is not "done" because a demo passed; it is done when the gate is in CI and failures block merges.

### 14.1 Test pyramid

| Level | What | Examples | Runs |
|---|---|---|---|
| Unit | Pure functions and small components | 4-state gate eval, Liberty function parse, fault ID ordering, scan rule predicate | Every commit |
| Property-based | Invariants over generated inputs | Insertion preserves function; patterns replay; fault collapse never removes a detectable fault | Every commit / nightly |
| Differential | Same input through two independent implementations | Our sim vs scalar ref; our ATPG vs Atalanta; our netlist vs ABC CEC | Nightly |
| Integration | Full flow on fixed designs | ISCAS'85/89, ITC'99, G100 block | Nightly |
| Benchmark | Coverage/pattern/runtime vs commercial | TetraMAX on same netlist/lib/SDC | Weekly / per release |
| Silicon | ATE correlation | First silicon, failing pattern debug | Per tape-out |

### 14.2 Benchmark ladder

| Rung | Suite | Size | What it proves | Exit gate |
|---|---|---|---|---|
| 1 | ISCAS'85 (c432–c7552) | 0.1–3 k gates | Combinational correctness, PODEM/SAT, fault collapsing | 100% coverage on all testable faults; patterns replay |
| 2 | ISCAS'89 (s27–s38584) | 0.1–20 k gates | Sequential-to-scan conversion, scan mapping | Coverage parity with published results within 1 pt |
| 3 | ITC'99 (b01–b22) | 1–200 k gates | Larger combinational/sequential blocks, multi-clock experiments | No aborts on testable faults; runtime within budget |
| 4 | OpenCores / internal small blocks | 10–100 k gates | Realistic RTL structures, memories, black boxes | DRC clean or waived; coverage target met |
| 5 | G100 bring-up block | ~50 k gates | End-to-end scan insertion, ATPG, STIL, replay | Phase 1 gate |
| 6 | G100 subsystem | 0.5–2 M gates | Multi-clock, compression, scale | Phase 2 gate |
| 7 | grx930 / G100 top | 5–10 M gates | Full-chip scale, runtime, memory | Production gate |

Published ISCAS'89 numbers vary with scan methodology and fault list; compare only under an explicitly documented flow. Do not tune to a benchmark in a way that does not transfer to the chips.

### 14.3 TetraMAX parity protocol

Parity claims are only meaningful when both tools see the same problem. Freeze and hash:

- gate-level netlist, libraries, SDC, and test-mode constraints;
- fault list (or generate from the same checkpoint policy and compare counts);
- scan configuration and chain count;
- TetraMAX version and options;
- machine and thread count.

Run both tools. Compare:

| Dimension | Comparison | Acceptance |
|---|---|---|
| Fault states | Per-fault detected/untestable/aborted | Every difference explained; no false untestable |
| Coverage | Testable coverage | Within 0.5 pt |
| Pattern count | Same fault list, same constraints | ≤ 1.10× TMAX |
| Runtime | Same hardware, warm cache | ≤ 1.5× initially, ≤ 1.0× target |
| Pattern replay | Our STIL in gate-level sim | 100% detection match |
| DRC | Rule-by-rule on golden netlists | No false negatives; false positives triaged |

The first parity run will expose differences in fault counting, X handling, and default constraints. Document each difference in a parity ledger with an owner and a resolution. A raw coverage number without the ledger is not evidence.

### 14.4 Formal and structural checks

- **Insertion equivalence:** the functional-mode netlist (`SE=0`) must be equivalent to the original. Use ABC `cec` or Yosys `equiv_make` + `equiv_simple`; run on every insertion.
- **Scan structure:** chain order, bit positions, and lockup latches are checked structurally against the scan map, not just by simulation.
- **SAT proofs:** every `UNTESTABLE` result carries solver metadata; small cases are cross-checked with MiniSat and with structural analysis.
- **DRC mutation testing:** for each rule, generate a netlist that violates exactly that rule; the rule must fire. This catches both missing rules and rules that fire on everything.
- **Pattern replay:** every written pattern is re-simulated; recorded fault detections must match exactly.

### 14.5 ATE and silicon correlation

The final gate is silicon. Before tape-out, the ATE team runs our STIL through the vendor simulator. After first silicon:

1. Run the full pattern set on the tester; collect failing cycles and pins.
2. Map failures back to scan chains and nets using the scan map and pattern annotations.
3. Compare failing patterns with expected X masks and timing assumptions.
4. Update the profile (masks, timing, chain order) and re-issue patterns with a new version.
5. Record yield and failure signatures; feed them to diagnosis (phase 3).

No tool is "production ready" before this loop closes at least once. A pattern set that passes simulation but fails on silicon is a specification bug, not a silicon bug, until proven otherwise.

### 14.6 CI gates

| Gate | Checks | Blocks |
|---|---|---|
| Commit | Build (debug/release), unit tests, sanitizer smoke, format/lint, deterministic hash test | Merge |
| Nightly | Full unit/property suite, ISCAS'85/89, parser fuzz smoke, DRC mutation subset, performance trend | Release branch |
| Weekly | ITC'99, TMAX parity (if license available), long SAT/fuzz, memory profile, STIL round-trip | Milestone |
| Release | All of the above, full benchmark ladder, ATE format validation, signed report | Tag |

Determinism test: run the same flow twice with 1 thread and with N threads; pattern files and coverage reports must hash identically. If they do not, the merge order is wrong.

### 14.7 Metrics dashboard

Track per run: coverage (all denominators), pattern count, runtime by stage, peak RSS, DRC counts by rule, aborted faults, SAT proofs, and a determinism hash. Plot trends by design and by tool version. A performance regression greater than 10% on a benchmark blocks the release until explained.

---

## 15. Performance and scalability

### 15.1 Budgets

These are engineering targets to validate on real blocks, not claims.

| Workload | Design size | Faults | Threads | Runtime target | Memory target |
|---|---|---|---|---|---|
| ISCAS'85 | < 5 k gates | < 10 k | 1 | < 1 s | < 100 MB |
| G100 bring-up block | 50 k gates | 100 k | 16 | < 10 min | < 2 GB |
| G100 subsystem | 1 M gates | 1 M | 32 | < 2 h | < 8 GB |
| Full chip | 10 M gates | 10 M | 64 | < 12 h | < 32 GB |
| Pattern grading | 1 M gates × 100 k patterns | — | 64 | < 30 min | < 16 GB |

If a target is missed by more than 2×, profile before adding hardware. The usual culprits are per-fault allocation, pointer-chasing gate objects, and SAT calls on faults that structural ATPG could have solved.

### 15.2 Techniques

| Technique | Expected impact | Cost |
|---|---|---|
| Flat SoA arrays, 32-bit IDs | 2–5× faster than object graphs; cache-friendly | Up-front design discipline |
| 64-fault bit-parallel simulation | ~50× over scalar per fault | 2-plane encoding complexity |
| Event-driven evaluation with level queue | 2–10× on sparse activity | Queue and activity masks |
| Random-pattern pre-pass | Removes 60–80% of faults before search | Low |
| Fault collapsing | 3–5× fewer targets | Proof infrastructure |
| Structural ATPG before SAT | Avoids solver overhead on easy faults | Engine complexity |
| SAT only for hard faults | Bounds worst-case runtime | Solver integration |
| Deterministic fault partitioning | Scales to 64 cores | Merge logic |
| Checkpoint/restart | Saves long runs; enables debugging | Serialization |
| Compression-aware ATPG | 5–50× pattern reduction | Linear constraints, compactor model |

### 15.3 Measurement

- Microbenchmarks with Google Benchmark for gate eval, implication, fault insertion, and pattern merge.
- Whole-flow benchmarks with fixed profiles and pinned thread counts.
- `perf`/VTune (Linux) or ETW/WPA (Windows) for CPU profiles; Tracy for frame-level timeline if a GUI is added.
- Counters: gates evaluated per pattern, SAT calls per 1 k faults, average backtracks, conflict rate, cache misses per gate.
- Store benchmark results as JSON in CI; plot over time.

### 15.4 Scaling risks

| Risk | Symptom | Mitigation |
|---|---|---|
| SAT blowup on hard faults | A few faults consume most of the runtime | Per-fault budgets, portfolio of solvers, structural preprocessing |
| High-fanout nets | Implication and simulation hot spots | Fanout branch nodes, occurrence counts, partitioning |
| X-source explosion | Most patterns have many masks; coverage stalls | X-source inventory, bounding at source, mask optimization |
| Clock-domain complexity | Capture procedures multiply | Start single-clock; formalize procedures; limit simultaneous capture |
| Memory bandwidth | Bit-parallel planes saturate | Blocking/tiling, compressed fault words, NUMA-aware allocation |
| Non-determinism | Thread count changes results | Deterministic merge, stable IDs, hash tests |

### 15.5 Hardware

A 64-core x86 server with 512 GB RAM and NVMe local storage is the reference development and regression machine. A 16-core laptop with 64 GB RAM should handle blocks up to ~500 k gates. GPU is optional and phase 3+; do not buy GPUs for v1.

---

## 16. Chip bring-up plan: grx930 and G100

### 16.1 Common sequence

1. **Profile:** freeze netlist, libraries, SDC, scan spec, test pins, X sources, and fault model.
2. **Read and elaborate:** resolve black boxes, check undriven/multi-driven nets, compare cell counts with the design team's reference.
3. **Structural DRC before insertion:** catch loops, black boxes, and clock issues before adding scan logic.
4. **Scan replacement and stitching:** insert scan cells, build chains, add lockup latches, emit scan map.
5. **Post-insertion DRC:** full rule set; every waiver has an owner and expiry.
6. **Fault list:** generate, collapse, exclude with reasons, hash.
7. **ATPG:** random pass, structural, SAT closure; stop at budget or coverage target.
8. **Compaction and pattern export:** STIL first, then tester format.
9. **Replay:** gate-level simulation and ATE simulator; compare detection and timing.
10. **Sign-off:** coverage report, DRC report, pattern manifest, known limitations.

### 16.2 Block selection

Do not start with the top level. Select blocks by risk:

| Order | Block profile | Why |
|---|---|---|
| 1 | 20–50 k gates, single clock, no analog, few memories | Proves the flow end-to-end |
| 2 | 100–300 k gates, two clock domains, scan compression off | Proves multi-clock DRC and capture |
| 3 | 0.5–1 M gates, memories and black boxes, compression on | Proves X handling and scale |
| 4 | Full chip | Proves runtime, memory, and ATE integration |

### 16.3 grx930 profile

| Field | Value | Status |
|---|---|---|
| Top module | TBD | [INTAKE] |
| Netlist | TBD | [INTAKE] |
| Libraries | TBD | [INTAKE] |
| Clock domains | TBD | [INTAKE] |
| Scan style | mux-D (assumed) | [INTAKE] |
| Chain count / length | TBD | [INTAKE] |
| Test clocks | TBD | [INTAKE] |
| Compression | TBD | [INTAKE] |
| ATE / format | TBD | [INTAKE] |
| X sources | TBD | [INTAKE] |

### 16.4 G100 profile

| Field | Value | Status |
|---|---|---|
| Top module | TBD | [INTAKE] |
| Netlist | TBD | [INTAKE] |
| Libraries | TBD | [INTAKE] |
| Clock domains | TBD | [INTAKE] |
| Scan style | mux-D (assumed) | [INTAKE] |
| Chain count / length | TBD | [INTAKE] |
| Test clocks | TBD | [INTAKE] |
| Compression | TBD | [INTAKE] |
| ATE / format | TBD | [INTAKE] |
| X sources | TBD | [INTAKE] |

### 16.5 Acceptance per chip

- Full flow runs unattended from a clean checkout with one command.
- DRC is clean or every waiver is documented and unexpired.
- Testable stuck-at coverage meets the block target and is within 0.5 pt of TMAX on the parity blocks.
- Pattern count is within 1.10× TMAX; runtime is within the agreed budget.
- Patterns replay in gate-level simulation and are accepted by the ATE simulator.
- A parity ledger exists for every coverage or pattern-count difference.
- The run manifest reproduces the exact pattern set from the recorded inputs and seed.

### 16.6 Responsibilities

| Area | Owner | Deliverable |
|---|---|---|
| Netlist/library handoff | Design team | Frozen, tagged inputs |
| Scan methodology and chain spec | DFT team | Scan config and DRC waivers |
| ATPG engine and flow | Tools team | Tool release, reports |
| Pattern validation | Verification team | Replay and parity reports |
| ATE program and silicon | ATE team | Tester bring-up, failure logs |
| Sign-off | DFT lead | Signed coverage/DRC/pattern manifest |
---

## 17. Roadmap, staffing, and effort

### 17.1 Phase plan

| Phase | Duration | Deliverables | Exit gate |
|---|---|---|---|
| **P0 — Foundation** | Months 0–2 | Repo, build, design DB, Liberty/Verilog import, fault list, PODEM, scalar + bit-parallel simulation, CLI, CI | 100% coverage on ISCAS'85 c432/c499; deterministic hashes; no sanitizer failures |
| **P1 — Vertical slice** | Months 3–5 | Scan replacement, stitching, DRC engine, full-scan stuck-at, STIL writer, G100 50 k-gate block | ≥ 95% testable coverage; DRC clean/waived; patterns replay in gate-level sim |
| **P2 — Production stuck-at** | Months 6–9 | Multi-clock capture, compression baseline, TMAX parity on two blocks, 1 M-gate subsystem | ≥ 99% testable coverage; pattern count ≤ 1.10× TMAX; runtime ≤ 1.5× |
| **P3 — Transition and diagnosis** | Months 10–15 | LOC/LOS transition ATPG, two-timeframe model, chain diagnosis, physical-aware stitching | Transition coverage target; diagnosis accuracy on injected failures |
| **P4 — Scale and AI** | Months 16–24 | Full-chip runs, second chip, learned heuristics in shadow mode, ATE correlation | Production sign-off on grx930 and G100 |

Phase gates are hard. A phase does not start because the calendar says so; it starts when the previous exit gate is met or explicitly waived by the DFT lead with a written risk acceptance.

### 17.2 Staffing

| Role | FTE | Focus |
|---|---|---|
| Architect / tech lead | 1 | Data model, ATPG architecture, reviews, parity strategy |
| ATPG core engineers | 3 | Implication, PODEM/FAN, SAT integration, compaction |
| DFT/scan engineers | 2 | Insertion, stitching, DRC, chip profiles |
| Verification engineers | 2 | Benchmarks, differential testing, replay, CI |
| Front end / infrastructure | 1 | Yosys/Liberty import, build, performance tooling |
| ML / data engineer | 1 | Features, models, shadow evaluation (from phase 2) |
| ATE liaison | 0.5 | Tester formats, vendor simulator, silicon bring-up |
| Project manager | 0.5 | Schedule, dependencies, risk log |
| **Total** | **11** | |

Ramp-up: start with 6 (architect, 2 ATPG, 1 DFT, 1 verification, 1 front end), add the rest at phase 2. Hiring senior ATPG talent is the schedule-critical path; do not assume it can be hired on demand.

### 17.3 Effort and build-vs-buy

A TMAX-class tool is a multi-year, multi-team effort. This plan delivers a **focused full-scan stuck-at subset in ~24 months with 11 FTE (~22 engineer-years)**. Transition delay, diagnosis, and compression maturity add an estimated **8–12 engineer-years**. Full feature parity with TetraMAX — including all fault models, compression IP, diagnosis, and tester integrations — is not a 24-month deliverable and should not be promised as one.

| Condition | Recommendation |
|---|---|
| Only two chips, no follow-on roadmap, TMAX licenses available | License; build a thin automation layer instead |
| Multi-chip roadmap, test cost is strategic, tool control matters | Build the focused subset; keep TMAX as the parity oracle |
| Need transition/diagnosis immediately | License now, build stuck-at in parallel, revisit after P2 |

The build case rests on more than license cost: control of the flow, ability to tune heuristics for your designs, and avoiding per-seat scaling. The buy case rests on time-to-silicon and access to proven compression/diagnosis IP.

### 17.4 Milestones

| Milestone | Date | Demo |
|---|---|---|
| M0 | Month 0 | Team staffed, repo, decision on build-vs-buy |
| M1 | Month 2 | ISCAS'85 stuck-at ATPG and simulation from CLI |
| M2 | Month 5 | G100 50 k-gate block: insert, DRC, ATPG, STIL, replay |
| M3 | Month 9 | TMAX parity report on two blocks; 1 M-gate subsystem |
| M4 | Month 15 | Transition ATPG and diagnosis demo |
| M5 | Month 24 | Production sign-off package for grx930 and G100 |

---

## 18. Risks and mitigations

| Risk | Likelihood | Impact | Mitigation | Early warning |
|---|---|---|---|---|
| X handling is harder than expected | High | High | Inventory X sources early; bound at source; conservative masks; coverage accounting separates masked from untestable | Aborted faults > 10%; mask overhead > 20% |
| Multi-clock capture semantics | High | High | Start single-clock; formalize capture procedures with DFT; DRC rules for domain crossings | DFT003/DFT019 spikes; replay mismatches |
| SAT blowup on hard faults | Medium | High | Per-fault budgets; structural fallback; solver portfolio; prove untestability only when solver says UNSAT | A few faults dominate runtime |
| Pattern count misses parity | Medium | High | Invest in compaction early; compression baseline at P2; run TMAX diff from first block | Pattern count > 1.2× TMAX at P1 |
| Scan DRC escapes | Medium | High | Mutation tests per rule; golden netlists; independent review of waivers | Silicon failure traced to unscanned cell |
| Library scan-cell gaps | Medium | Medium | Audit libraries at intake; manual models for exotic cells; flag unsupported cells | Missing `test_cell`; function mismatch |
| Netlist handoff delays | High | Medium | Freeze dates; mock netlists; import blocks as they land | Profile fields still TBD at M1 |
| Scope creep into BIST/analog/transition | High | Medium | Written non-goals; phase gates; change control | New feature requests before P2 gate |
| Determinism bugs | Medium | Medium | Stable IDs; deterministic merge; 1-thread vs N-thread hash tests | Different pattern hashes across runs |
| ATE format mismatch | Medium | Medium | Early vendor simulator; STIL round-trip; ATE liaison from P1 | Vendor warnings unresolved at P2 |
| AI overreach | Low | High | Hard boundary; shadow mode; deterministic fallback; no LLM in loop | Model recommendation accepted without simulation |
| Licensing/legal | Low | Medium | Legal review of Yosys/ABC/CaDiCaL/Tcl and any compression IP | Unclear license in dependency scan |
| Key-person loss | Medium | High | Pairing, design docs, code ownership rotation | Single owner on ATPG or scan |
| Performance regression | Medium | Medium | CI benchmarks; 10% gate; profile before optimization | Nightly trend slope |

The risk log is reviewed monthly. Each risk has an owner, a trigger, and a mitigation that is funded, not just written down.

---

## 19. Interfaces, command language, and repository

### 19.1 Tcl command surface

The names below are ours; TMAX-compatible aliases are added by a migration layer, not by renaming the core API.

```tcl
read_liberty -lib libs/stdcell.lib
read_liberty -lib libs/scan.lib
read_verilog netlists/g100_block.v
read_sdc constraints/g100_functional.sdc
elaborate -top g100_block
check_design

insert_scan -config profiles/g100_scan.yaml
run_drc -report reports/drc.json
create_fault_list -model stuck_at
run_atpg -random 2000 -backtracks 200 -sat_budget 10s -threads 32

report_coverage -by domain -by chain
write_patterns -stil patterns/g100.stil
write_scan_map reports/scan_map.json
write_faults reports/g100.faults
```

Design rules for the command layer:

- every command returns a structured result and sets a Tcl error code on failure;
- destructive commands require an explicit output path and never overwrite by default;
- all options are recorded in the run manifest;
- `-help` is generated from the same schema as the parser, so it cannot drift.

### 19.2 Python API

```python
import gmax

db = gmax.read_verilog("netlists/g100_block.v")
gmax.read_liberty(db, ["libs/stdcell.lib", "libs/scan.lib"])
gmax.read_sdc(db, "constraints/g100_functional.sdc")
gmax.elaborate(db, top="g100_block")

scan = gmax.insert_scan(db, config="profiles/g100_scan.yaml")
drc = gmax.run_drc(db)
faults = gmax.create_fault_list(db, model="stuck_at")
result = gmax.run_atpg(db, faults, threads=32, sat_budget="10s")

print(result.coverage.testable)
gmax.write_patterns(result, "patterns/g100.stil", format="stil")
```

The Python API is a thin binding over the C++ API. It is for automation and analysis, not a second implementation of the flow.

### 19.3 Formats and exit codes

| Format | Purpose | Versioning |
|---|---|---|
| `.dfdb` | Design database snapshot | Major/minor schema; CRC per section |
| `.faults` | Fault list with states and proof metadata | Header with netlist/fault-list hash |
| `.patterns` / `.stil` | Pattern IR and STIL export | Pattern IR version in manifest |
| `.json` | DRC, coverage, run manifest | Schema version field |
| `.v` | Netlist export for debug/equivalence | Generated, not hand-edited |

Exit codes: `0` success, `1` usage error, `2` design/import error, `3` DRC error, `4` ATPG incomplete (coverage or aborted faults above threshold), `5` internal error. CI treats `4` as a failure unless the profile explicitly allows it.

### 19.4 Repository layout

```
gmax/
  CMakeLists.txt
  cmake/
  src/
    core/
      db/            # hierarchy, flat graph, serialization
      frontend/      # Verilog/Liberty/SDC import
      dft/           # scan replacement, stitching, DRC
      atpg/          # fault list, PODEM, FAN, SAT, compaction
      sim/           # bit-parallel fault simulation
      compress/      # XOR/Illinois/EDT constraints
      pattern/       # pattern IR, STIL/WGL writers
      report/        # coverage, DRC, manifests
    shell/
      tcl/           # Tcl commands
      python/        # pybind11 module
  third_party/       # pinned Yosys/ABC/CaDiCaL/MiniSat
  tests/
    unit/
    property/
    differential/
    benchmarks/      # ISCAS, ITC, internal
  profiles/
    grx930/
    g100/
  docs/
  tools/             # dashboards, pattern viewer, CI scripts
```

### 19.5 Build and test

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
./build/gmax --version
```

Dependencies are vendored or pinned by commit hash; no floating `latest`. A clean build from a fresh checkout must succeed offline after the dependency cache is populated.

### 19.6 Versioning

Semantic versioning for the tool; independent schema versions for `.dfdb`, fault lists, pattern IR, and reports. A tool release declares the schema versions it reads and writes. Breaking schema changes require a converter and a migration test on a frozen design.
---

## 20. Open decisions and next steps

### 20.1 Decisions required

| Decision | Owner | Needed by | Options | Recommendation |
|---|---|---|---|---|
| Build vs. buy | Executive + DFT lead | M0 | Build focused subset; license TMAX; hybrid | Decide before staffing; do not drift into both |
| ATE platform and pattern format | ATE lead | M1 | Advantest V93000; Teradyne UltraFLEX; other | STIL first; vendor format after platform is fixed |
| Compression IP | DFT + tools | M3 | In-house XOR/Illinois; licensed EDT | Start in-house for stuck-at; revisit for transition |
| Scan style | DFT | M1 | mux-D; LSSD; hybrid | mux-D for v1 |
| Chain count and length | DFT | M1 | Based on pins, test time, and skew | Set from ATE pin budget and target test time |
| Test clock and capture procedure | DFT | M1 | Simultaneous; sequential; grouped | Start single-clock, then grouped |
| X-source policy | DFT | M2 | Bound at source; mask at observation; both | Bound at source where possible, mask the rest |
| TMAX version and scripts | DFT | M1 | Current production version | Freeze for parity runs |
| Physical-aware stitching | DFT | M6 | Wirelength-aware ordering | Phase 3, after logical flow is stable |
| Transition/diagnosis scope | Architecture | M9 | LOC/LOS; path delay; chain diagnosis | LOC/LOS + chain diagnosis first |
| AI pilot | Tools/ML | M12 | Difficulty classifier; backtrace ranking | Difficulty classifier in shadow mode |
| Export control / legal | Legal | M0 | Internal use; external distribution | Review before any external sharing |

### 20.2 Next 30 days

1. Confirm the **G100 bring-up block** and freeze its netlist, libraries, SDC, and scan spec.
2. Make the build-vs-buy decision and staff the first six roles.
3. Stand up the repository, CMake build, CI, and the `.dfdb` skeleton from this document.
4. Implement Liberty and Verilog import, the flat graph, and ISCAS'85 c432/c499.
5. Implement fault list, PODEM, and scalar fault simulation; reach 100% coverage on c432.
6. Write ADRs for SAT integration, pattern IR, and deterministic threading.
7. Schedule the first TMAX parity run with the DFT team; agree on the comparison ledger.
8. Add the DRC rule table to the issue tracker, one issue per rule, with mutation tests.

The next 30 days are successful if c432 runs end-to-end from the CLI, twice, with byte-identical output, and the G100 block profile is frozen.

---

## 21. TMAX capability map

Commercial capability descriptions vary by TetraMAX version and license; treat this as a planning map, not a feature-for-feature claim.

| Capability | Commercial baseline | Our P1 | Our P2 | Our P3+ | Notes |
|---|---|---|---|---|---|
| Verilog/Liberty read | Yes | Yes | Production hardening | — | Yosys front end; Liberty subset |
| Hierarchical netlist | Yes | Yes | Yes | — | One DB, two views |
| Scan insertion/stitching | Yes | Yes | Multi-clock | Physical-aware | mux-D first |
| Scan DRC | Yes | Yes | Golden/mutation set | — | Rule IDs and waivers |
| Stuck-at ATPG | Yes | Yes | TMAX parity | — | PODEM/FAN + SAT |
| Fault simulation | Yes | Yes | PPSFP | GPU experiment | CPU reference |
| Compaction | Yes | Basic | Advanced | — | Dynamic + static |
| Test compression | Yes | No | Illinois/XOR baseline | Compression-aware | Licensed IP optional |
| Transition delay ATPG | Yes | No | No | LOC/LOS | Phase 3 |
| Path delay ATPG | Yes | No | No | Later | Not in 24-month scope |
| Diagnosis | Yes | No | Basic chain diagnosis | Advanced | Phase 3 |
| STIL | Yes | Yes | Yes | — | Round-trip validated |
| WGL | Yes | No | Yes | — | Same IR |
| Tcl command surface | Yes | Yes | TMAX aliases | — | Migration layer |
| Multi-clock capture | Yes | Limited | Yes | — | Grouped capture |
| UPF/power-aware | Yes | No | No | TBD | Out of scope v1 |
| Formal/equivalence | Partial | Yosys/ABC | Yosys/ABC | — | Not a differentiator |
| Tester interfaces | Yes | STIL | Vendor format | — | ATE team owns program |
| GUI | Yes | CLI | CLI + reports | Optional | Do not build a GUI early |

The strategic gap is not a missing checkbox; it is **pattern quality and runtime on your real blocks**. The parity ledger, not the feature table, decides whether the tool is ready.

---

## Appendix A — Glossary

| Term | Meaning |
|---|---|
| ATPG | Automatic Test Pattern Generation |
| BIST | Built-In Self-Test |
| CTL | Core Test Language (IEEE 1450.6) |
| DRC | Design Rule Check (here, scan/testability rules) |
| EDT | Embedded Deterministic Test (XOR-based compression) |
| FAN | Fanout-oriented test generation algorithm |
| FFR | Fanout-Free Region |
| LOC | Launch-Off-Capture (transition test) |
| LOS | Launch-Off-Shift (transition test) |
| ODC | Observability Don't-Care |
| PODEM | Path-Oriented Decision Making |
| SAT | Boolean Satisfiability |
| SCOAP | Sandia Controllability/Observability Analysis Program |
| SPF | STIL Procedure File |
| STIL | Standard Test Interface Language (IEEE 1450) |
| TCD | Test Cycle Description (tester-specific) |
| WGL | Waveform Generation Language |
| X | Unknown logic value |
| X-mask | A mask that prevents an unknown value from corrupting observation |
| Scan chain | Shift register of scan cells |
| Lockup latch | Latch inserted to tolerate clock skew between chain segments |
| mux-D | Scan style that inserts a mux before the flop D input |
| Capture | Clock pulse that captures the combinational response |
| Load/unload | Shift-in/shift-out phases of a scan pattern |
| Checkpoint fault | Fault generated at selected gate outputs/boundaries before collapsing |
| Testable coverage | Detected ÷ (detected + undetected + aborted), excluding proven untestable |
| Abort | Search budget exhausted without a pattern or proof |

---

## Appendix B — References and further reading

1. P. Goel, "An Implicit Enumeration Algorithm to Generate Tests for Combinational Logic Circuits," *IEEE Transactions on Computers*, 1981. (PODEM)
2. H. Fujiwara and T. Shimono, "On the Acceleration of Test Generation Algorithms," *IEEE Transactions on Computers*, 1983. (FAN)
3. T. Larrabee, "Test Pattern Generation Using Boolean Satisfiability," *IEEE Transactions on Computer-Aided Design*, 1992.
4. J. P. Marques-Silva and K. A. Sakallah, "GRASP: A Search Algorithm for Propositional Satisfiability," *IEEE Transactions on Computers*, 1999.
5. A. Biere, M. Heule, H. van Maaren, and T. Walsh (eds.), *Handbook of Satisfiability*, 2nd ed., IOS Press, 2021.
6. L. Goldstein, "Controllability/Observability Analysis of Digital Circuits," *IEEE Transactions on Circuits and Systems*, 1979. (SCOAP)
7. I. Hamzaoglu and J. H. Patel, "Reducing Test Application Time Through Test Data Compression and Illinois Scan Architecture," 1999.
8. J. Rajski et al., "Embedded Deterministic Test for Low Cost Manufacturing Test," 2002.
9. IEEE 1450, *Standard Test Interface Language (STIL) for Digital Test Vector Data*.
10. IEEE 1450.6, *Core Test Language (CTL)*.
11. Yosys documentation, https://yosyshq.net/yosys/.
12. ABC: System for Sequential Logic Synthesis and Formal Verification, https://github.com/berkeley-abc/abc.
13. CaDiCaL SAT Solver, https://github.com/arminbiere/cadical.
14. MiniSat, http://minisat.se/.
15. Fault: an open-source DFT toolchain, https://fault.readthedocs.io/.
16. Atalanta ATPG (academic reference implementation) and Hope fault simulator; useful for differential testing on ISCAS circuits.
17. "PastATPG: A Hybrid ATPG Framework for Better Test Compaction with Partial Assignment SAT," DAC 2025. (Research direction for X-aware SAT patterns.)

---

## Appendix C — TBD register

| ID | Item | Owner | Needed by | Blocks |
|---|---|---|---|---|
| TBD-01 | grx930 top netlist and libraries | Design | M1 | grx930 profile |
| TBD-02 | G100 top netlist and libraries | Design | M1 | G100 profile |
| TBD-03 | Scan spec (chains, clocks, SE) | DFT | M1 | Insertion |
| TBD-04 | ATE platform and pin map | ATE | M1 | Pattern writer |
| TBD-05 | Compression IP decision | DFT/tools | M3 | P2 compression |
| TBD-06 | Test clock/capture procedure | DFT | M1 | Multi-clock ATPG |
| TBD-07 | X-source inventory | DFT | M2 | Coverage/masks |
| TBD-08 | TMAX version and golden runs | DFT | M1 | Parity |
| TBD-09 | Build-vs-buy decision | Executive | M0 | Staffing |
| TBD-10 | First G100 bring-up block | DFT | M0 | P1 |
| TBD-11 | Legal/license review | Legal | M0 | Distribution |
| TBD-12 | Physical data (DEF/LEF) | Design | M6 | Physical-aware stitching |

---

*End of document. This is a living architecture: update the ADRs and parity ledger as measurements replace assumptions.*
