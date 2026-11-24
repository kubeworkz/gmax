# G0 sign-off package — Foundation (M2)

- Status: **SIGNED — GO-WITH-WAIVERS (W1, W2), 2026-11-24. G0 is not recorded
  as met without waivers; WP-101 is not authorized.**
- Gate: G0, plan section 10. Gate pack: `docs/gates/G0-gate-pack.md`,
  blob SHA-256 `cc37d5792533389cb25f7529f7efc3db5b06eb1c0300d130dcbc5a2dee2f73e3`
  at `3942c65` (a Windows checkout with `core.autocrlf=true` hashes the CRLF
  working copy as `e00417751147e5baeca6b0ac6de0aadee0119fd912401e6b375e92599412a5b5`).
- Evidence pin: gate pack reviewed commit `2649fe8cf81b574fa1f3c8f5bb30c7c84d0c2dc8`.
- Plan: v0.9, `sha256:7862006e8ff8a3a54218a37c2f990e749cc8674da09ef9c74d1d83c746b06bef`.
- Architecture baseline: `dft-atpg-platform-architecture-grx930-g100.md` at blob
  `d9ae07254b87e5b3b5e2b41672613ac8fee340c9`.
- Package date: 2026-11-24.
- Signed by: Gridworkz (single operator) for the DFT lead (chair), architect,
  verification lead and architecture owner.
- Decision recorded: **GO-WITH-WAIVERS (W1, W2), signed 2026-11-24.**

This is the signed copy for the G0 record and for the two intake items that block
WP-101. The signatures are recorded in the gate pack. The package does not close
G0-D04 or G0-D05, declare G0 met without waivers, or authorize WP-101; a separate
written start authorization is still required.

## 1. Evidence snapshot

Statuses below are quoted from the gate pack; this package does not re-decide
them.

| Criterion | Recorded status | G0 effect |
|---|---|---|
| G0.1 ISCAS'85 c432/c499 | Met: 100% of testable faults on both | Satisfied |
| G0.2 SAT smoke | Met: one SAT and one UNSAT case, MiniSat agrees | Satisfied |
| G0.3 Determinism | Coverage leg met; pattern leg deferred by W1 | **Waived — W1 signed 2026-11-24** |
| G0.4 Commit gate | Met: ten required checks, branch protection enforced | Satisfied |
| G0.5 Reproducibility | ATPG-run scope met; ingest/profile/library manifest scope deferred by W2 | **Waived — W2 signed 2026-11-24** |
| G0.6 ADRs | Met: ADR-009, ADR-010, ADR-011 accepted | Satisfied |
| G0.7 Simulation agreement | Met: 64-lane and scalar agree on every c432 net | Satisfied |

CI and retained evidence:

- `G0 pinned-tool gates` passed all four steps in run `45454925112`; all ten jobs
  in that run passed.
- PR #7's final run `45482308413` passed all eleven checks. The gate job uploaded
  the `g0-evidence` artifact from `reports/g0`; the pack records the first
  artifact identity and its digest in section 2.
- Open defects: **G0-D04** (TBD-03 scan spec) and **G0-D05** (TBD-01/TBD-02
  libraries with `test_cell`). No Critical defect is open.
- The pack's local artifact hashes are the local evidence identity. CI manifests
  and `sat_smoke.json` differ by absolute paths and tool identities; the pack
  records that caveat rather than treating them as byte-identical.

## 2. Signature record R1 — W1 (G0.3 pattern leg)

| Field | Value |
|---|---|
| Request | Approve waiver W1 exactly as recorded in the gate pack, section 5 |
| Approver | Architecture owner (ARCH) |
| Criterion narrowed | G0.3 determinism |
| Waiver owner | ATPG (WP-106) |
| Scope | G0.3 is met on the coverage leg: 1-thread and 4-thread runs produce byte-identical `coverage.json`, and manifests differ only in `options.threads`. The pattern-hash leg is carried into G1.6. |
| Risk accepted | A nondeterministic pattern order could escape until WP-106. ADR-011 fixes fault chunks, merge by fault ID, seed handling and forbidden ordering sources; G1.6 repeats the determinism test once patterns exist. |
| Expiry | WP-106 merge, and no later than the G1 gate (proposed hard date 2027-04-30) |
| Recorded in | `docs/gates/G0-gate-pack.md` section 5 and plan section 10, G0.3 |

