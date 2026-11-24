# G0 gate pack — Foundation (M2)

- Status: **DRAFT — decision pending signatures.** G0 is not yet declared met.
- Phase: P0, gate G0 (plan section 10).
- Reviewed commit: `1c4572d8530ece4f6318aa05931f9de47ccbe543`.
- Plan: v0.9, `sha256:7862006e8ff8a3a54218a37c2f990e749cc8674da09ef9c74d1d83c746b06bef`.
- Review date: 2026-11-24.
- Chair: DFT lead (signature below).

Evidence is pinned to the reviewed commit. A later commit reopens the pack: the
test hashes and the CI run identity below no longer describe it.

## 1. Gate criterion table

| # | Criterion | Threshold | Status | Evidence | Hash / run |
|---|---|---|---|---|---|
| G0.1 | c432 and c499 end-to-end from CLI | 100% of testable faults on both | **Met** | `tests/process/iscas85.cmake`; CI step "run G0.1 ISCAS'85 coverage gate" | test `sha256:35cc69d6…`; run `45454925112`, job `138661665180`; `cli.iscas85` passed |
| G0.2 | SAT smoke | ≥1 SAT and ≥1 UNSAT on c432; MiniSat agrees | **Met** | `tests/process/sat_smoke.cmake`, `sat_smoke.py`; `cross_check` (Yosys 0.71) and `minisat_check` (2.2.1) | test `sha256:dfe7e4a7…`, `sha256:6a85ea4f…`; same run, step "run G0.2 SAT smoke gate", success |
| G0.3 | Determinism | 1-thread and N-thread pattern/coverage hashes identical | **Waived — coverage leg met, pattern leg deferred** (W1) | `tests/process/threads.cmake`: 1 vs 4 workers, byte-identical `coverage.json`, manifests differ only `options.threads` | test `sha256:4b84fd78…`; same run, step "run G0.3 thread determinism gate", success |
| G0.4 | Commit gate | Debug/release, unit tests, sanitizer smoke, format/lint block a merge | **Met** | `.github/workflows/ci.yml` runs all ten jobs on push to `main` and once per pull-request head; `main` branch protection requires every one, strict and enforced for admins (verified 2026-11-24); PR #1 reported `blocked` then `clean`; PR #3 produced one suite; red run `45326575526`, green run `45436627671` | config `sha256:883b61dc…`; protection snapshot `docs/gates/G0-branch-protection.json` `sha256:c5525d0c…` (`protected: true`, 10 contexts, `enforce_admins: true`) |
| G0.5 | Reproducibility | Every run emits a complete manifest | **Waived — ATPG-run scope** (W2) | `run_atpg` writes `manifest.json`; `cli.iscas85` asserts schema, netlist/fault/coverage/stage hashes and byte-identical repeat | `src/core/report/report.cpp` `sha256:e707e6f4…`; `report.h` `sha256:e8cd354b…`; `src/shell/commands.cpp` `sha256:b522df83…` |
| G0.6 | ADRs | 009–011 accepted | **Met** | `docs/adr/ADR-009-sat-integration.md`, `ADR-010-pattern-ir.md`, `ADR-011-deterministic-threading.md`, all `Accepted` | `sha256:9c15584c…`, `sha256:a97521ab…`, `sha256:57e5549e…` |
| G0.7 | Simulation agreement | 64-lane matches scalar on every net of c432 | **Met** | `tests/differential/fault_diff.cpp`, `tests/process/fault_diff.cmake`; 350 nets, 700 raw faults, 256 patterns, **691 detected**, detection and first-pattern agreement | `sha256:e71178b0…`, `sha256:8dfe0c03…`; same run, step "run G0.7 fault-simulation differential gate", success |

The pinned-tool job (`G0 pinned-tool gates`, job `138661665180`) ran all four
G0.1/G0.2/G0.3/G0.7 steps successfully on the reviewed commit. Its Yosys is the
pinned 0.71 build and its MiniSat is the pinned 2.2.1 build.

## 2. Evidence artifacts, by path and hash

Tracked evidence sources at `1c4572d`:

