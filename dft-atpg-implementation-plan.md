# Implementation Plan — DFT/ATPG Platform (grx930 & G100)

**Status:** Draft v0.6 for review — supersedes v0.5, which clarified architecture 6.4's MUX2 select-X rule.
**Date:** 2026-11-24
**Revision:** v0.6, 2026-11-24 — G0.2 evidence now names the pinned Yosys and MiniSat 2.2.1 cross-check and the separate `sat-gate` CI job; the default ctest matrix remains skip-friendly. No scope, package, gate, effort or capacity figure changed.
**Owner:** Architect / tech lead (plan), DFT lead (gates), PM (tracking).
**Baseline:** `dft-atpg-platform-architecture-grx930-g100.md` at blob `d9ae07254b87e5b3b5e2b41672613ac8fee340c9`, the MUX2-clarification revision, landed in the same commit as plan v0.5.
**Companion documents:** `transition-parity-protocol-p2.md` (P2 transition protocol, M9 pattern baseline), `diagnosis-benchmark-p3.md` (`D-DIAG-01` / `D-DIAG-RESULT-01`).

---

## 0. Purpose and standing of this document

This plan turns the architecture into sequenced work packages with owners, dependencies, estimates, and gate evidence. It does **not** restate the architecture and it does not replace it.

Rules of precedence:

- Where this plan and the architecture disagree, the **architecture wins** and this plan is corrected.
- The plan may **not** add scope to v1. Anything not in the architecture is not in this plan; new scope needs an architecture change and a new plan revision.
- The plan may make execution choices the architecture leaves open (sequencing, team split, which package is built first). Those are listed in section 15, not buried in the phase tables.

Phase 5 (section 8) is scheduled in this document but sits outside v1 and outside the 24-month baseline. Sequencing it here makes the work visible; it does not authorise it to start. Its scope enters the architecture first, under the revision in 8.1, and no package in it may begin before that revision is recorded.

Estimate accuracy is **±30%** at this stage (AACE class 4). Estimates are engineer-weeks (ew) of direct work, excluding holidays, reviews, and gate administration. They are not dates. The four largest and least precedent-bound estimates are pressure-tested in Appendix C.

---

## 1. Planning model

### 1.1 Work packages

Every package has an ID, a single accountable owner role, a definition of done, and at least one gate criterion it feeds. If a package cannot name the gate criterion it serves, it is cut.

| Code | Role (architecture 17.2) |
|---|---|
| ARCH | Architect / tech lead |
| ATPG | ATPG core engineers |
| DFT | DFT/scan engineers |
| VER | Verification engineers |
| FE | Front end / infrastructure |
| ML | ML / data engineer |
| ATE | ATE liaison |
| PM | Project manager |

Package states: `planned` → `active` → `merged` → `gate-evidenced`. A package is not complete because code merged; it is complete when its gate evidence exists and has been reviewed.

### 1.2 Capacity basis

Capacity is FTE × working weeks for the phase, from the architecture's staffing table. The plan does **not** re-derive headcount. Role mix is checked per phase, not just phase totals; section 12.1 records the audit and what it changed.

| Phase | Months | Weeks | FTE | Capacity (ew) | Planned effort (ew) | Load |
|---|---|---|---|---|---|---|
| P0 — Foundation | 0–2 | 10 | 6 | 60 | 58 | 97% |
| P1 — Vertical slice | 3–5 | 13 | 8 | 104 | 93 | 89% |
| P2 — Production stuck-at | 6–9 | 17 | 11 | 187 | 166 | 89% |
| P3 — Transition and diagnosis | 10–15 | 26 | 11 | 286 | 186 | 65% |
| P4 — Scale and AI | 16–24 | 39 | 11 | 429 | 314 | 73% |
| **Baseline total (M0–M24)** | **0–24** | **105** | — | **1066** | **817** | **77%** |
| P5 — Boundary scan and memory BIST | 25–33 | 39 | 6.0 | 234 | 142 | 61% |
| **Programme total (M0–M33)** | **0–33** | **144** | — | **1300** | **959** | **74%** |

The 249 ew difference is **phase reserve**, not unallocated staff. It is spent on parity and accuracy re-runs, gate shortfalls, and defect work arriving from real blocks. A parity or accuracy re-run costs one to three weeks of the whole team and produces no code, which is why P3 and P4 show the most reserve. Phase 5's 92 ew reserve sits inside its own 234 ew line and is not available to P0–P4.

The reserve is **not** funding for the 8–12 engineer-year maturity gap the architecture names in section 17.3. Path delay, cell-aware, and compression IP maturity remain outside this plan (Appendix A).

### 1.3 Phase gate protocol

Phase gates are hard (architecture 17.1). A phase starts when the previous gate is **met**, or **waived in writing** with a named risk acceptance, approved by the authority section 15 names for that class — the architecture owner for a narrowed criterion or a late capability, the PM for a release limitation. The DFT lead chairs the review and signs the decision; chairing a gate is not approval authority over an architecture-scope gap. "Calendar says so" is not an entry condition.

Each gate review produces a gate pack:

1. The gate criterion table for that phase (section 10) marked met / not met / waived.
2. The evidence artifacts, by path and hash.
3. Open defects with severity and owner.
4. Parity ledger state, if the phase has one.
5. A one-page decision: **go / go-with-waivers / stop**, signed by DFT lead, architect, and verification lead — and by the PM at the release gate, where the production sign-off sits.
6. Where a criterion names a set, the set as it stood at the gate. G1.2's rule list has no other authoritative record, so the pack carries it, with each removal attributed to the contingency that made it and the approval that allowed it. The parity block sets in G2.4, G3.1 and G4.5 need the same treatment for a different reason: the ledger is indexed by difference, so a block with no differences leaves no entry, and the ledger alone cannot prove that three blocks were compared. The pack carries the block list with each block's profile hash. The waiver sets behind every criterion that cites a waiver audit — G1.2, G2.9, G4.1 and G4.3 — do have one, the waiver file, so the pack carries the file hash, its count and the audit result: every entry with an owner and an expiry, none expired. Engineering waivers take no separate approver by design (section 15); pinning the hash is what makes the file's contents the gate's evidence rather than a later reconstruction. Criteria whose set is already pinned by hash — G3.3's corpus, G4.6's manifest, G1.1's fault list — need only that pinned artifact, which item 2 already carries.

A gate waiver without an owner, a reason, an expiry date, a written risk acceptance, and an approver under section 15 is not a waiver.

### 1.4 Definition of done

For code packages:

- merged behind the commit gate (build debug/release, unit tests, sanitizer smoke, format/lint, determinism hash test);
- unit tests cover the new logic's failure modes, not just its happy path;
- no new sanitizer findings, no new compiler warnings under `-Werror`;
- determinism: identical hashes for 1 thread and N threads on the affected flow;
- the owning ADR or user-facing doc updated;
- reviewed by the component owner, not the author alone.

For benchmark and analysis packages: frozen inputs with hashes, a manifest, and a ledger entry for every difference that is not explained by the tool.

For documents and records: reviewed by the named approvers, linked from the architecture or the companion doc, and revision-stamped.

---

## 2. Critical path

```
build/CI → DB → {Liberty, Yosys} → fault list → {PODEM, fault sim} → SAT smoke → c432/c499        [G0]
  → scan replacement → stitching → DRC → full-scan ATPG → compaction → STIL → replay → 50 k block  [G1]
  → grouped capture → compression → parity harness → 3-block parity → 1 M scale                    [G2]
  → two-timeframe transition → transition parity → chain diagnosis → diagnosis benchmark           [G3]
  → full chip → runtime parity → second chip → shadow AI → silicon correlation → release qualification [G4]
  → boundary scan → memory BIST → BSDL and BIST vectors                                   [G5]
```

The chain is not parallelizable at its ends. Two consequences drive the schedule:

- **P1 cannot be compressed by adding people.** Scan replacement, stitching, and DRC share one design database and one legality model. Past four engineers the coordination cost exceeds the gain.
- **P2's parity runs cannot start before the harness exists.** The harness is therefore built during P1 (overlap 2.1 below).

### 2.1 Deliberate cross-phase overlaps

Each overlap is a start date, not a second cost: the early work is drawn from the host phase's reserve (P1 has 17 ew, P2 has 25 ew), and the package stays costed in the phase that owns it.

| # | Overlap | Why it is worth the risk |
|---|---|---|
| 1 | Parity harness (WP-204) starts at M4, inside P1 | The first TMAX diff must not be blocked by tooling at M6. The architecture flags pattern count > 1.2× at P1 as an early warning; that warning is only observable if the harness exists. |
| 2 | DRC rules filed as 20 tracker issues at M1, each with its mutation test (architecture 20.2 item 8) | WP-103 then implements against existing tests instead of inventing all 20 at once. Mutation scaffolding lands incrementally. |
| 3 | Compression decision (TBD-05) pulled to M3, not M6 | The chain-count and chain-length targets differ once compression is on. Deciding at M3 avoids re-stitching at P2. |
| 4 | Diagnosis corpus plumbing (WP-310) starts at M8 | `D-DIAG-01` is an **M9** deliverable even though P3 begins at M10. The record must be frozen before P3 ATPG starts; it cannot be produced after the fact. |

### 2.2 Schedule-critical external inputs

These are not engineering work and cannot be recovered by working harder: **G100 bring-up block freeze**, **TMAX version/license/scripts**, **libraries with `test_cell`**, **scan spec**, **SDC clock intent**. Section 13 maps each to the packages it blocks.

---

## 3. Phase 0 — Foundation (M0–M2)

**Architecture exit gate:** 100% coverage on ISCAS'85 c432/c499; SAT smoke test passes; deterministic hashes; no sanitizer failures.
**Expanded gate:** G0.1–G0.7 (section 10). **Demo:** M1.

| ID | Work package | Deliverables | ew | Depends on | Owner |
|---|---|---|---|---|---|
| WP-001 | Build, dependencies, CI skeleton | CMake presets, pinned `third_party` refs by commit, `-Wall -Wextra -Werror`, ASan/UBSan job, clang-format, ctest wiring, offline clean-build check | 4 | — | FE |
| WP-002 | Design DB v0 and `.dfdb` | Hierarchical + flat SoA graph (architecture 6.3), stable 32-bit IDs, string table, section table with CRC32, minor-version policy, sorted-key serialization | 6 | WP-001 | ARCH |
| WP-003 | Liberty subset import | `cell`/`pin`/`function`/`ff`/`latch`/`statetable`/`test_cell` parsing and primitive decomposition; the exhaustive function check for ≤ 6-input cells and scan-cell detection with an acknowledgement warning move to WP-101 | 4 | WP-002 | DFT |
| WP-004 | Verilog front end via Yosys | Pinned recipe, JSON ingest, structured diagnostics (unresolved module, multiple drivers, undriven input, combinational loop, width mismatch), cell-count reconciliation against the reference report | 6 | WP-002 | FE |
| WP-005 | Four-state evaluation | Scalar reference evaluator, 64-lane `known`/`value` planes, X-correct gate formulas, exhaustive differential unit tests against the scalar evaluator | 4 | WP-002 | VER |
| WP-006 | Fault list and collapsing | Checkpoint generation (outputs, fanout branches, PIs/POs, scan boundaries), deterministic ordering (net ID, SA0 before SA1), structural equivalence collapse, `.faults` writer with netlist and fault-list header hashes | 5 | WP-002, WP-004 | DFT |
| WP-007 | PODEM | Five-valued algebra, implication with occurrence counts, X-path check over controlled gates and X sources, multi-objective backtrace, chronological backtracking, per-fault budgets (`max_backtracks`, `max_time_ms`) | 10 | WP-005, WP-006 | ATPG |
| WP-008 | SAT smoke test | `ISatSolver` boundary, CaDiCaL, two-copy miter with Tseitin clauses, detection clause over observables, UNSAT metadata, MiniSat cross-check on ≤ 1 k-gate instances | 4 | WP-006 | ATPG |
| WP-009 | Fault simulation core | Scalar reference simulator, 64-fault/word parallel-fault with event queue and fault dropping, agreement test against the scalar simulator | 6 | WP-005 | ATPG |
| WP-010 | CLI and Tcl v0 | Command schema with generated `-help`, exit codes 0–5, `--version`; the run manifest moves to WP-011 | 2 | WP-001 | ARCH |
| WP-011 | ISCAS'85 bring-up | c432/c499 ingest, unattended end-to-end run, coverage report with denominators and fault-list hash, 1-thread vs N-thread hash equality, run manifest (tool version, profile/lib/netlist/seed/stage hashes) moved from WP-010 | 5 | WP-007–WP-010 | VER |
| WP-012 | ADRs 009–011 | SAT integration, pattern IR, deterministic threading; reviewed and accepted | 2 | — | ARCH |