Approved text:

> I approve waiver W1 for G0.3. I confirm the scope and written risk acceptance
> recorded in G0 gate pack section 5 and accept that the pattern-hash leg is
> deferred to WP-106, expiring at WP-106 merge and no later than 2027-04-30.

| Approver | Name | Decision (approve / deny) | Signature | Date |
|---|---|---|---|---|
| Architecture owner (ARCH) | Gridworkz | Approve | `/s/ Gridworkz` | 2026-11-24 |

Approval of W1 is one precondition of the G0 decision. It is not the gate
decision and does not by itself change any criterion status.

## 3. Signature record R2 — W2 (G0.5 manifest scope)

| Field | Value |
|---|---|
| Request | Approve waiver W2 exactly as recorded in the gate pack, section 5 |
| Approver | Architecture owner (ARCH) |
| Criterion narrowed | G0.5 reproducibility |
| Waiver owner | VER (WP-011) |
| Scope | G0.5 is met for runs that start from a `.dfdb` and `.faults`: tool version, netlist hash, fault-list hash, coverage hash, stage hashes, budget, seed and result are recorded, and repeated runs are byte-identical. Manifest coverage for ingest stages is deferred to the next WP-011 maintenance change; profile and library manifest fields are deferred to WP-111. |
| Risk accepted | A run cannot be reproduced from its manifest alone across Yosys builds. Mitigated by pinning Yosys 0.71 and by the netlist hash identifying the elaborated input. |
| Expiry | WP-111 bring-up, and no later than the G1 gate (proposed hard date 2027-04-30) |
| Recorded in | `docs/gates/G0-gate-pack.md` section 5 and plan section 10, G0.5 |

Approved text:

> I approve waiver W2 for G0.5. I confirm the scope and written risk acceptance
> recorded in G0 gate pack section 5 and accept that ingest, profile and library
> manifest coverage is deferred to the WP-011 maintenance change and WP-111,
> expiring at WP-111 bring-up and no later than 2027-04-30.

| Approver | Name | Decision (approve / deny) | Signature | Date |
|---|---|---|---|---|
| Architecture owner (ARCH) | Gridworkz | Approve | `/s/ Gridworkz` | 2026-11-24 |

Approval of W2 is one precondition of the G0 decision. It is not the gate
decision and does not by itself change any criterion status.

## 4. Signature record R3 — G0 gate decision

The chair recorded the one-page decision for the three G0 signatories. The
decision is **GO-WITH-WAIVERS (W1, W2)**; **GO** was not available while G0.3's
pattern leg and G0.5's broader manifest scope were unproven, and **STOP** was not
selected.

Recorded decision statement:

> G0 decision: **GO-WITH-WAIVERS (W1, W2)**, signed 2026-11-24 by Gridworkz for
> the DFT lead, architect and verification lead roles. The evidence is the G0
> gate pack pinned at `2649fe8`; G0.1, G0.2, G0.4, G0.6 and G0.7 are met; G0.3 is
> met on the coverage leg with its pattern leg waived by W1; G0.5 is met for
> ATPG runs with the remaining manifest scope waived by W2. The decision is
> effective 2026-11-24. It does not authorize WP-101; a separate written start
> authorization is required after TBD-03 and a qualifying library are accepted.

| Role | Name | Decision (GO-WITH-WAIVERS / GO / STOP) | Signature | Date |
|---|---|---|---|---|
| DFT lead (chair) | Gridworkz | GO-WITH-WAIVERS (W1, W2) | `/s/ Gridworkz` | 2026-11-24 |
| Architect | Gridworkz | GO-WITH-WAIVERS (W1, W2) | `/s/ Gridworkz` | 2026-11-24 |
| Verification lead | Gridworkz | GO-WITH-WAIVERS (W1, W2) | `/s/ Gridworkz` | 2026-11-24 |