| Path | SHA-256 |
|---|---|
| `.github/workflows/ci.yml` | `883b61dc0a14d5671843cd0ed196886cd029ec8a2189fe0bfe9d89b2c2eb1512` |
| `docs/gates/G0-branch-protection.json` | `c5525d0cc59d8ad5c0450925f1cb377bf929035dd2e28eb162842c773afc6e8d` |
| `tests/process/iscas85.cmake` | `35cc69d675f72f6cdbb5e909b60e9fed5ae0720062a6b1dd026128d3519729e8` |
| `tests/process/sat_smoke.cmake` | `dfe7e4a713162dffbf6b5d6af8ecac9761dd97964b225847627ee1139f962ece` |
| `tests/process/sat_smoke.py` | `6a85ea4fde3021d5803bfa8143455f4d77f9bdadde4c1f1fbcf6383a1040320a` |
| `tests/process/threads.cmake` | `4b84fd78ef9ad57dca745e29171ca03090b40a691f6d64c5d8ce717bbeac9381` |
| `tests/process/fault_diff.cmake` | `8dfe0c03e49cfc9ec2c939dac484ef4d7ba5e5ad66950f5b787f869578df799d` |
| `tests/differential/fault_diff.cpp` | `e71178b0308c2b74889e0c98d6ed43d6631de81ce77578faaa491f107bca3100` |
| `tests/CMakeLists.txt` | `98eb17d9f649d6f720155b71abf40cce7049d825cf89754a4b0e79d2d89edf36` |
| `src/core/report/report.cpp` | `e707e6f49bd6f6ce273f5b86ede1609936303b71c27d3213de6e5985a07dcd80` |
| `src/core/report/report.h` | `e8cd354b0b34d71d5d91ed6d916979a39c59334a9403569928f84594640bb3f2` |
| `src/shell/commands.cpp` | `b522df8326844f8d4c040125ab7a2a22b0f4ac478967b100fe3d02bf635ad563` |
| `docs/adr/ADR-009-sat-integration.md` | `9c15584c361586de8e9205421ea696ffd9281a741d52a640ce55e5c0d8437b9a` |
| `docs/adr/ADR-010-pattern-ir.md` | `a97521ab3ac649f56538f6ccd69f0319af715fd9d9da0d40afd35fe5317f7938` |
| `docs/adr/ADR-011-deterministic-threading.md` | `57e5549e2d4dea01f06de1351cef3b9211b5175f791ef78418860f22b8c940fc` |
| `dft-atpg-implementation-plan.md` | `7862006e8ff8a3a54218a37c2f990e749cc8674da09ef9c74d1d83c746b06bef` |

Flow content identities (stable across checkout paths after the Yosys
source-name normalization):

| Check | Netlist hash | Fault-list hash | Result |
|---|---|---|---|
| c432 end-to-end | `413dd789` | `424032e8` | raw 700, collapsed 484, detected 479, untestable 5, aborted 0; testable 479/479 (100%) |
| c499 end-to-end | `bebace97` | `6796ff49` | raw 574, collapsed 574, detected 574, untestable 0, aborted 0; testable 574/574 (100%) |
| G0.3 c432 1 vs 4 threads | `413dd789` | `424032e8` | `coverage.json` byte-identical; manifests equal except `options.threads` 1 vs 4 |
| G0.7 c432 differential | `413dd789` | raw list (700) | 700 faults, 256 patterns, 691 detected, zero disagreements |

Runtime reports (`coverage.json`, `manifest.json`, `sat_smoke.json`) are written
under the ctest work directories and **deleted by each test on success**
(`file(REMOVE_RECURSE …)`). No retained artifact therefore exists to hash at
pack time; the assertions and the CI job logs are the only durable record. This
is defect G0-D02 and blocks item 2 as written until a gate run preserves the
reports.

CI evidence identities:

- Pinned-tool gates: run `45454925112`, job `138661665180` — all four steps
  success, including `cli.iscas85`.
- G0.4 red run: `45326575526` on `824d632` (Windows `NOMINMAX` failure).
- G0.4 green run: `45436627671` on `fa90c6b`, all ten jobs success.
- G0.4 pull-request verification: PR #1 was `mergeable_state: blocked` while
  the last macOS check was queued, then `clean` once all 21 check runs passed;
  merged as `645474b`.