**Phase total: 58 ew.**

**Deliberately out of P0:** PPSFP, FAN, dynamic compaction, STIL, full equivalence/dominance collapsing with SAT proofs, ISCAS'89. Each moves to P1 or P2 and is listed there. P0 runs at 58/60 (97%); anything added must still displace something.

**Ownership follows competency, not headcount.** Liberty/`test_cell` handling and fault-list generation are DFT intake work; the scalar four-state model is verification's independent oracle for the vectorized engine; the command schema is an architectural contract. Section 12.1 is what forced those assignments to be explicit.

**P0 risks**

- *Build environment drift.* The first week is spent on CMake and dependency pinning, not on ATPG. If the 30-day goal (c432 twice, byte-identical) slips, it slips here first.
- *Yosys JSON size.* Even on c432, decide the ingest shape now; full-chip JSON is a P2 problem with a P2 fix (pass plugin, ADR-002).
- *PODEM X-path optimism.* WP-007 at 10 ew covers the algorithm, not the production hardening. Budget the first replay mismatches into WP-011, not into a new package.

---

## 4. Phase 1 — Vertical slice (M3–M5)

**Architecture exit gate:** ≥ 95% testable coverage on the bring-up block; DRC clean or waived; patterns replay in gate-level simulation. **Expanded gate:** G1.1–G1.8. **Demo:** M2.

| ID | Work package | Deliverables | ew | Depends on | Owner |
|---|---|---|---|---|---|
| WP-101 | Scan replacement | mux-D insertion from `test_cell`, dedicated scan-flop path when the library provides one, preservation of data/clock/enable/async behavior, functional-mode equivalence proof (ABC `cec` or Yosys `equiv_make` + `equiv_simple`), plus Liberty scan-cell detection with an acknowledgement warning when `test_cell` is absent and the exhaustive function check for ≤ 6-input cells, both moved from WP-003 | 12 | G0 | DFT |
| WP-102 | Chain stitching | Clock/power-domain partition, target-count allocation, first-fit-decreasing packing, lockup insertion at crossings, scan map plus a report recording the cost function and every tie-break | 9 | WP-101 | DFT |
| WP-103 | DRC engine and rule set | DFT001–DFT020, union-find domains, Tarjan SCC, X-reachability, severity and auto-fix, JSON + table report, waivers with owner/reason/expiry checked at load so an expired waiver fails the report, one mutation test per rule | 19 | WP-101 | DFT |
| WP-104 | Full-scan ATPG semantics | PPI/PPO mapping, scan-frame model, load/capture/unload/compare, capture-frame observation set for detection | 8 | WP-102, WP-007 | ATPG |
| WP-105 | Compaction v1 | Dynamic compaction objectives, greedy static merge, X-fill for mergeability, re-grade after every merge or fill | 9 | WP-104 | ATPG |
| WP-106 | Pattern IR and STIL | Canonical IR (signals, timing sets, cycles, frames, masks, annotations), STIL 1.1 writer, STIL/SPF reader, round-trip structural equality, fuzz targets for the Verilog, Liberty and STIL readers with seed corpora | 11 | WP-104 | FE |
| WP-107 | Replay and detection match | Gate-level replay of written patterns, recorded-vs-replayed detection comparison; mismatch localization moves to WP-313 | 5 | WP-106, WP-009 | VER |
| WP-109 | SAT closure | Solver-reuse benchmark (solver copy / activation literals / re-encode), proof metadata, timeout → `ABORTED`, constraint units for test mode and masks | 7 | WP-104 | ATPG |
| WP-110 | SDC subset and test mode | Restricted Tcl SDC commands, clock domains and generated clocks, case analysis, false paths for capture, precise warnings for unsupported commands | 6 | WP-002 | ARCH |
| WP-111 | G100 50 k bring-up | Frozen profile, unattended full flow, coverage/DRC/pattern reports, debug log; falls back to any qualified public block if intake slips | 7 | WP-101–WP-110 | VER |

**Phase total: 93 ew.**

**Notes**

- WP-111 is the only package that may re-point to a public block. The gate then moves to rung 4 of the benchmark ladder, and the M2 demo is labelled as such.
- **Why FAN and WGL were moved out of P1.** The FAN package's entry condition is measured PODEM backtracking on a real block (architecture 9.4), and the only real block in P1 is WP-111, the last package — it was sequenced before its own measurement. The WGL writer was also mis-placed: architecture 21 lists WGL as a P2 capability and G1.5 requires only STIL. Both are now P2 packages, WP-214 and WP-215.
- WP-111 is VER-led: end-to-end bring-up is integration verification; DFT owns the profile, the scan map and the DRC waivers.
- **G1.7's TMAX run is a DFT activity, not a tool package.** TBD-08 provides the frozen TMAX version and scripts at M4; DFT executes the 50 k-block run and the results are recorded in the ledger that WP-204 opens. WP-205 industrialises the same flow across three blocks in P2.
- **G1.5's vendor-simulator run needs the ATE liaison before the roster has one.** ATE joins at 0.5 FTE in P2; for the P1 STIL acceptance the liaison is engaged on call-off for the simulator session, which is days rather than engineer-weeks and is not costed as a package.
- **G1.2 blocks on expired waivers at P1.** WP-103 checks expiry when a waiver is loaded, so an expired waiver fails the report instead of waiting for a reviewer to notice it. WP-208 industrialises the workflow in P2 and WP-415 audits the accumulated set at G4. The check costs 1 ew; the local swap pass moves from WP-102 to WP-311, where wirelength data exists and the optimisation belongs. DFT lands at 40/39 (103%), and the six-rule contingency in Appendix C remains the release valve if the phase runs over.

---

## 5. Phase 2 — Production stuck-at (M6–M9)

**Architecture exit gate:** ≥ 99.5% testable coverage; pattern count ≤ 1.10× TMAX; runtime ≤ 1.5×; 1 M-gate subsystem; parity on at least three blocks. **Expanded gate:** G2.1–G2.10. **Demo:** M3.

| ID | Work package | Deliverables | ew | Depends on | Owner |
|---|---|---|---|---|---|
| WP-201 | Multi-clock capture | Capture-procedure model (simultaneous / grouped / sequential), intermediate-state scanned-or-masked rule, per-group timing sets, DRC hooks for DFT003/DFT019 | 12 | WP-110, WP-104 | DFT |
| WP-202 | Compression baseline | Illinois scan first, then XOR decompressor/compactor; network configuration ingestion so a licensed compressor can be described rather than replaced; chain-balancing targets | 26 | WP-102 | DFT |
| WP-203 | Compression-aware ATPG | GF(2) decompressor constraints in search and SAT, compactor observability and aliasing-aware detection claims, mask controller in the IR | 15 | WP-202, WP-109 | ARCH |
| WP-204 | Parity harness and ledger | Per-fault state comparison and comparison reporting (VER, 7 ew); frozen-input hashing, ledger schema with owner and resolution, and coverage/pattern/runtime diff-report generation (ATPG, 4 ew) | 11 | WP-106 | VER + ATPG (7 + 4) |
| WP-205 | Parity runs and triage | Three blocks against TMAX, compared-block list with profile hashes, one ledger entry per difference, fault-count and X-handling reconciliation, no unexplained delta, audit sample of UNTESTABLE faults re-proven and reported | 19 | WP-204, WP-111 | VER |
| WP-206 | 1 M-gate scale | SoA tuning, memory profile against the ≤ 16/32 GB targets, checkpoint/restart, stage-level timing, 32-thread determinism at scale | 17 | WP-205 | ATPG |
| WP-207 | Pattern-count reduction | Set-cover ILP on a reduced universe, merge runtime cap, X-fill for compressibility | 10 | WP-105 | ATPG |
| WP-208 | DRC golden set | Golden netlists annotated with expected rule fires, false-positive triage, waiver workflow (creation, extension, audit trail), golden set pinned in CI with expected-firing hashes; expired waivers already fail WP-103's load check, so none of that is re-implemented here | 7 | WP-103 | VER |
| WP-209 | X-source handling | Inventory ingestion, bounding at source, mask cost per unit of fault coverage, masked-vs-untestable accounting in every report | 10 | WP-201 | DFT |
| WP-210 | TMAX script migration | Tcl aliases on the migration layer, per-command mapping table, explicit errors for unsupported commands | 8 | WP-010 | FE |
| WP-211 | Vendor ATE format writer | Second writer after the platform is frozen and the vendor simulator loop; closure of every syntax/semantic warning moves to WP-313 | 9 | WP-106, TBD-04 | ATE |
| WP-212 | Performance regression CI | Google Benchmark microbenchmarks, whole-flow benchmarks, 10% regression gate, JSON trend | 6 | WP-206 | FE |
| WP-213 | PPSFP mode | Pattern-parallel 64/word simulation, agreement test against parallel-fault, sampling use | 6 | WP-009 | ATPG |
| WP-214 | FAN engine | FFR and head-line construction, multiple backtrace, dominator propagation, shared implication/activation with PODEM, per-fault engine selection | 7 | WP-111 | ATPG |
| WP-215 | WGL writer | Second writer from the same IR, round-trip parse | 3 | WP-106 | FE |

**Phase total: 166 ew.**

**Notes**

- WP-203 is the largest single technical unknown in the plan. If the compactor model cannot be validated against a real network by M8, the P2 gate is met on **uncompressed** patterns and compression is reported as incomplete. That is a gate waiver approved by the architecture owner and recorded in the P2 gate pack, not a silent scope change.
- WP-205 owns the parity ledger. The architect signs the ledger; verification runs it. A coverage number without a closed ledger is not evidence (architecture 14.3).
- WP-214 lands at P2 start, after PODEM has been measured on the 50 k block. Parity block 1 is graded with PODEM only — as it was in P1 — while blocks 2 and 3 get FAN. The re-sequencing costs P2 slack: 25 ew instead of 35.
- P2 carries three DFT engineers. WP-202 (compression network) and WP-201 (multi-clock capture) are 38 ew between them, and two engineers supply 34 ew for the whole phase; the third is what makes DFT run at 94% instead of 141%. WP-203 is ARCH-led because compression-aware ATPG is design-heavy and the architect would otherwise be unallocated in P2.

---

## 6. Phase 3 — Transition and diagnosis (M10–M15)

**Architecture exit gate:** transition coverage and diagnosis accuracy meet the M9 targets in section 20.1 — transition ≥ 90% testable within 1.0 pt of TMAX; diagnosis top-5 ≥ 90%, top-1 exact ≥ 70%. **Expanded gate:** G3.1–G3.6. **Demo:** M4.