The decision and signatures are recorded in the gate pack. They do not close
G0-D04 or G0-D05, and they do not start WP-101.

## 5. Intake request R4 — TBD-03 scan spec

| Field | Value |
|---|---|
| Input | Scan spec: chains, clocks, scan enable, test-mode pins and related insertion constraints |
| Owner | DFT |
| Needed by | M1; before WP-101 and WP-102 start |
| Blocks | WP-101, WP-102 (plan section 13); G0-D04 |
| Record | A frozen scan-spec file plus its SHA-256, linked from the G100 profile and the G0 pack |

TBD-03 is one frozen artifact. WP-101 consumes the scan style, scan-enable,
clock, test-mode and cell-scope fields; WP-102 consumes the chain, lockup,
clock-mixing and scan-I/O fields. The whole spec must be frozen before either
package starts. The capture procedure is TBD-06 and is not part of this request.

### 5.1 Required content

No field may be blank, `TBD`, or an empty list. A value that cannot yet be final
must be marked `provisional: true` with the decision that will replace it and
the package it blocks.

| # | Field | Exact requirement |
|---|---|---|
| 1 | Chip, block, profile | `chip`, block name and profile path. The block must be the G100 bring-up block (TBD-10) or an explicitly labelled provisional substitute. |
| 2 | Scan style | `mux_d`, or a dedicated scan-flop path where the library provides one. If hybrid, state which cell types use which path; LSSD is out of scope. |
| 3 | Sequential-cell scope | Scannable sequential cell types; latches excluded, bypassed or reported as a DRC error; black-box instances listed; no unclassified sequential instance. |
| 4 | Chain plan | Target chain count, target and maximum chain length, maximum length skew (default 5%), and whether the count is final or provisional pending TBD-04. |
| 5 | Clock plan | Test clock names and top-level ports; clock domains and compatible groups; shift versus capture role; generated-clock relationships; reference to the SDC or clock table with its path and hash. |
| 6 | Scan enable | SE port name or names; active level; functional/capture value (**must be 0 for the G1.3 `SE=0` proof**); shared per compatible group, per domain, or single; reset/test-mode default. |
| 7 | Test-mode pins | Test-mode port name, direction, active level, default, and its relationship to SE. Every test-mode control needed by insertion must be reachable from the top level. |
| 8 | Scan I/O | Scan-in and scan-out port names, counts and directions; bit order and numbering convention (1-based from scan-in unless explicitly stated otherwise); chain count must not exceed the available pins. |
| 9 | Lockup and clock mixing | Lockup policy (`auto`, `always`, `never`) and insertion points at domain crossings or segment boundaries; incompatible clock groups that may never share a chain. |
| 10 | Async and gated controls | Async set/reset pins and groups, with the test-mode value or bypass that controls them; gated-clock bypass method and ports. Uncontrolled async or gated clocks are errors, not defaults. |
| 11 | Power domains | Power-domain boundaries for chain partitioning, or an explicit single-domain statement. |
| 12 | Chain order | Deterministic order and tie-break rules for cells within a chain, and the scan-map bit numbering convention. |

### 5.2 Delivery format and acceptance

Deliver one file at `profiles/g100/<block>-scan.yaml` (or record the actual
immutable path), using the architecture section 3 `scan:` and `constraints:`
keys, plus the signed table for values that do not fit those keys. Record the
file's SHA-256 and the block profile hash.

Acceptance requires:

1. The DFT lead signs the scan spec; Design confirms every clock, test-mode and
   scan-I/O port name against the netlist.
2. Every field above is present; no value is `TBD` or blank; every provisional
   value names the decision that will replace it.
3. The WP-101 lead confirms the spec contains every value needed to map
   sequential cells to scan equivalents and to produce the `SE=0` netlist.
4. G0-D04 closes only when the accepted spec's path and hash are recorded in the
   gate pack.