- G0.4 trigger policy: push workflows run only on `main`; feature branches are
  validated once by `pull_request`. PR #3 produced one workflow run and 11 check
  runs (ten jobs plus GitGuardian) instead of 21, and merged as `1c4572d`.
- G0.4 enforcement: `main` protected 2026-11-24; required contexts `format`,
  `G0 pinned-tool gates`, `ubuntu-latest Debug`, `ubuntu-latest Release`,
  `windows-latest Debug`, `windows-latest Release`, `macos-latest Debug`,
  `macos-latest Release`, `asan+ubsan`, `offline build`; strict up-to-date
  required; `enforce_admins` enabled; force pushes and deletions disabled.
  Verified by `GET /repos/kubeworkz/gmax/branches/main` (`protected: true`) and
  the protection endpoint (10 contexts). Snapshot:
  `docs/gates/G0-branch-protection.json`.
- Head run at the reviewed commit `1c4572d`: `45454925112` concluded **success**;
  all ten jobs passed. The macOS Debug job waited about ten minutes for a runner,
  then completed successfully (G0-D03 closed).

## 3. Open defects

| ID | Defect | Severity | Owner | Blocks |
|---|---|---|---|---|
| G0-D02 | Process tests delete their report artifacts, so the pack cannot attach `coverage.json` / `manifest.json` / `sat_smoke.json` by path and hash | Medium | VER + FE | Gate pack item 2 |
| G0-D04 | TBD-03 scan spec (chains, clocks, SE) is open | High (for WP-101) | DFT | Starting WP-101 |
| G0-D05 | TBD-01/TBD-02 libraries with `test_cell` are absent; the repo carries no Liberty library, only parser fixtures | High (for WP-101 completion) | Design + DFT | Real-library insertion and G1.3 equivalence |

G0-D01 **closed 2026-11-24**: `main` now requires all ten CI checks, with
strict up-to-date and admin enforcement, verified through the branch and
protection APIs. The rule also rejects direct pushes whose commit has no check
runs (`GH006: 10 of 10 required status checks are expected`), so changes land
through pull requests. PR #1 demonstrated the gate: `blocked` while the macOS
checks were queued, `clean` once all 21 check runs passed, then merged.

No Critical defect is open. G0.1/G0.2/G0.3/G0.7 regressions are caught by the
pinned-tool job; G0.4 enforcement now lives in repository configuration, and
the remaining gate defects are evidence retention (G0-D02) and intake (G0-D04,
G0-D05).

## 4. Parity ledger

Not applicable at G0. The ledger is opened by WP-204 in P1; P0 has no parity
runs to record.

## 5. Waivers

A waiver requires an owner, a reason, an expiry date, a written risk acceptance
and an approver under plan section 15. Both below narrow a gate criterion, so
the approver is the **architecture owner**. Neither is approved yet.

### W1 — G0.3 pattern-hash leg deferred to WP-106

| Field | Value |
|---|---|
| Criterion | G0.3 determinism |
| Owner | ATPG (WP-106) |
| Reason | P0 writes no test patterns. The only artifact with a stable identity is `coverage.json`; the "pattern/coverage hashes" threshold names a pattern artifact that WP-106 introduces. |
| Scope granted | G0.3 is met on the coverage leg: 1-thread and 4-thread runs produce byte-identical coverage and manifests differing only in the recorded worker count (ADR-011). The pattern-hash leg is carried into G1.6, which repeats the test when patterns exist. |
| Risk accepted | A nondeterministic pattern order could escape until WP-106. ADR-011 fixes fault chunks, merge by fault ID, seed handling and forbidden ordering sources; G1.6's determinism test runs the full flow, including patterns, once they exist, and would catch a violation before the G1 gate. |
| Expiry | WP-106 merge, and no later than the G1 gate (proposed hard date 2027-04-30) |
| Approver | Architecture owner (ARCH) — signature pending |
| Recorded in | This pack; plan section 10 G0.3 |

### W2 — G0.5 manifest scope limited to ATPG runs