| ID | Work package | Deliverables | ew | Depends on | Owner |
|---|---|---|---|---|---|
| WP-301 | Two-timeframe transition model | Launch/capture pair semantics, LOC and LOS procedures, two-frame SAT encoding, pair extension to the pattern IR | 14 | G2 | ATPG |
| WP-302 | Transition fault list | STR/STF generation, transition collapsing, pair-coverage accounting distinct from stuck-at | 8 | WP-301 | ATPG |
| WP-303 | Transition ATPG | Structural two-frame search, SAT two-frame closure, per-fault budgets, branch DRC executed before `add_faults` | 18 | WP-302 | ATPG |
| WP-304 | Protocol conformance | Command-order validation against `transition-parity-protocol-p2.md`, procedure checks, protocol regression set | 8 | WP-303 | DFT |
| WP-305 | Transition compaction | Pair-compatible merge, X-fill under pair constraints, re-grade | 8 | WP-303 | ATPG |
| WP-306 | M9 transition baseline and parity | TMAX transition run on the three P2 parity blocks, frozen pattern package, in-house vs TMAX delta per block | 14 | WP-303 | VER |
| WP-307 | Transition at scale | 1 M-gate transition run, runtime and memory, abort triage | 10 | WP-306, WP-206 | ATPG |
| WP-308 | Chain diagnosis engine | Effect-cause chain analysis, failing-signature matching, bit/segment localization, typed suspects per the benchmark schema | 20 | WP-306 | ATPG |
| WP-309 | Diagnosis benchmark harness | Injection per `diagnosis-benchmark-p3.md`, canonicalization, equivalence groups, competition ranking, scoring with Wilson intervals | 14 | WP-308 | VER |
| WP-310 | Decision and result records | `D-DIAG-01` at M9; `D-DIAG-RESULT-01` at the P3 gate; report tooling that checks corpus SHA and mode inheritance | 6 | WP-309 | ARCH |
| WP-311 | Physical-aware stitching | DEF/LEF ingest, wirelength-aware ordering including the local swap pass deferred from WP-102, measured improvement with no DRC regression | 15 | WP-208 | DFT |
| WP-312 | M9/P3 evidence pack | Transition model docs, corpora and hashes, parity ledger, TMAX diagnosis baseline, sign-offs | 10 | WP-306, WP-310 | PM |
| WP-313 | P2 sustaining | Block support, parity re-runs, defects arriving from P2 usage, replay mismatch localization deferred from WP-107, ATE writer warning closure deferred from WP-211; charge split 6 ATPG / 12 DFT / 13 VER / 2 ATE | 33 | G2 | ATPG + DFT + VER + ATE |
| WP-314 | Classifier feature pipeline | Feature schema from the DB and run statistics, labelled dataset from P2/P3 runs, design-level split to prevent leakage | 8 | WP-205, WP-306 | ML |

**Phase total: 186 ew.**

**Notes**

- WP-306 and G3.1 use the **M9-frozen TMAX transition pattern baseline**, not patterns generated after the fact. The corpus definition is fixed in `diagnosis-benchmark-p3.md` section 3.1 and must not be replaced by a P3-era corpus.
- The transition protocol's TMAX command-order validation is still open: no TMAX installation has been found on the current environment. It is a **G3.2 prerequisite**, not something P3 can discover late. If TMAX access is unavailable, WP-304 runs against the protocol's expected command order and the gate carries an explicit "not validated against an installed release" limitation.
- WP-310 is deliberately small and deliberately not an implementation package. `D-DIAG-01` records the evaluation mode and TMAX baseline at M9; `D-DIAG-RESULT-01` records the P3 scores against the same corpus. The mode is inherited, never reselected, and a corpus SHA mismatch blocks scoring. The record schemas already exist in the companion doc; this package builds the plumbing, not the policy.
- Under `PER_STRATUM` or `DEFAULT`, cell and chain are separate gates. A cell-only pass does not satisfy G3.3, and a pooled number does not rescue a failing stratum.

---

## 7. Phase 4 — Scale and AI (M16–M24)

**Architecture exit gate:** program production sign-off (v1 plus P3–P4) on grx930 and G100. **Expanded gate:** G4.1–G4.6. **Demo:** M5.

| ID | Work package | Deliverables | ew | Depends on | Owner |
|---|---|---|---|---|---|
| WP-401 | Full-chip runs | grx930 and G100 top level, 64-thread runs, runtime and memory within the architecture 15.1 budgets; domain partitioning fallback above 32 GB, checkpoint/restart at 10 M gates, abort triage on both chips | 36 | G3 | ATPG |
| WP-402 | Runtime to parity | 64-thread profiling, hot-path work, SAT call reduction, ≤ 1.0× TMAX target | 24 | WP-401 | ATPG |
| WP-403 | Second chip profile | Profile, intake audit, bring-up, library-gap closure, and the 16.5 acceptance evidence for that chip | 28 | WP-401 | DFT |
| WP-404 | Fault difficulty classifier | Training pipeline, model registry with hashes, ONNX inference; features and dataset come from WP-314 | 6 | WP-314 | ML |
| WP-405 | Shadow mode and promotion | Shadow runs across three blocks or one chip, promotion gate with held-out designs, fallback byte-equality, drift and fallback-rate monitoring | 12 | WP-404 | ML |
| WP-406 | Backtrace ranking experiment | Ranking learned from successful traces, bounded deterministic inference, shadow evaluation only | 10 | WP-405 | ML |
| WP-407 | Chain-assignment cost model | Offline cost model that seeds the deterministic packer; legality checks unchanged | 8 | WP-405 | ML |
| WP-408 | GPU fault-sim experiment | Kernel validated against the CPU reference, determinism check, go/no-go on production use | 12 | WP-213 | ATPG |
| WP-409 | Silicon correlation | ATE pattern run, failure-mapping tooling to chain and net, profile update, pattern re-issue with a new version, yield and failure signatures recorded | 20 | WP-401, WP-211 | ATE |
| WP-410 | Advanced diagnosis | Multiplets, bridging signature extensions, resolution reporting | 12 | WP-308 | ATPG |
| WP-411 | Release engineering | Packaging, offline install, schema-version matrix, operator docs. Produces the release candidate and its installable artifact; does not run the acceptance ladder or sign the release | 10 | WP-401 | FE |
| WP-412 | Sustaining | Customer block support, defect fixes, parity maintenance, regression triage; charge split 30 ATPG / 20 VER / 10 DFT | 60 | G3 | ATPG + VER + DFT |
| WP-413 | Full-chip TMAX parity | Parity runs at 10 M-gate scale on both chips, ≥ 3 representative blocks per chip, per-chip comparison report and closed ledger; fault counting and X handling re-reconciled at full chip | 32 | WP-401, WP-204 | VER |
| WP-414 | Release qualification | Qualification of the WP-411 candidate: full benchmark ladder, ATE format validation, unattended clean-checkout run, manifest reproduction of the pattern set, signed release report (PM). A defect found here returns to WP-411 for a new candidate | 28 | WP-401, WP-211, WP-411 | VER |
| WP-415 | Full-chip DRC and waiver audit | Full-chip DRC on both tops, waiver audit with expiry, scan-map sign-off for each chip | 16 | WP-401 | DFT |

**Phase total: 314 ew.**

**Gate mapping**

| Package | Serves | Evidence it produces |
|---|---|---|
| WP-413 | G4.1, G4.5 | Per-chip parity report and closed ledger at 10 M-gate scale |
| WP-414 | G4.5, G4.6 | Qualification report, ladder results, manifest reproduction, replay and vendor simulator logs |
| WP-415 | G4.1 (both tops), G4.3 (second chip) | Full-chip DRC report, waiver audit, scan-map sign-off |

**Release engineering builds; release qualification proves.** WP-411 owns the packaged artifact, its offline install and its operator documentation; WP-414 owns the evidence and the signature. WP-414 depends on a named WP-411 artifact and records its hash, and G4.5 closes only when the qualification report cites that hash — so neither package can satisfy the gate alone. A packaging change produces a new candidate and invalidates the prior qualification.

**Notes**

- **Scope was corrected, not the roster.** Three acceptance items in architecture 16.5 had no package and two packages were costed at block scale; section 12.1 carries the before and after. The roster is unchanged because the phase contains two tape-out and silicon cycles where the constraint is calendar, not effort.
- WP-404 through WP-407 are gated by shadow evidence, not by model quality claims. Disabling the model must restore the deterministic baseline **byte-identically**; that test is part of WP-405's definition of done.
- WP-408 is an experiment with an explicit stop. The CPU engine remains the reference implementation (architecture 10.5).
- WP-409 is the only package that requires silicon. Its schedule is calendar-bound, which is why P4 carries the largest reserve.

---

## 8. Phase 5 — Boundary scan and memory BIST (M25–M33)

**Exit gate:** G5.1–G5.6 on both chips' test designs; BSDL and BIST vectors accepted by the board and ATE teams. **Expanded gate:** G5.1–G5.6. **Demo:** M6.

**Standing.** Phase 5 is scheduled and resourced in this plan: it has its own capacity line in section 1.2, a milestone at M33, packages WP-501–WP-516 and a gate family in section 10. It is not part of the 24-month baseline. The baseline stays at 817 ew against 1066 ew and no P0–P4 package, gate or date moves because of it — the phase is funded from a separate 234 ew line that begins after M24.

Its scope is now in the architecture. The revision landed at `993a84d`: 2.2 no longer lists memory BIST or boundary scan as non-goals, 21 carries boundary-scan, memory-BIST and memory-coverage rows, 9.1 defines `MEMORY_ARRAY` as an exclusion reason, and 14.3 splits its coverage row so the logic claim stays comparable to TMAX. Section 0's rule is satisfied, so the phase is startable on the conditions in 8.1 — which are design and board deliverables, not a paper change. It was sequenced here before that revision existed so the work was costed and gated rather than rediscovered in month 25.

**Architecture revision, as landed.** Section 2.2 removed memory BIST and boundary scan from the non-goals; section 21 gained boundary-scan, memory-BIST and memory-coverage rows; 9.1 defines `MEMORY_ARRAY` as the exclusion reason for SRAM internals; 14.3 and 17.1 split coverage into a logic row and a memory row with no cross-comparison, for the reason set out in 8.5. The revision cost ARCH 6 ew, carried in this phase's total rather than P0–P4.

### 8.1 Entry conditions

- Architecture revision landed (architecture `993a84d`, blob `61c9e75`); the remaining conditions are intake, not paper.
- TBD-13 package/bond map (die pad → package pin). Without it BSR ordering is guesswork and EXTEST has no pin reference.
- TBD-14 SRAM behavioural models with defect injection, replacing `BLACKBOX` for memory instances.
- TBD-15 SRAM redundancy and eFuse specification. Without it WP-514 analyses repair but emits nothing programmable.
- TBD-16 board/interconnect model for EXTEST vector proof.
- One reporting rule, specified in 8.5: memory coverage is its own column and never enters the logic parity ledger.
- TBD-13 through TBD-16 are registered in section 13 with an M25 needed-by date. They are not P0–P4 inputs, and the baseline's critical-inputs list does not depend on them.

### 8.2 Boundary scan — 56 ew

The TestMAX DFT / BSD Compiler equivalent: 1149.1 insertion, TAP, BSDL and interconnect vectors. 1149.6 is not included; it adds approximately 8 ew if the board requires AC-coupled testing.

| ID | Work package | Deliverables | ew | Depends on | Owner |
|---|---|---|---|---|---|
| WP-501 | Boundary-scan cells and BSR insertion | 1149.1 cell variants, BSR ordering across pads and package pins, boundary-cell type in the DB, package/pad map intake | 10 | TBD-13 | DFT |
| WP-502 | TAP and instruction register | TAP FSM, IR plus IDCODE/BYPASS/EXTEST/SAMPLE/PRELOAD, optional INTEST/CLAMP/HIGHZ, test-mode pin model | 12 | WP-501 | DFT |
| WP-503 | BSDL writer and validator | 1149.1 BSDL text, package map, attribute set, lint and round-trip parse before release | 10 | WP-502 | FE |
| WP-504 | Boundary-scan vectors | EXTEST interconnect and SAMPLE/PRELOAD on the existing pattern IR, STIL/WGL output, board-model constraints | 10 | WP-502 | DFT |
| WP-505 | Boundary-scan DRC | TAP pin conflicts, BSR reachability, chain integrity, instruction coverage, the WP-103 waiver mechanism | 6 | WP-501 | DFT |
| WP-506 | Boundary-scan verification | TAP FSM and EXTEST simulation, BSDL validation, injected board opens/shorts, comparison against BSD Compiler on a test design | 8 | WP-503–WP-505 | VER |

### 8.3 Memory BIST — 76 ew

The TestMAX SMS equivalent: SRAM intake, collars, March engine, repair and integration. This is the larger risk of the two: it needs models the program does not currently consume, and there is no silicon on this program to confirm it.