| Role | Name | Accept / reject | Signature | Date |
|---|---|---|---|---|
| DFT lead (owner) | | | | |
| Design (port/clock confirmation) | | | | |
| WP-101 lead (consumability) | | | | |

## 6. Intake request R5 — libraries and netlists with `test_cell`

| Field | Value |
|---|---|
| Input | TBD-02: G100 bring-up netlist and Liberty libraries with scan-cell definitions. TBD-01: grx930 netlist and libraries. |
| Owner | Design (delivery); DFT (intake audit and scan mapping) |
| Needed by | M1; before real-library insertion and the G1.3 equivalence proof |
| Blocks | G0-D05; WP-101 completion; G1.1/G1.3 |
| Record | One immutable library/netlist package with per-file SHA-256 and a signed scan-cell mapping table |

The first WP-101 insertion proof runs on the G100 bring-up block and therefore
needs the **TBD-02 G100 set**. The grx930 set (TBD-01) is required before
G0-D05 can close and before the grx930 profile is frozen; it is not required for
the first G100 insertion trial.

### 6.1 Required files

| ID | File set | Minimum content | Needed for |
|---|---|---|---|
| TBD-02a | G100 gate-level netlist(s) | Verilog for each mode, top module name, black-box declarations, resolved cell references | Real insertion and `SE=0` equivalence |
| TBD-02b | G100 Liberty set | Every library and functional corner used by the block, including a separate scan library if used; plain `.lib`; all cells instantiated by the netlist | Cell mapping, `test_cell`, function checks |
| TBD-02c | G100 scan-cell mapping | One row per sequential cell type using the columns in 6.2 | WP-101 insertion |
| TBD-01 | grx930 netlist and Liberty set | Same content as TBD-02a/TBD-02b, plus the grx930 scan mapping | grx930 profile; G0-D05 closure |

If a library has multiple corners, state which corner is authoritative for
function and scan and provide hashes for every supplied file. Timing arcs are
retained but ignored by v1 ATPG (architecture 7.2).

### 6.2 Liberty and mapping requirements

The Liberty set must support the architecture 7.2 subset: `cell`, `area`, `pin`,
`direction`, `function`, `three_state`, `clock`, `next_state`, `clear`,
`preset`, `ff`/`latch`, `statetable` and `test_cell`. The scan-cell mapping table
must have one row per sequential cell type:

| Library cell | Sequential type | Data pin | Clock pin | Enable pin | Async set/reset pins | Scan data pin | Scan enable pin | Scan out pin | Active levels | Scan equivalent cell | Source (`test_cell` / fallback) |
|---|---|---|---|---|---|---|---|---|---|---|---|
| | | | | | | | | | | | |

If a scan equivalent has a separate scan clock, add that pin and its active
edge to the mapping row.

Requirements:

1. Every sequential cell that must be scanned has a `test_cell` group or a
   documented fallback attribute/naming convention. A fallback emits the
   acknowledgement warning required by architecture 7.2 and plan WP-101; the
   acknowledgement must be recorded in the profile.
2. A dedicated scan flop uses its scan pins directly. A mux-D flow must have the
   mux primitive or a supported primitive construction, and must not silently
   substitute a different scan style.
3. Data, clock, enable and asynchronous behavior is preserved. Uncontrolled
   async pins, latches in the scan path and undefined clock connections are
   explicit errors, not inferred defaults.
4. Every combinational cell with at most six inputs passes the exhaustive
   `function` check; a mismatch is a hard error.
5. Cell names in the mapping table match the netlist and Liberty exactly. No
   unversioned library drop is accepted.

### 6.3 Acceptance checks

1. The WP-003 Liberty reader parses the set; every netlist instance resolves to a
   library cell; no unsupported construct is silently dropped.
2. Every sequential instance is either mapped to a scan equivalent or listed in
   an approved exclusion (latch, black box, non-scan macro).
3. Every scan candidate has `test_cell` or an acknowledged fallback; the
   acknowledgement is present in the profile.