| Field | Value |
|---|---|
| Criterion | G0.5 reproducibility |
| Owner | VER (WP-011) |
| Reason | `run_atpg` is the only P0 stage that emits a manifest; `read_verilog` and `write_faults` do not. The manifest's `inputs.profile` is `null`, `inputs.libraries` is `[]`, and there is no Yosys recipe/version hash, because P0 has no profile or library input. |
| Scope granted | G0.5 is met for runs that start from a `.dfdb` and `.faults`: tool version, netlist hash, fault-list hash, coverage hash, stage hashes, budget, seed and result are recorded, and repeated runs are byte-identical. Manifest coverage for ingest stages (`read_verilog`, `write_faults`) is deferred to the next WP-011 maintenance change; manifest coverage for profile and library inputs is deferred to WP-111, which first makes them gate-relevant. |
| Risk accepted | A run cannot be reproduced from its manifest alone across Yosys builds. Mitigated by pinning Yosys 0.71 and by the netlist hash identifying the elaborated input. |
| Expiry | WP-111 bring-up, and no later than the G1 gate (proposed hard date 2027-04-30) |
| Approver | Architecture owner (ARCH) — signature pending |
| Recorded in | This pack; plan section 10 G0.5 |

## 6. Sets named by a criterion

G0 names no rule list, corpus or block set that needs a separate record. The
c432/c499 circuits are pinned by netlist and fault-list hashes above; G0.7's
"every net" set is the raw stuck-at list derived deterministically from the
c432 netlist hash `413dd789` (700 faults). G0.2's two cases are the enumerated
faults 168 SA1 (SAT) and 51 SA0 (UNSAT).

## 7. Decision

**Recommendation: GO-WITH-WAIVERS (W1, W2), conditional.** It becomes effective
only when the following hold; items 1 and 4 are already closed:

1. G0-D01 — **closed 2026-11-24**: `main` requires all ten CI checks (strict,
   admins enforced); the protection snapshot is hashed in section 2.
2. W1 and W2 are signed by the architecture owner with dates and expiries.
3. G0-D02 is closed: a gate run retains the report artifacts under a stable path,
   with their SHA-256 recorded in this pack.
4. G0-D03 — **closed 2026-11-24**: run `45454925112` on the reviewed commit
   `1c4572d` passed all ten jobs; the pack is re-pinned to it.
5. The decision below is signed by the DFT lead, architect and verification lead.

Until the remaining three hold, **G0 is not met and WP-101 is not authorized to start.**
The PM signature is not required at G0; it is required at the release gate.

### What blocks starting WP-101

1. **A signed G0 decision.** WP-101's only stated dependency is G0 (plan
   section 9). Until the decision above is signed, starting it violates the hard
   gate protocol.
2. **TBD-03, the scan spec (chains, clocks, SE).** Plan section 13 lists it as
   needed by M1 and blocking WP-101 and WP-102. Without it the insertion target
   is undefined; only library-independent scaffolding can proceed, and only
   under a written intake risk acceptance.
3. **A library with `test_cell` (TBD-01/TBD-02).** Section 13 lists these
   against WP-003, WP-004 and WP-111, not WP-101 directly. WP-101 nevertheless
   inserts scan cells from `test_cell` and must prove functional-mode equivalence,
   and the repository carries no Liberty library — only parser fixtures. Real
   insertion and the G1.3 equivalence proof cannot complete without one.

Not blocking: pinned Yosys 0.71 is available and its `equiv_make`/`equiv_simple`
path is the ADR-009 choice; the G0.1/G0.2/G0.3/G0.7 gates are green in CI.

If the gate signs go-with-waivers before intake lands, the authorization should
cover only WP-101's library-independent slice — mux-D insertion scaffolding,
`test_cell` scan-cell detection with the acknowledgement warning, and the
≤ 6-input exhaustive function check — and hold real-library insertion and its
equivalence proof until TBD-03 and a library arrive.

| Role | Name | Decision | Signature | Date |
|---|---|---|---|---|
| DFT lead (chair) | | | | |
| Architect | | | | |
| Verification lead | | | | |