| ID | Work package | Deliverables | ew | Depends on | Owner |
|---|---|---|---|---|---|
| WP-511 | SRAM intake and memory fault models | Memory instances leave `BLACKBOX`; behavioural models, SAF/transition/coupling/word-line fault classes, per-macro metadata | 10 | TBD-14 | DFT |
| WP-512 | Collar and wrapper generation | Address/data/control muxing, comparator, pipeline, functional-mode bypass, clock/reset handling, BIST-mode pin model; reports the collar gate count per macro that 8.5 needs | 16 | WP-511 | DFT |
| WP-513 | March engine and BIST controller | March C-/MATS+ and programmable sequences, address/data generation, signature, controller RTL | 20 | WP-512 | DFT |
| WP-514 | Repair analysis | Row/column redundancy analysis, BIRA, fuse/signature output | 12 | WP-513, TBD-15 | DFT |
| WP-515 | BIST integration and mode DRC | BIST bypass in scan, test-mode pin conflicts, memory coverage accounting separate from logic coverage, report changes | 8 | WP-513, WP-514 | DFT |
| WP-516 | BIST verification and ATE vectors | Defect-injected model runs, March coverage, BIST-mode ATE vectors, post-insertion parity re-run on one block (8.5), warning closure | 10 | WP-515 | VER 6 / ATE 4 |

**Phase total: 132 ew** (range 92–172 at ±30%). With gate and decision overhead — ARCH 6 for the architecture revision and gate packs, PM 4 — the phase plans **142 ew** against its 234 ew line, leaving 92 ew of phase reserve.

### 8.4 Role load

The phase carries its own roster for 39 weeks: 3.0 DFT, 1.0 VER, 0.5 FE, 0.5 ATE, 0.5 ARCH, 0.5 PM — 6.0 FTE. No role among the baseline's eleven changes, and no baseline capacity is consumed. Role capacities are taken from section 12.1 as printed, so the audit and this table use the same numbers.

| Role | FTE | Capacity (ew) | Demand (ew) | Load |
|---|---|---|---|---|
| DFT | 3.0 | 117 | 104 | 89% |
| VER | 1.0 | 39 | 14 | 36% |
| FE | 0.5 | 19.5 | 10 | 51% |
| ATE | 0.5 | 19.5 | 4 | 21% |
| ARCH | 0.5 | 19.5 | 6 | 31% |
| PM | 0.5 | 19.5 | 4 | 21% |
| **Total** | **6.0** | **234** | **142** | **61%** |

Every role is under 110% with margin. DFT is the tightest at 89%, and the 11 ew of slack there is the phase reserve, not a second project.

**Why the phase is not absorbed into P0–P4.** The alternative was tested and rejected on the role rule, not on the aggregate. Absorbing 142 ew into the existing 1066 would land at 959/1066 — 90% aggregate, which looks acceptable — but DFT's share is 104 ew against a 44 ew reserve, and the role rule is per role:

| Role | P0–P4 demand / capacity | With Phase 5 work | Load |
|---|---|---|---|
| ARCH | 37 / 105 | 43 / 105 | 41% |
| ATPG | 286 / 292 | 286 / 292 | 98% |
| DFT | 186 / 230 | 290 / 230 | **126%** |
| VER | 175 / 187 | 189 / 187 | 101% |
| FE | 48 / 105 | 58 / 105 | 55% |
| ML | 44 / 65 | 44 / 65 | 68% |
| ATE | 31 / 43.5 | 35 / 43.5 | 80% |
| PM | 10 / 41 | 14 / 41 | 34% |
| **Total** | **817 / 1066** | **959 / 1066** | **90%** |

DFT lands at 126%, a 60 ew gap: roughly one additional DFT engineer from P2 through P4, which changes the roster the architecture fixes at 11. Section 12.1 records the same finding. Absorption is therefore available only as a staffing decision, and this phase is the version that needs no such decision.

### 8.5 Effect on G2.1: the coverage denominator

G2.1 measures one pool today. Architecture 9.9 defines testable coverage as `detected ÷ (detected + not_detected + aborted)`, and 9.1 fixes the fault sites: gate inputs and outputs, primary inputs and outputs, scan-cell data inputs and outputs, and chain segments, with black-box internals excluded by reason code. Memory macros are `BLACKBOX` (architecture 6.2), so their internals contribute no sites at all. The denominator G2.1 has been comparing against TMAX is logic-only **because there is no memory in it**, not because a rule keeps memory out — the reporting rule in 8.1 has nothing to enforce until arrays leave black box.

Memory BIST breaks that by accident of structure, in three places that must not land in the same bucket.

| Pool | Contents | Sites, illustrative 1 M-gate block with 24 macros | Detected by | TMAX-comparable |
|---|---|---|---|---|
| Logic `D_L` | gates, PIs/POs, scan cells, plus collar muxes, comparators, March controller and BIRA once inserted | 2.00 M → 2.03 M (+2–3%) | scan ATPG | yes, on an identical netlist |
| Memory `D_M` | bit cells, word lines, decoders, sense amps — SAF, transition and coupling classes per WP-511 | 0 → 3.15 M (24 × 65,536 × 2) | March signature through the collar | no — TMAX has no sites for them |
| Pooled | both, as one number | 5.18 M | neither mechanism alone | no |

The collar count is the one figure the plan does not have yet: 1–1.5 k gates per mid-size macro, and WP-512 must report the real number out of the DB. The memory count is arithmetic on a stated macro geometry, not a design measurement; it is here to show the ratio, because the ratio is what decides the wording.

**Why the two pools are never summed.** Take the illustration at face value: 99.5% of 2.03 M logic and 95% of 3.15 M memory is 5.01 M of 5.18 M — **96.8%**, a number worse than either input and descriptive of neither. Reverse the March figure to 99% and the pooled result is **99.2%**: inside G2.1's 0.5 pt band, passing, while 31 k memory faults are unreported as memory faults. Pooling is not conservative or aggressive; it is uninterpretable in both directions, and it destroys the TMAX comparison because TMAX supplies a denominator for only one of the two pools.

**The netlist-identity problem is the sharper one.** 17.1 accepts a coverage comparison only when both tools ran the same netlist, fault list, constraints, chain count and options. The three P2 parity blocks are graded pre-insertion. Inserting collars changes the netlist hash and adds 2–3% to the denominator, so a post-insertion GMAX run compared with a pre-insertion TMAX run is not a 0.5 pt comparison — it is two unrelated numbers that happen to sit near each other. The phase takes the first reading: both tools re-run on the post-insertion netlist for one block, reusing the WP-205 harness, which is why WP-516 carries that re-run in its VER share. If it overruns, WP-516 and the phase total move by the overrun; the requirement is not dropped.

**Gate wording this phase inherits.** G2.1 and G2.4 are restated below for any run whose netlist contains BIST collars. That cannot happen before WP-512, so the M9 P2 gate keeps the current wording in section 10 and the restated rows bind the post-insertion parity re-run in WP-516. The architecture revision makes the split explicit in 14.3, which is why the restatement is now a consequence rather than a proposal.

*G2.1, as written:* `Testable coverage | ≥ 99.5% and within 0.5 pt of TMAX | coverage + ledger | VER`

*G2.1, restated:* `Logic testable coverage | On the post-insertion netlist, over architecture 9.1's fault sites with collar and BIST-controller logic included: ≥ 99.5%, and within 0.5 pt of TMAX on the identical netlist, fault list, constraints, chain count and options, both hashes recorded. SRAM array internals are excluded under reason code MEMORY_ARRAY and the excluded count is reported. Memory faults never enter this denominator; a pooled figure satisfies this criterion at no value. | coverage report stating denominator count, netlist hash and fault-list hash; ledger; comparison report naming the shared netlist | VER`

*G2.4, as written:* `Parity coverage | ≥ 3 blocks, every difference closed in the ledger | ledger + compared-block list with profile hashes | ARCH`

*G2.4, restated:* `Parity coverage | ≥ 3 blocks, every difference closed in the ledger; the compared-block list records each block's netlist hash with inserted collars counted, and a block is not counted twice under two netlists | ledger + compared-block list with profile and netlist hashes | ARCH`

Memory coverage is deliberately not a G2 criterion and gets no M9 threshold. It is reported beside G2.1 from the moment arrays leave `BLACKBOX` — its own column, its own denominator, blank where no BIST exists — and it is gated at G5.4, on injected faults per macro class, against a target fixed at the architecture revision. A figure with no threshold is still worth printing: it is the only thing that would reveal a collar that is present but testing nothing.

G4.1 is unchanged, because this phase follows M24. G3.1's transition pool gains the same collar sites at roughly a third of the stuck-at increment after collapse, inside its 1.0 pt band, so its threshold does not move.

### 8.6 Risks

- **No silicon.** G5.2 rests on a board model and G5.4 on defect-injected models. Neither substitutes for the silicon correlation G4.2 requires of the baseline, and there is no G5.7 equivalent to it on this program. Treat the phase's claims as simulation-grade.
- **The inputs may not exist.** TBD-13 and TBD-16 are new design and board deliverables. If they are unavailable the failure mode is the one TBD-08 already shows: the run cannot be executed on the schedule, and the gate moves rather than the evidence being simulated.
- **Coverage contamination.** If memory coverage enters the logic parity ledger, G2.1 becomes incomparable to TMAX, which excludes memories. 8.5 fixes the gate wording and WP-515's separate column enforces it; neither is a formatting choice.
- **Repair without eFuse data.** WP-514 can analyse redundancy without TBD-15 but cannot emit programmable repair; that is a partial deliverable, not a pass.
- **Two capabilities in one phase.** If boundary scan slips, memory BIST should not be held behind it; the phase can split after G5.1/G5.3 with two separate exit gates.

### 8.7 Licensing fork

If TestMAX SMS and BSD Compiler are licensed rather than built, most of this phase disappears: WP-501–WP-506 are cancelled because the vendor supplies BSR insertion, TAP, BSDL and vectors, and WP-511–WP-514 reduce to SRAM model intake and collar-instantiation checks. What remains is approximately 14 ew of integration — test-mode pin and DB model (4), BIST bypass and mode DRC (4), collar checks and BIST access vectors (6) — planned inside P2/P3 where WP-110 already models test mode. That is a work package inside the 1066 ew, not a phase, and it consumes baseline reserve rather than this phase's line.

TBD-09 therefore decides between a 142 ew phase and a 14 ew package. The phase is written as the build branch, and the architecture revision has landed, so TBD-09 is now a scope decision rather than a precondition: if it resolves to licence, 8.2 and 8.3 are deleted, the G5 family reduces to G5.3/G5.4 on vendor collars, and section 1.2 loses its Phase 5 row.

## 9. Cross-cutting workstreams

These are not separate packages; they are obligations attached to packages, and they are checked at every gate.

### 9.1 Verification and CI

| Level | Owner | First required | Gate |
|---|---|---|---|
| Unit + property | package owner | P0 | Commit |
| Differential (sim vs scalar, SAT vs structural, PPSFP vs parallel-fault) | VER | P0 / P2 | Nightly |
| Integration (ISCAS'85/89, ITC'99, block) | VER | P0 / P1 | Nightly |
| Benchmark ladder rungs 1–7 | VER | P0 → P4 | Weekly / release |
| Parity ledger | VER + ARCH | P1 harness, P2 runs | Milestone |
| Determinism (1 vs N threads, byte-identical) | FE | P0 | Every gate |

The benchmark ladder is the release spine. A rung is entered only when its predecessor exits; a rung's results are recorded with input hashes, or they do not count.

### 9.2 Determinism

Determinism is a release property, not a P4 cleanup item. It is enforced from P0 by stable IDs, merge-by-fault-ID, and the hash test in WP-011. Any package that changes merge order, iteration order, or ID assignment must show an unchanged or explained hash before merge.

### 9.3 Parity ledger

One ledger, opened in WP-204, closed per difference in WP-205 and WP-306. Every entry has: dimension, expected, observed, cause, owner, resolution, status. A coverage or pattern-count claim that cites the ledger is evidence; one that does not is an opinion.

### 9.4 Intake and profiles

Profiles are versioned YAML, never code branches (architecture 3). Every gate pack records the profile hash used. Intake items are tracked in section 13; an item marked `[INTAKE]` in the architecture that is still open at its needed-by date is escalated at the gate, not absorbed silently.