4. The ≤ 6-input exhaustive function check passes; any mismatch fails intake.
5. A trial insertion on the G100 block produces an `SE=0` netlist; Yosys
   `equiv_make` + `equiv_simple` (ADR-009 choice) or ABC `cec` reports
   equivalence. Any mismatch fails intake.
6. WP-101's structural checks pass: every scan cell has a scan enable and a
   defined test clock, no latch is in the scan path, and no async control is
   uncontrolled.
7. The package records per-file SHA-256, library version/corner and the signed
   mapping table. G0-D05 closes only after the TBD-02 set passes for G100 and
   the TBD-01 set passes for grx930.

| Role | Name | Accept / reject | Signature | Date |
|---|---|---|---|---|
| Design owner | | | | |
| DFT intake owner | | | | |
| WP-101 lead (trial insertion) | | | | |

## 7. WP-101 start checklist

Plan section 9 gives WP-101 only G0 as a package dependency, and plan section 13
adds TBD-03 as a start input. The library is not a formal WP-101 dependency,
but its real-insertion deliverable and the G1.3 equivalence proof cannot be
completed without one. This package therefore treats the G100 library set as a
start condition for real insertion, not for the narrow library-independent
slice.

WP-101 may start only when every condition below is satisfied and recorded in a
written start authorization. The signed gate decision does not supply that
authorization.

| Condition | Required before formal WP-101 start? | Recorded in |
|---|---|---|
| G0 decision signed by DFT lead, architect and verification lead | Signed 2026-11-24 | Gate pack section 7 |
| W1 signed by the architecture owner | Signed 2026-11-24 | Gate pack section 5 |
| W2 signed by the architecture owner | Signed 2026-11-24 | Gate pack section 5 |
| TBD-03 scan spec accepted and hashed | Yes; plan section 13 makes it a WP-101 start input | Gate pack G0-D04 closure |
| G100 netlist, Liberty set and scan mapping accepted | Yes for real insertion and G1.3; no for a library-independent slice only if the gate decision explicitly records a written intake risk acceptance | Gate pack G0-D05 / intake record |
| grx930 netlist and Liberty set accepted | No for the first G100 insertion trial; yes before G0-D05 closes and the grx930 profile freezes | Intake record |
| G100 bring-up block (TBD-10) named | Not a formal WP-101 dependency; needed to make the TBD-03 scan spec and the G100 trial block-specific (plan lists TBD-10 against WP-111/G1) | Profile and TBD-03 |
| TBD-06 capture procedure | No; blocks WP-201/G2.6 | TBD-06 record |
| TBD-07 X-source policy | No; blocks WP-209. Black-box declarations still come with the netlist | TBD-07 record |
| TBD-04 ATE platform and pin map | No; chain count may be provisional, but must be re-frozen before WP-211 | TBD-04 record |
| Separate written WP-101 start authorization | Yes | Gate pack or a dated companion record |

Until the required rows are satisfied, the only permissible WP-101 activity is
the library-independent slice named in the gate pack — `test_cell` scan-cell
detection with the acknowledgement warning, the ≤ 6-input exhaustive function
check, and mux-D scaffolding — and that slice still requires the gate decision
to authorize it in writing. Real insertion and the G1.3 equivalence proof wait
for TBD-03 and the G100 library set.

## 8. Recording and effect

- W1 and W2 were signed on the tables in sections 2 and 3 on 2026-11-24 by
  Gridworkz, the single operator. That signature is the approval required by plan
  section 15 for a narrowed criterion, and it is recorded in the gate pack.
- The gate decision was signed on the table in section 4 and recorded in the gate
  pack on 2026-11-24.
- TBD-03 acceptance is recorded with the file path, SHA-256 and the signatures in
  section 5.2; that closes G0-D04.
- The library package is recorded with per-file SHA-256 and the signatures in
  section 6.3. G0-D05 closes only after both chip sets pass.
- A WP-101 start authorization is a separate dated record. It is not implied by
  any signature in this package.

**Current effect: the G0 decision is signed GO-WITH-WAIVERS (W1, W2). G0 is not
recorded as met without waivers; G0-D04 and G0-D05 remain open, and WP-101 has
not started.**
