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