### 9.5 Legal and licensing

TBD-11 must close before any code leaves the company and before `third_party` contents are frozen in WP-001. Internal use is lower risk, but attribution and notice obligations still apply. A dependency whose license is unclear blocks WP-001, not the release.

### 9.6 Documentation and ADRs

ADRs 009–011 (SAT, pattern IR, threading) are P0 deliverables. Every later package that makes an architectural choice — solver reuse, compactor model, diagnosis suspect schema — writes or amends an ADR in the same change set. Docs are reviewed like code.

---

## 10. Gate criteria and evidence

### G0 — M2 (Foundation)

| # | Criterion | Threshold | Source | Evidence | Owner |
|---|---|---|---|---|---|
| G0.1 | c432 and c499 end-to-end from CLI | 100% of testable faults detected on both circuits | 17.1 P0 gate | `reports/coverage.json` + fault-list hash | VER |
| G0.2 | SAT smoke | ≥ 1 SAT and ≥ 1 UNSAT on c432; MiniSat agrees on ≤ 1 k-gate instances | 17.1 P0 gate, 14.4 | `reports/sat_smoke.json` | ATPG |
| G0.3 | Determinism | 1-thread and N-thread pattern/coverage hashes identical | 14.6 determinism test | hash test log | FE |
| G0.4 | Commit gate | Debug and release build, unit tests, sanitizer smoke and format/lint all block a merge | 14.6 commit gate | CI config + one red and one green run | FE |
| G0.5 | Reproducibility | Every run emits a complete manifest | 4.2, 6.6 | sample manifests | VER |
| G0.6 | ADRs | 009–011 accepted | 20.2 item 6 | `docs/adr/` | ARCH |
| G0.7 | Simulation agreement | Parallel-fault simulation matches the scalar reference on every net of c432 | 10.4, 17.1 P0 (bit-parallel simulation) | differential test log | ATPG |

G0.2 evidence is `reports/sat_smoke.json`: `cross_check` is the pinned Yosys
SAT result and `minisat_check` is the pinned MiniSat 2.2.1 result on the same
CNFs (ADR-009). The MiniSat leg runs when `GMAX_MINISAT` points at the
developer-cache binary. The gate invocation also sets `GMAX_G0_2_GATE=1`; with
that set, a missing `GMAX_MINISAT` fails the test instead of skipping it. CI
runs this gate in the separate `sat-gate` job with the pinned Yosys/MiniSat
caches; the default ctest matrix remains skip-friendly.

### G1 — M5 (Vertical slice)

| # | Criterion | Threshold | Source | Evidence | Owner |
|---|---|---|---|---|---|
| G1.1 | Testable coverage, 50 k block | ≥ 95%, denominator and fault-list hash stated | 2.3 P1 gate, 17.1 | `reports/coverage.json` | DFT |
| G1.2 | DRC | Clean, or every waiver has owner + expiry and none is expired (expiry checked at load, not at review); every rule in the P1 rule set recorded in the gate pack is mutation-tested, with 0 false negatives; false positives < 5% of rules fired | 2.3 P1 gate, 8.3 | `reports/drc.json`, recorded P1 rule list, waiver file with expiry check, mutation and false-positive triage logs | DFT |
| G1.3 | Insertion equivalence | `SE=0` netlist equivalent to original | 8.1, 14.4 | ABC `cec` / Yosys report | DFT |
| G1.4 | Replay | 100% detection match | 2.3, 14.4 | `reports/replay.json` | VER |
| G1.5 | STIL | Round-trip structural equality; vendor simulator free of unresolved warnings | 12.2, 12.5 | round-trip + vendor log | FE |
| G1.6 | Determinism | 1 vs N threads identical | 14.6 | hash test log | FE |
| G1.7 | TMAX comparison | Pattern count measured against TMAX with no gate; ATPG runtime ≤ 5× TMAX | 2.3 P1 gate | TMAX run log + pattern-count comparison, entered in the parity ledger when WP-204 opens it | VER |
| G1.8 | Parser fuzz smoke | No crash and no unbounded growth in the Verilog, Liberty and STIL readers | 7.5, 14.6 nightly | fuzz run log | FE |

### G2 — M9 (Production stuck-at)

| # | Criterion | Threshold | Source | Evidence | Owner |
|---|---|---|---|---|---|
| G2.1 | Testable coverage | ≥ 99.5% and within 0.5 pt of TMAX | 2.3 production, 17.1 | coverage + ledger | VER |
| G2.2 | Pattern count | ≤ 1.10× TMAX on identical constraints | 2.3, 17.1 | parity report | VER |
| G2.3 | Runtime | ≤ 1.5× TMAX, same hardware, warm cache | 2.3, 17.1 | parity report | VER |
| G2.4 | Parity coverage | ≥ 3 blocks, every difference closed in the ledger | 14.3, 16.5 | ledger + compared-block list with profile hashes | ARCH |
| G2.5 | Scale | 1 M-gate subsystem inside memory and time budget | 15.1 | `reports/perf.json` | ATPG |
| G2.6 | Multi-clock | Grouped capture replays; sequential capture scanned-or-masked | 9.7 | replay + DRC report | DFT |
| G2.7 | Exit codes and CI | Codes 0–5 honoured; commit/nightly/weekly gates blocking | 19.3, 14.6 | CI config + run log | FE |
| G2.8 | Untestable integrity | Every `UNTESTABLE` carries solver metadata, an audit sample is re-proven, and every fault-state difference against TMAX is explained — no false untestable | 9.1, 14.3 | audit report + fault-state ledger | ATPG |
| G2.9 | DRC at scale | Clean or waived on all parity blocks; no expired waivers; false positives < 1% of rules fired | 2.3 production, 8.3 | DRC report | DFT |
| G2.10 | Compression baseline | Illinois then XOR network implemented and measured (ratio, coverage delta, mask overhead); or a written waiver, approved by the architecture owner and recorded in the P2 gate pack, recording the uncompressed fallback and its pattern-count cost | 11.3, 17.1 P2 deliverables | compression report, or waiver with its approval record | DFT |

### G3 — M15 (Transition and diagnosis)

| # | Criterion | Threshold | Source | Evidence | Owner |
|---|---|---|---|---|---|
| G3.1 | Transition coverage | ≥ 90% testable, within 1.0 pt of TMAX, on the three P2 parity blocks | 20.1, 17.1 P3 | transition parity report + compared-block list with profile hashes | VER |
| G3.2 | Transition parity | Ledger closed per difference; command order validated against the P2 protocol | 14.3, `transition-parity-protocol-p2.md` | ledger + conformance log | DFT |
| G3.3 | Diagnosis accuracy | `D-DIAG-01` approved and frozen, or `DEFAULT` recorded as the applied mode; `D-DIAG-RESULT-01` complete with the TMAX confirmation attached; mode and thresholds inherited; corpus SHA matches; top-1 ≥ 70%, top-5 ≥ 90%; no underpowered required stratum | 20.1, `diagnosis-benchmark-p3.md` sections 2.1, 7, 8 | result record + scoring worksheet | ARCH |
| G3.4 | Diagnosis resolution | Group-size, segment-length, suspect-count, confusion-matrix, undetected and discarded counts reported | `diagnosis-benchmark-p3.md` 5.3, 8 | result record B.5 attachments | VER |
| G3.5 | Physical-aware stitching | Measured wirelength improvement, no DRC regression | 8.2, 20.1 | stitching report | DFT |
| G3.6 | N/A handling | Every N/A stratum listed with a reason, excluded from the denominator, never scored as a pass | `diagnosis-benchmark-p3.md` 2, 8 | decision record A.4 | DFT |

### G4 — M24 (Scale and AI)

| # | Criterion | Threshold | Evidence | Owner |
|---|---|---|---|---|
| G4.1 | Full chip | Testable coverage ≥ 99.5% and within 0.5 pt of TMAX; pattern count ≤ 1.10× TMAX; runtime and memory within architecture 15.1 budgets; DRC clean or every waiver documented and unexpired — on both chips | full-chip report, WP-413 parity report and ledger, WP-415 DRC report and waiver audit | ATPG + DFT |
| G4.2 | Silicon correlation | ATE run, failure mapped to chain/net, patterns re-issued; loop closed at least once | correlation report | ATE |
| G4.3 | Second chip | Supported to the same acceptance criteria (16.5) | chip sign-off, WP-415 DRC and waiver audit | DFT |
| G4.4 | AI guardrails | Shadow evidence recorded; disabling the model restores the baseline byte-identically | promotion report + fallback test | ML |
| G4.5 | Release package | Coverage/DRC/pattern manifest, parity ledger, known limitations, schema versions; a TMAX comparison report on at least three representative blocks per chip, with a ledger entry for every coverage or pattern-count difference | WP-414 qualification report citing the WP-411 artifact hash, with the compared-block list and profile hashes | PM |
| G4.6 | Reproducibility and handoff | The flow runs unattended from a clean checkout with one command; the run manifest reproduces the exact pattern set from the recorded inputs and seed; patterns replay in gate-level simulation and are accepted by the ATE simulator without unresolved warnings | WP-414 qualification report, replay log, vendor simulator log | VER |

Every item in architecture 16.5 maps to a criterion; nothing is gated only by a package's deliverables.

| 16.5 acceptance item | G4 |
|---|---|
| Unattended clean-checkout run, one command | G4.6 |
| DRC clean or every waiver documented and unexpired | G4.1 (both tops), G4.3 (second chip) |
| Testable coverage target, within 0.5 pt of TMAX | G4.1 |
| Pattern count ≤ 1.10× TMAX, runtime within budget | G4.1 |
| Replay in gate-level simulation, ATE simulator acceptance | G4.6 |
| TMAX comparison report on ≥ 3 blocks, with parity ledger | G4.5 |
| Run manifest reproduces the exact pattern set | G4.6 |

WP-415 produces the DRC evidence for both tops; G4.5 packages the report without restating the threshold. G4.2 (silicon correlation, architecture 14.5) and G4.4 (AI guardrails, 13.4) sit outside 16.5 by design.

### G5 — M33 (Boundary scan and memory BIST)

Not in force until the architecture revision in 8.1 lands; until then this family is a reservation, and no gate pack cites it.

| # | Criterion | Threshold | Evidence | Owner |
|---|---|---|---|---|
| G5.1 | Boundary-scan insertion | Every pad has a BSR cell; TAP present; functional-mode bypass equivalence proven; boundary-scan DRC clean or every waiver documented and unexpired | BSR map, equivalence log, WP-505 DRC report | DFT |
| G5.2 | BSDL and interconnect | BSDL passes 1149.1 syntax validation and an independent validator; EXTEST/SAMPLE vectors detect every injected open and short in the board model; replay matches simulation | BSDL file, validator log, board-model results | VER |
| G5.3 | BIST insertion | Collar on every SRAM instance; BIST bypass equivalence proven; March executes in simulation; memory coverage reported in its own column | collar map, equivalence log, March report | DFT |
| G5.4 | March coverage | Target fixed at the architecture revision (proposed ≥ 95% of injected memory faults per macro class, with signature check) | injection ledger and coverage report | VER |
| G5.5 | Repair | Repair analysis localizes injected row/column defects and emits the correct fuse/signature; no false repair | repair report on injected defects | DFT |
| G5.6 | Integration and release | BIST and boundary-scan modes run from ATE vectors without unresolved warnings; the run manifest reproduces the vectors; memory coverage stays separate in every report | ATE log, run manifest, report samples | ATE |

G5.4 is deliberately the only threshold the phase sets for memory, and it is measured on injected faults rather than silicon, for the reason in 8.6. The logic claim stays with G2.1 under its restated wording.

---

## 11. Milestones and demos

| Milestone | Date | Demo | Gate evidence |
|---|---|---|---|
| M0 | Month 0 | Team staffed, repo, build-vs-buy decided | decision record TBD-09 |
| M1 | Month 2 | ISCAS'85 stuck-at ATPG and simulation from CLI, twice, identical | G0 pack |
| M2 | Month 5 | 50 k block: insert, DRC, ATPG, STIL, replay | G1 pack |
| M3 | Month 9 | TMAX parity on ≥ 3 blocks; 1 M-gate subsystem | G2 pack |
| M4 | Month 15 | Transition ATPG and diagnosis demo against M9 targets | G3 pack |
| M5 | Month 24 | Program production sign-off package for grx930 and G100 | G4 pack |
| M6 | Month 33 | BSDL and BIST modes accepted by the board and ATE teams | G5 pack |

The M1 demo is deliberately narrow: one command, two runs, byte-identical output. It is the first proof that the reproducibility claim in architecture 2.1 is real rather than aspirational.

M6 is the first milestone outside the 24-month baseline: it exists only because Phase 5 is scheduled (section 8), and it cannot be claimed before the architecture revision in 8.1 lands.

---

## 12. Staffing and load

| Phase | FTE | Composition |
|---|---|---|
| P0 | 6 | architect, 2 ATPG, 1 DFT, 1 VER, 1 FE |
| P1 | 8 | +2 DFT (from M3) |
| P2 | 11 | +1 ATPG, +1 VER, +0.5 ATE, +0.5 PM |
| P3 | 11 | −1 DFT, +1 ML |
| P4 | 11 | unchanged |
| P5 | 6.0 | 0.5 architect, 3 DFT, 1 VER, 0.5 FE, 0.5 ATE, 0.5 PM — separate line from M25 |

Capacity, effort and load per phase are in section 1.2. Total headcount stays at the architecture's 11; what moved is the phase each role sits in. Phase 5 is the one exception and is not counted here: it is a separate 6.0 FTE team after M24, so the eleven are untouched. Section 12.1 holds the role-level view, the rule it is held to, and what changed.

### 12.1 Role load audit

An owner is an accountable lead, not a sole implementer. But a lead assignment above the role's capacity is a delivery risk dressed up as a label, so the plan applies one rule: **every role's lead-assigned effort is ≤ 110% of that role's capacity in the phase.** Anything above that is fixed by ownership, by phase mix, or by moving the package — never by an undisclosed contribution.

| Phase | ARCH | ATPG | DFT | VER | FE | ML | ATE | PM |
|---|---|---|---|---|---|---|---|---|
| P0 | 10/10 | 20/20 | 9/10 | 9/10 | 10/10 | — | — | — |
| P1 | 6/13 | 24/26 | 40/39 | 12/13 | 11/13 | — | — | — |
| P2 | 15/17 | 44/51 | 48/51 | 33/34 | 17/17 | — | 9/11 | 0/8.5 |
| P3 | 6/26 | 84/78 | 35/52 | 41/52 | 0/26 | 8/26 | 2/13 | 10/13 |
| P4 | 0/39 | 114/117 | 54/78 | 80/78 | 10/39 | 36/39 | 20/19.5 | 0/19.5 |
| P5 | 6/19.5 | — | 104/117 | 14/39 | 10/19.5 | — | 4/19.5 | 4/19.5 |

Demand / capacity in engineer-weeks. ATE is carried at 0.65 FTE in P2 (11 ew) and 0.5 FTE elsewhere; PM at 0.5 FTE. `0` means the role is on the phase roster with no costed package; `—` means it is not on the roster.

No cell exceeds 110%. The tightest are P3's ATPG at 108%, and P1's DFT, P4's VER and P4's ATE at 103%. Phase 5's tightest is DFT at 89%, on its own line, with no baseline role in it.

| Phase | Before the audit | Finding | Fix |
|---|---|---|---|
| P0 | FE 19/10 (190%), ATPG 29/20 (145%), DFT 0/10 | Two roles over capacity while a third had nothing to own | Liberty and the fault list to DFT, the four-state reference to VER, the command schema to ARCH; every role now ≤ 100% |
| P1 | DFT 47/26 (181%) | A 21 ew gap hidden by an 87 ≤ 91 aggregate test | Third DFT engineer, WP-111 to VER; 40/39 after the P0/P1 rebalance |
| P2 | DFT 50/34 (147%), ATPG 55/51 (108%), ATE 11/8.5 (129%) | Three roles over; architect and ML seats unallocated | Third DFT, WP-203 to ARCH, WP-208 to VER, ATE at 0.65 FTE |
| P3 | ATPG 104/78 (133%) | Sustaining was charged entirely to ATPG | WP-313 split 6/12/13/2 across ATPG, DFT, VER and ATE |
| P4 | ML 44/39 (113%), ATPG 126/117 (108%), VER 0/78; 202 ew against 429 (47%) | Role mix and scope both wrong: features scheduled a phase after the data exists, sustaining charged entirely to ATPG, verification uncosted, and three 16.5 items without a package | WP-314 takes the feature schema and dataset, WP-404 keeps training; WP-412 split 30/20/10; WP-413–WP-415 added and WP-401/402/403/409 re-estimated — 314 ew (73%) |
| P5 | DFT 290/230 (126%) if absorbed into P0–P4, against a 90% aggregate | The aggregate test passed while the role rule failed by 60 ew — the same shape as P1's hidden 181% | Phase 5 given its own 234 ew line after M24: 104/117 (89%); absorption remains available only as a staffing decision |

**Idle seats are not spare capacity.** The architect and PM rows carry review, gate packs and parity-ledger sign-off that the work-package estimates do not itemise; P4's remaining front-end time is release maintenance. P3's front end (0/26) and P4's architect (0/39) are the largest gaps; P1's architect now carries the SDC subset at 6/13. If one of them starts carrying implementation, the phase estimate is revised rather than the seat absorbing it silently.

**P0 has no reserve:** 60 ew against 60 ew, with ARCH and DFT at 110%. That is deliberate — the foundation phase is short and its work is well understood — but WP-008 (SAT smoke) is the named flex item, moving to P1 week 1 if PODEM overruns rather than slipping the gate.

---

## 13. Critical inputs and decisions

| Item | ID | Owner | Needed by | Blocks |
|---|---|---|---|---|
| Build-vs-buy decision | TBD-09 | Executive + DFT lead | M0 | Staffing and the whole plan |
| First G100 bring-up block | TBD-10 | DFT | M0 | WP-111, G1 |
| Legal/license review | TBD-11 | Legal | M0 | WP-001 `third_party` freeze, any external distribution |
| Netlists and libraries, both chips | TBD-01, TBD-02 | Design | M1 | WP-003, WP-004, WP-111 |
| Scan spec (chains, clocks, SE) | TBD-03 | DFT | M1 | WP-101, WP-102 |
| Test clock and capture procedure | TBD-06 | DFT | M1 | WP-201, G2.6 |
| ATE platform and pin map | TBD-04 | ATE | M1 | WP-211, WP-409 |
| TMAX version, license, scripts | TBD-08 | DFT | M1 (runs at M4) | WP-204, WP-205, G2.1–G2.4 |
| Compression IP decision | TBD-05 | DFT + tools | M3 | WP-202, chain target selection |
| X-source inventory and policy | TBD-07 | DFT | M2 | WP-209 |
| DEF/LEF physical data | TBD-12 | Design | M13 | WP-311 |
| Package/bond map (pad → pin) | TBD-13 | Design | M25 | WP-501, WP-505, G5.1 |
| SRAM models with defect injection | TBD-14 | Design | M25 | WP-511, WP-512 |
| SRAM redundancy and eFuse spec | TBD-15 | Design | M25 | WP-514, G5.5 |
| Board/interconnect model | TBD-16 | Board + ATE | M25 | WP-504, G5.2 |

Two of these deserve executive attention rather than engineering attention: **TBD-09** decides whether the plan runs at all — and, for Phase 5, whether it is a 142 ew phase or a 14 ew package (8.7) — and **TBD-08** decides whether the P2 gate is measurable. Without TMAX, G2.2 and G2.3 cannot be evaluated as written (section 15).

---

## 14. Risk register mapped to the plan

| Risk (architecture 18) | Early warning | Plan response | Packages |
|---|---|---|---|
| X handling harder than expected | Aborted faults > 10%; mask overhead > 20% | X-source bounding before scale; mask cost accounted per fault; conservative masks with coverage attribution rather than silent suppression | WP-209, WP-105, WP-205 |
| Multi-clock capture semantics | DFT003/DFT019 spikes; replay mismatch | Capture procedures formalized before parity runs; sequential capture gated on scanned-or-masked intermediate state | WP-201, WP-103, WP-110 |
| SAT blowup on hard faults | A few faults dominate runtime | Per-fault budgets from P0; solver-reuse benchmark in P1; portfolio and structural preprocessing at scale | WP-007, WP-109, WP-402 |
| Pattern count misses parity | > 1.2× at P1 | Harness early; compaction v1 in P1; ILP and compression in P2 | WP-204, WP-207, WP-202 |
| Scan DRC escapes | Silicon failure traced to an unscanned cell | Mutation test per rule at implementation time; waiver expiry; golden netlists | WP-103, WP-208 |
| Library scan-cell gaps | Missing `test_cell`; function mismatch | Intake audit; hard error on function mismatch; acknowledgement path for naming-convention fallback | WP-003 |
| Netlist handoff delays | Profile fields still TBD at M1 | Bench rungs 4–6 substitute; WP-111 re-points to a qualified public block; M2 demo labelled | WP-111 |
| Determinism bugs | Differing hashes across runs or thread counts | Stable IDs and merge-by-ID from P0; hash test at every gate | WP-011, WP-206 |
| Key-person loss | Single owner on ATPG or scan | ADRs, pairing on PODEM/FAN, ownership rotation in gate packs | WP-012, WP-214 |
| Performance regression | Nightly trend slope | Micro and whole-flow benchmarks with a 10% gate | WP-212 |

The register is reviewed at each gate, not monthly in isolation, so a risk that is not moving shows up next to the gate it threatens.

---

## 15. Change control and what would change this plan

The plan's baseline is the architecture blob recorded at the top of this document. A new architecture commit triggers a traceability re-check (section 17) and, if scope moved, a plan revision with a new hash. The revision record and the hash rule are in 15.1.

### 15.1 Plan revision record

| Rev | Date | What moved | Plan hash |
|---|---|---|---|
| v0.6 | 2026-11-24 | Recorded the G0.2 SAT smoke evidence: the pinned Yosys `cross_check`, the pinned MiniSat 2.2.1 `minisat_check`, the `GMAX_G0_2_GATE=1` hard-failure mode, and the separate `sat-gate` CI job. The default ctest matrix remains skip-friendly. No scope, package, gate, effort or capacity figure changed. | `sha256:8fbe5402a28ead444b7e815d7f0d5d66b08185cbfa75752ca97bc337898c3b40` |
| v0.5 | 2026-11-23 | Clarified architecture 6.4: MUX2 uses the conservative select-X rule (a known select picks the then/else data; an X or Z select yields X even when the data inputs agree), and `out = (s & a) | (~s & b)` is explicitly non-normative because it is data-order dependent under an X select. Re-pinned the baseline blob and recomputed the plan hash. No scope, package, gate, effort or capacity figure changed. | `sha256:da562a2b9ecdb06a75618aae14f8f1c3c50767abbb5c8b00e2961d8b28f2a0c1` |
| v0.4 | 2026-11-22 | Re-pinned to the component-code revision of the architecture: 4.1 gained the ten codes (`FE`, `DB`, `DFT`, `ATPG`, `FSIM`, `CMP`, `PAT`, `DIAG`, `AI`, `VER`), the citation rule `C-` plus code, and a table the checker reads. `ids-defined` now treats a cited code with no row there as dangling, like any other id. No scope, package, gate, effort or capacity figure changed. | `sha256:ae269b8a490b656929d14051d9e6f58b6fa2485e6974589ca5beac048cd71d0b` |
| v0.3 | 2026-11-22 | Re-pinned to architecture `993a84d` (blob `61c9e75`), the revision Phase 5 was waiting on: 2.2 dropped both capabilities from the non-goals, 21 gained the boundary-scan, memory-BIST and memory-coverage rows, 9.1 defined `MEMORY_ARRAY`, and 14.3/17.1 split coverage into logic and memory. Section 8 records the revision as landed and 8.1 as satisfied on that condition; the Baseline line, and every reference to a pending revision, were updated. No scope, package, gate, effort or capacity figure changed. | `sha256:cfe56c78822d22dae30397d67b56fe0e1da0a06ea7d8b0d27962ca57b90bd191` |
| v0.2 | 2026-11-22 | Phase 5 promoted from a candidate appendix to a scheduled phase. Appendix D dissolved into section 8 (WP-501–WP-516, 132 ew; 142 ew with ARCH 6 and PM 4); G5.1–G5.6 moved into section 10; a 234 ew capacity line and a 959/1300 programme total added to 1.2; milestone M6 at M33 in section 11; a 6.0 FTE row and an audit row in section 12; TBD-13–TBD-16 in section 13; Appendix A's memory-BIST row replaced by a pointer to section 8. Sections 8–16 renumbered to 9–17 and every internal reference updated. P0–P4 are unchanged: 817 ew against 1066 ew. | `sha256:12348591f24eb76b6b65fd99319d34f102d3d7327a89e88f1d27d38cd2f8f52d` |
| v0.1 | — | First issue for review. Phase 5 carried as Appendix D, outside the baseline and outside any capacity line. | not recorded |

The digest is SHA-256 over this file, UTF-8 with LF line endings, with the digest in the newest row replaced by `HASH` — a record cannot hash itself, so that substitution is the convention rather than a loophole. `sed -E '0,/sha256:[0-9a-f]{64}/ s/sha256:[0-9a-f]{64}/sha256:HASH/' dft-atpg-implementation-plan.md | sha256sum` reproduces the newest row. Earlier rows keep the hash of the text they describe and are not re-derivable from this file, which carries one live digest at a time.

The two documents move together: v0.2 scheduled the phase ahead of the architecture, and v0.3 re-pins the plan once the architecture revision landed at `993a84d`. Neither row is authority to begin work — Phase 5 starts on the input conditions in 8.1, which are deliverables rather than papers. A later change to Phase 5 scope, total or gate adds a row here rather than editing an existing one.

Scope additions are not accepted inside a phase. A new fault model, a new format, or a new chip goes through the architecture first.

Cuts are changes too. A reduction that alters what a gate proves — fewer rules, a capability landing a phase late, a missing comparison block — follows the same path, with one named approver and one recorded location:

| Decision | Approver | Recorded in |
|---|---|---|
| A gate criterion narrows, or an architecture capability lands a phase late (G1.2, G2.10) | Architecture owner (ARCH), because the plan is subordinate to the architecture | That phase's gate pack |
| A limitation ships with the release (G4.5 waiver) | Program manager (PM), who signs the production release | WP-414 qualification report, known limitations; cited from the G4 pack |
| A package or part of one moves between phases without changing a gate, and the receiving phase's role stays within the 110% rule (WP-307; WP-211's simulator-loop fallback) | The role lead that owns the package in both phases | Gate pack of the phase that loses the work |
| A gate moves later because a gate-critical package overruns and no scope can be cut (WP-107/WP-111, WP-204/WP-205, P0's architect) | Program manager (PM), who owns the schedule | That phase's gate pack, with the plan hash revised |

Engineering waivers inside a run are not plan changes: the DRC waivers under G1.2 and G2.9 carry an owner and an expiry, expiry is checked at load from P1 by WP-103, the workflow around it (creation, extension, audit trail) lands with WP-208 in P2, and the full accumulated set is audited at G4 by WP-415 — without a separate approval. The four events below are program decisions, not routine gate waivers; each is recorded as a plan revision with a new hash, and any waiver it creates follows the table above.

| Event | Effect | Residual risk |
|---|---|---|
| **Buy decision at M0** (license TMAX, build thin automation) | Plan collapses to WP-001..WP-010 subset + WP-204 + WP-210, roughly 40 ew over two quarters. P1–P4 are cancelled. | None to the schedule; the strategic gap in architecture 17.3 moves to the vendor. |
| **No TMAX license or version at M1** | G2.2 and G2.3 are restated as absolute targets (≥ 99.5% coverage; an absolute pattern budget per block; runtime against an internal baseline). The parity ledger becomes a benchmark ledger. | "≤ 1.10× TMAX" becomes unverifiable and is recorded as a gate waiver approved by the architecture owner in the P2 gate pack. |
| **Bring-up block slips ≥ 6 weeks** | P1 runs on benchmark rungs 4–5; G1 is met on a public block; P2 cannot start because parity needs a real block. | Schedule moves with intake, not with effort. |
| **Transition/diagnosis scope cut** | Stop after P2: v1 ships at M9. Keep WP-301..WP-306 archived for a later phase. | The architecture's 24-month program becomes a 9-month v1 plus a deferred phase. |

The 8–12 engineer-year maturity gap in architecture 17.3 is out of scope. Closing it requires a new plan, not a longer P4.

---

## 16. First 30 days

Derived from architecture 20.2, ordered by dependency rather than by listing.

| Week | Work | Packages | Exit check |
|---|---|---|---|
| 1 | Repo, CMake, dependency pinning, CI skeleton, sanitizer job; TBD-09/TBD-10 confirmed; 20 DRC rule issues opened with mutation-test stubs | WP-001, WP-012 (start) | Clean build from fresh checkout; CI runs on a trivial test |
| 2 | `.dfdb` skeleton and flat graph; Liberty parser start; TMAX version and scripts requested; G100 block candidate reviewed | WP-002, WP-003 | DB round-trips a synthetic 100-gate design with stable IDs |
| 3 | Yosys ingest on c432; four-state evaluator with exhaustive tests; PODEM skeleton; ADR-009 draft | WP-004, WP-005, WP-007 | c432 elaborates with zero unexplained diagnostics |
| 4 | Fault list, scalar simulation, CLI v0, first end-to-end run; gate review for M0 | WP-006, WP-009, WP-010, WP-011 | c432 runs twice with byte-identical coverage and pattern hashes |

**30-day acceptance (architecture 20.2):** c432 runs end-to-end from the CLI, twice, with byte-identical output, and the G100 block profile is frozen. Both, not either.

If c432 runs but the profile is not frozen, the 30 days failed on the input side and P1 starts late. That is the correct reading: the schedule risk in this program is intake, not code.

---

## 17. Traceability to the architecture

| Architecture section | Covered by |
|---|---|
| 2.2, 21 (Phase 5 rows, pending the revision in 8.1) | WP-501–WP-516, section 8 |
| 3 Profiles and intake | WP-111, WP-403, section 13 |
| 4.2 Control flow, 4.3 threading | WP-010, WP-011, WP-012 |
| 6.1–6.3 DB and graph | WP-002 |
| 6.4 Four-state and word encoding | WP-005, WP-009, WP-213 |
| 6.6 Persistence, manifests | WP-002, WP-010 |
| 7.1–7.3 Verilog, Liberty, SDC | WP-003, WP-004, WP-110 |
| 7.4–7.5 STIL/SPF, validation | WP-106, WP-215 |
| 8.1–8.2 Insertion and stitching | WP-101, WP-102, WP-311 |
| 8.3–8.4 DRC and outputs | WP-103, WP-208 |
| 9.1 Fault list and collapsing | WP-006, WP-302 |
| 9.2–9.6 Engine pipeline, PODEM, FAN, SAT, scheduling | WP-007, WP-109, WP-214 |
| 9.7 Full-scan and multi-clock | WP-104, WP-201 |
| 9.8–9.9 Compaction, coverage reporting | WP-105, WP-207, WP-205 |
| 10 Fault simulation | WP-009, WP-213, WP-408 |
| 11 Compression | WP-202, WP-203 |
| 12 Pattern IR and ATE | WP-106, WP-215, WP-211 |
| 13 AI architecture and guardrails | WP-314, WP-404–WP-407 |
| 14 Verification, parity, CI | WP-107, WP-204, WP-205, WP-212, WP-414 |
| 15 Performance and scale | WP-206, WP-307, WP-402 |
| 16 Bring-up and acceptance | WP-111, WP-205, WP-403, WP-413, WP-415 |
| 17 Roadmap, staffing, milestones | Sections 1.2, 11, 12 |
| 18 Risks | Section 14 |
| 19 Interfaces, repository, build | WP-001, WP-010, WP-210 |
| 20.1 Decisions | Section 13 |
| 20.2 Next 30 days | Section 16 |
| P2 transition protocol | WP-301–WP-306 |
| P3 diagnosis benchmark | WP-308–WP-310, G3.3–G3.6 |

---

## Appendix A — Deferred and out of scope

Not in the 24-month baseline. Each needs an architecture change and a new plan revision.

Memory BIST and boundary scan have left this appendix. They are Phase 5 (section 8): scheduled, costed on their own 234 ew line, and gated on the architecture revision in 8.1. Until that revision lands they remain non-goals in architecture 2.2 and no package in the phase may start.

| Item | Reason | Revisit |
|---|---|---|
| Path-delay ATPG | Not in the 24-month scope (architecture 21) | After P4 |
| IDDQ, cell-aware, bridging **ATPG** | v1 fault models are stuck-at; bridging appears only in diagnosis | After P4 |
| Analog test, multi-die | Explicit non-goals (architecture 2.2) | Not planned |
| UPF / power-aware ATPG | Out of scope for the 24-month program (architecture 20.1) | After P4 |
| GUI | Do not build one early (architecture 21) | Optional, post-P4 |
| Bit-exact TMAX command compatibility | TMAX-like surface plus migration layer only (architecture 2.2) | Never as a goal |
| GPU fault simulation as a production path | Experiment only; CPU is the reference (architecture 10.5) | WP-408 decision |
| Foundry library qualification | Consumes qualified libraries, does not qualify them | Not planned |
| 8–12 ey of transition/diagnosis/compression maturity | Outside the 24-month program (architecture 17.3) | Separate program |

## Appendix B — Open items in this plan

| Item | Why it is open | Owner | Needed by |
|---|---|---|---|
| TMAX availability on the build environment | No `tmax`/`tetramax` executable or install root found; blocks G3.2 validation | DFT | M4 |
| Compression network to validate WP-203 | Depends on TBD-05 and whether a licensed compressor is in use | DFT + tools | M3 |
| Whether D-CHAIN-BR is qualified | `D-DIAG-01` decides at M9; affects WP-309 corpus and G3.3 denominators | DFT + Architecture | M8 |
| Vendor ATE format details | TBD-04 unresolved; WP-211 cannot start without it | ATE | M6 |
| Exact FTE names and start dates | Staffing is the schedule-critical path (architecture 17.2) | PM | M0 |

An item that misses its needed-by date is escalated at the gate (9.4). If closing it requires changing a gate criterion or shipping a limitation, that change follows section 15 rather than a local decision.

---

## Appendix C — Estimate pressure test

Four estimates carry most of the remaining risk: they are the largest packages whose work has no internal precedent. Each is decomposed into sub-deliverables and compared with the nearest internal package or with the class of implementation it belongs to. A fifth package, WP-208, is decomposed below for a different reason — it was rescoped by subtraction rather than measured, so its residue had to be derived. The comparison is structural — external teams' hours are not public — so the output is a range and a revised point estimate, not a benchmark.

| Package | Was | Now | Range | Basis | Verdict |
|---|---|---|---|---|---|
| WP-103 DRC engine, 20 rules | 14 | 19 | 15–24 | Framework (union-find domains, Tarjan SCC, X-reachability, report, waivers) ≈ 5 ew, plus 1 ew to check expiry at load so G1.2 can block on an expired waiver before WP-208 exists. Twenty rules at 0.5–0.8 ew each, including a violating netlist and a mutation test ≈ 10–16 ew. Commercial scan-rule decks run to hundreds of rules over years; a 20-rule subset is tractable, but per-rule cost does not fall below ~0.5 ew because every rule needs its own evidence. | Revised +5 |
| WP-202 compression baseline | 20 | 26 | 22–36 | Illinois scan alone is 4–6 ew. XOR decompressor/compactor with network configuration ingestion and chain balancing is 14–18 ew. EDT-class networks in commercial tools are multi-year; this is a baseline that must beat Illinois on a real block (11.3), not parity with licensed IP. | Revised +6 |
| WP-308 chain diagnosis engine | 16 | 20 | 18–30 | Effect-cause chain analysis, signature matching and bit localization is research-grade work: academic prototypes are multi-person-year efforts, and commercial chain diagnosis is tuned over years. 20 ew buys a working engine for the benchmark strata, not the advanced multiplets in WP-410. | Revised +4 |
| WP-413 full-chip parity | 24 | 32 | 26–40 | Anchored internally: WP-205 does three block comparisons in 19 ew (about 6 ew each). WP-413 covers two chips × three blocks at 10 M gates, where each run is slower and each difference harder to attribute — six comparisons at roughly 5 ew each plus ledger closure. | Revised +8 |

What the revisions changed:

- **P0 → 58 ew (97%).** Liberty scan-cell detection and the exhaustive function check (2 ew) move to WP-101, and the run manifest (1 ew) moves to WP-011. P0 keeps two weeks of phase reserve; the architect lands at 10/10 (100%), one permitted week inside the 110% line.
- **P1 → 93 ew (89%).** WP-101 takes the 2 ew of Liberty scan-cell work from WP-003. The DRC rule set absorbs 5 ew — 4 from the estimate, 1 for expiry checking at load — and one week moves out of WP-102 to WP-311, which holds DFT at 40/39 (103%) instead of 105%. A further week goes to WP-106 for fuzz targets over the three readers, which G1.8 required and nothing produced. Replay localization leaves WP-107 for WP-313, which is where VER's week of reserve comes from. WP-110 (SDC) moves to ARCH, which already owns the command surface and the constraint schema.
- **P2 → 166 ew (89%).** The compression baseline absorbs 6 ew; DFT lands at 48/51 (94%), not 141%. WP-205 gains one week for G2.8's untestable audit report and G2.4's compared-block list, evidence the criteria named but no package produced. WP-204's tooling moves to ATPG (4 ew), which takes VER from 109% to 33/34 (97%), and WP-211's warning closure moves to WP-313, which takes ATE from 11/11 to 9/11 (82%).
- **P3 → 186 ew (65%).** Chain diagnosis absorbs 4 ew, the WP-102 swap pass adds 1 ew, replay localization adds 1 ew, and ATE writer warning closure adds 2 ew; the WP-313 sustaining split shifts to 6 ATPG / 12 DFT / 13 VER / 2 ATE — also where sustaining defects and late format polish actually land. ATPG lands at 84/78 (108%).
- **P4 → 314 ew (73%).** Full-chip parity absorbs 8 ew; VER reaches 80/78 (103%).

Baseline total 817 ew against the 1066 ew baseline capacity (77%), reserve 249 ew. Every role remains at or below 110%.

### WP-208, derived rather than decremented

The one week removed was expiry enforcement, now done at load in WP-103. That subtraction is only valid if the residue costs seven weeks, so it is decomposed here:

| Sub-deliverable | Basis | ew |
|---|---|---|
| Golden netlist corpus | Eight to twelve curated netlists — real block slices plus adversarial-but-clean constructs — each annotated with the rules that must fire and the rules that must not. WP-103's mutation netlists are per-rule unit fixtures, not this regression set. | 3 |
| False-positive triage | One DRC pass over the three parity blocks and the 1 M-gate subsystem, every firing on clean logic classified and either fixed or waived | 2.5 |
| Waiver workflow | Authoring side only: create, extend, audit trail. Validation at load stays in WP-103. | 1 |
| Regression wiring | Corpus pinned in CI by expected-firing hash, so a rule change that alters it fails the build | 0.5 |
| **Total** | | **7** |

Seven is right, at a range of 6–9; the corpus count is the uncertainty (eight netlists at the low end, twelve plus two adversarial cases at the high). Its high end is handled in the contingency table below, on the same terms as the other ranges.

### If the upper bound lands

A range is only useful if its top has a decided response. Each package reports effort-to-complete at its midpoint; if that puts it above the top of its range — or above what the phase's role capacity can carry, whichever comes first — the action below is taken in that phase rather than discovered at the gate. Every action returns the phase to roughly its planned effort, and the displaced work goes to a phase whose receiving role has slack.

| Package | Upper bound | Load if it lands | Action | After |
|---|---|---|---|---|
| WP-103 DRC rule set | 24 ew | P1 DFT 45/39 (115%) | Cut six rules and defer them to WP-313 in P3 (≈ 5 ew): the two multi-clock rules (DFT003, DFT019) and the rules that need compression-network or DEF inputs. Those inputs do not exist in P1, so their mutation tests would be synthetic. Six rules leave the P1 set. G1.2 is narrowed by change control to whatever remains after every cut — 14 only if no other cut has landed — and because the plan is subordinate to the architecture, the narrowing is approved by the architecture owner (ARCH) and recorded in the P1 gate pack under section 1.3 item 6, which names the six rules and their P3 date. | P1 DFT 40/39 (103%), P1 93 ew; P3 DFT 40/52 (77%), P3 191 ew |
| WP-202 compression baseline | 36 ew | P2 DFT 58/51 (114%) | Cut the licensed-network configuration ingestion and the chain-balancing targets (≈ 10 ew) and move both to WP-311 in P3, where DEF and wirelength data exist. G2.10 is still met on the in-house Illinois + XOR network; the compression report states that a vendor network cannot yet be described, and WP-311 closes it. Because that limits the 11.3 comparison to in-house networks, the limitation is approved by the architecture owner (ARCH) and recorded in the P2 gate pack beside the compression report. | P2 DFT 48/51 (94%), P2 166 ew; P3 DFT 45/52 (87%), P3 196 ew |
| WP-308 chain diagnosis | 30 ew | P3 ATPG 94/78 (121%) | Defer WP-307 (transition at scale, 10 ew) to P4 and fold it into WP-401. No G3 criterion depends on the 1 M-gate transition run — G3.1 is measured on the three P2 parity blocks — and scale is proven at full chip before G4. The deferral is approved by the ATPG lead and recorded in the P3 gate pack. | P3 ATPG 84/78 (108%), P3 186 ew; P4 ATPG 124/117 (106%), P4 324 ew |
| WP-413 full-chip parity | 40 ew | P4 VER 88/78 (113%) | First, move 10 ew of run execution to DFT, which holds 24 ew of P4 slack; the ledger and per-chip analysis stay with VER, so no scope is lost. If WP-415 consumes that slack, the fallback drops the second chip's third block and takes a G4.5 waiver naming the missing block, approved by the program manager (PM) as the signatory of the M5 production release and recorded in the WP-414 qualification report's known limitations, with the audit in the first sustaining cycle. | P4 VER 78/78 (100%), DFT 64/78 (82%); fallback VER 83/78 (106%) |
| WP-208 DRC golden set | 9 ew | P2 VER 35/34 (103%) | Drop the two adversarial netlists from the P2 corpus and move them to WP-313's VER share in P3. A sequencing change inside one role, approved by the verification lead and recorded in the P2 gate pack. | P2 VER 33/34 (97%), P2 166 ew; P3 VER 43/52 (83%), P3 188 ew |

They compose. If every row lands: P1 93 ew, P2 166 ew, P3 203 ew (DFT 50/52, VER 43/52, ATPG 84/78, ATE 2/13), P4 332 ew (ATPG 124/117, DFT 64/78, VER 78/78) — 852 ew against 1066 (80%), every role still at or below 110%.

### Point estimates with no range

WP-101, WP-102, the WP-107/WP-111 and WP-204/WP-205 pairs, WP-211 and P0's architect are single-point estimates, so there is no range top to trigger a row. The P1 DFT pair's buffer is the week restored when the swap pass moved to P3; P1 VER's is the week restored when replay localization moved to WP-313; P2 VER's is the four weeks of WP-204 tooling moved to ATPG, which leaves it at 33/34 (97%) with four weeks of overrun before the 110% line; P2 ATE's is the two weeks of warning closure moved to WP-313, leaving 9/11 (82%) and three weeks; P0 ARCH's is the week of run-manifest work moved to WP-011, leaving 10/10 (100%) and one permitted week. Each is spent before any scope moves. If effort-to-complete at the midpoint shows more than that:

| Package | Overrun | Action | After |
|---|---|---|---|
| P0 ARCH | > 1 ew | The remaining 10 ew are the DB, the CLI and the architecture-mandated ADRs (20.2 item 6); the only verification-shaped slice, the run manifest, already moved to WP-011. Nothing further can leave P0 without an architecture change, so G0 moves by the overrun, approved by the program manager (PM) and recorded in the P0 gate pack as a plan revision under section 15. | P0 ARCH 10/10 (100%); G0 moves by the overrun |
| WP-101 scan replacement | > 1 ew | Defer the dedicated scan-flop path and its library-specific equivalence check (≈ 2 ew) to WP-313, and ship mux-D insertion. G1.1 and G1.3 do not distinguish them, and the branch belongs with library variants in sustaining. Approved by the DFT lead and recorded in the P1 gate pack. | P1 93 ew, DFT 40/39 |
| WP-102 chain stitching | > 1 ew | No deferrable scope remains after the swap pass moved to WP-311, so the overrun is paid for out of DRC breadth: one rule per engineer-week leaves WP-103 for WP-313. That is a change-control narrowing of G1.2, not a local decision — architecture owner, P1 gate pack. | P1 93 ew, DFT 40/39 |
| WP-107 / WP-111 | > 2 ew combined | Localization already moved to WP-313, so nothing left here can be cut without weakening G1.1 or G1.4. First the WP-111 debug log goes (≈ 0.5 ew); beyond that the phase takes a schedule decision — G1 moves by the overrun, approved by the program manager (PM) and recorded in the P1 gate pack as a plan revision under section 15, not as a gate waiver. | P1 ≤ 93 ew, VER ≤ 14/13 (108%) |
| WP-204 / WP-205 | > 4 ew combined | Tooling already moved to ATPG; what remains is comparison logic and the three-block runs G2.4 counts, so nothing can be cut without a G2.4 narrowing. The phase takes a schedule decision — G2 moves by the overrun, approved by the program manager (PM) and recorded in the P2 gate pack as a plan revision under section 15. | P2 ≤ 170 ew, VER ≤ 37/34 (109%) |
| WP-211 vendor writer | > 3 ew | Warning closure already moved to WP-313; the vendor simulator loop (4 ew) is the next separable piece and already waits on TBD-04's format details, so it follows to WP-313's ATE share. G4.6 is the only criterion that consumes either and it is measured at M5, after P3. Approved by the ATE lead and recorded in the P2 gate pack. | P2 ATE 9/11 (82%), P2 166 ew; P3 ATE 6/13 (46%), P3 190 ew |

Rule cuts here and in WP-103's own row draw on the same 20-rule set, are cumulative, and apply in the order the overruns are found; G1.2 records the remainder, never a fixed count. The floor is the error-severity class — the rules whose violation would invalidate G1.1's coverage or G1.4's replay. Its size is fixed at M1, when the 20 rule issues are opened (section 16, week 1), so the plan does not pre-empt it; no cut may take a rule from that class, and if the arithmetic would reach it, the phase takes a gate decision under section 1.3 with a named risk acceptance, approved by the architecture owner, rather than a smaller DRC set. Neither remedy touches insertion correctness, the scan map, or G1.3 — the trade is a library optimisation first and DRC breadth second, in that order.

One rule applies to every contingency here: reduced scope is never claimed as a pass. G1.2 changes by change control, G2.10 records a limitation, WP-307's deferral, WP-101's scan-flop branch and WP-208's corpus cut are sequencing changes, P0's architect, WP-107/WP-111 and WP-204/WP-205 overruns move G0, G1 or G2 by plan revision, and the G4.5 fallback is a waiver — each with a named approver, an owner and a date, recorded as section 15 requires: the gate pack for all but the G4.5 waiver, which sits in the WP-414 qualification report. A gate that passed on quietly reduced scope would be worse than the overrun it avoided.

**Not revised, and why.** The design database, parsers and pattern IR are well-understood infrastructure with precedent from P0 and P1; the block-level parity work is anchored to WP-205; and the AI packages are bounded by the shadow-mode protocol rather than by implementation size. The revisions above are the only places where the original numbers depended on an assumption about how fast unfamiliar EDA work goes.

---

*End of plan. Revise at each gate; record the architecture hash this plan was derived from.*
