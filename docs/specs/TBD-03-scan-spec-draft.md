# TBD-03 — G100 scan specification (DRAFT)

- Status: **DRAFT — not frozen, not accepted.**
- Gate effect: **G0-D04 remains open. WP-101 implementation remains blocked.**
- Owner: DFT.
- Needed by: M1; before WP-101 and WP-102 start (plan section 13).
- Requirement source: `docs/gates/G0-sign-off-package.md` section 5, merged at
  `71f2523`; package blob SHA-256
  `4175dd97e25f24678dff4837779bbb40566c5c16e01541331143f4d4e6b84139`.
- Evidence baseline: gate pack reviewed commit
  `2649fe8cf81b574fa1f3c8f5bb30c7c84d0c2dc8`; plan v0.9
  `sha256:7862006e8ff8a3a54218a37c2f990e749cc8674da09ef9c74d1d83c746b06bef`;
  architecture baseline blob `d9ae07254b87e5b3b5e2b41672613ac8fee340c9`.
- Target artifact after freeze: `profiles/g100/<block>-scan.yaml`, with the
  block selected by TBD-10.

> This is an intake draft, not a frozen scan spec. Unknown G100 values are left
> deliberately unresolved. It must not be loaded by insertion, treated as TBD-03
> acceptance, used to close G0-D04, or used to start WP-101.

## 1. Scope and consumers

TBD-03 is one frozen artifact. WP-101 consumes the scan style, scan-enable,
clock, test-mode and cell-scope fields. WP-102 consumes the chain, lockup,
clock-mixing and scan-I/O fields. The whole spec must be frozen before either
package starts. The capture procedure is TBD-06 and is not part of this draft.

The draft follows the twelve required fields in package section 5.1. Values that
do not fit the architecture section 3 `scan:` and `constraints:` keys are listed
in the signed-table section; the package requires those values in the same frozen
file.

### 1.1 Exact package rules carried into this draft

- No accepted field may be blank, `TBD`, or an empty list. A value that cannot be
  final must be marked `provisional: true`, name the decision that will replace
  it, and name the package it blocks.
- Scan style is `mux_d`, or a dedicated scan-flop path where the library provides
  one. A hybrid must state which cell types use which path. LSSD is out of scope.
- Latches are excluded, bypassed, or reported as a DRC error. Black-box instances
  are listed. No sequential instance may remain unclassified.
- The chain count is final or provisional pending TBD-04. The default maximum
  length skew is 5%.
- Scan enable's functional/capture value is **0** for the G1.3 `SE=0` proof.
- Scan-in/out bit order is **1-based from scan-in** unless the spec explicitly
  states otherwise.
- Lockup policy is `auto`, `always`, or `never`; incompatible clock groups may
  never share a chain.
- Uncontrolled asynchronous controls or gated clocks are errors, not defaults.
- The chain count must not exceed the available scan I/O pins.
- Acceptance requires the DFT lead signature, Design confirmation of every
  clock/test-mode/scan-I/O port name, and WP-101 lead confirmation of
  consumability. G0-D04 closes only when the accepted path and hash are recorded
  in the gate pack.

## 2. Field-by-field draft

### 2.1 Chip, block and profile

**Requirement.** `chip`, block name and profile path. The block must be the G100
bring-up block (TBD-10), or an explicitly labelled provisional substitute.

**Draft.** `chip: G100`. Block and top module: **UNRESOLVED** (TBD-10 and
TBD-02a). Target path: `profiles/g100/<block>-scan.yaml`; no file is created by
this draft.

**State.** BLOCKED — TBD-10.

**Resolution.** DFT names the bring-up block; Design confirms the top module.
No provisional substitute is selected.

### 2.2 Scan style

**Requirement.** `mux_d`, or a dedicated scan-flop path where the library
provides one. A hybrid must state which cell types use which path. LSSD is out
of scope.

**Draft.** `mux_d` is the working assumption from architecture sections 1 and
8.1. A dedicated path is not confirmed: it depends on the G100 Liberty
`test_cell` set. No hybrid mapping is proposed.

**State.** PROVISIONAL — TBD-02b and G0-D05.

**Resolution.** Audit the G100 Liberty set. If dedicated scan flops exist, name
the cell types and switch those types off the mux-D path.

### 2.3 Sequential-cell scope

**Requirement.** Scannable sequential cell types; latches excluded, bypassed or
reported as a DRC error; black-box instances listed; no unclassified sequential
instance.

**Draft.** Policy is fixed: scan flops, exclude/bypass latches, list black boxes,
and reject unclassified sequential cells. The actual G100 cell-type list and
black-box inventory are **UNRESOLVED** (TBD-02a, TBD-02b, TBD-02c).

**State.** BLOCKED — TBD-02a/TBD-02b/TBD-02c.

**Resolution.** Reconcile the netlist against Liberty and the scan-cell mapping
table. Any cell without a mapping row blocks acceptance.

### 2.4 Chain plan

**Requirement.** Target chain count, target and maximum chain length, maximum
length skew (default 5%), and whether the count is final or provisional pending
TBD-04.

**Draft.** Target count, target length and maximum length: **UNRESOLVED**.
Maximum skew: **5% (default, provisional)**. The count is **provisional** pending
TBD-04. TBD-05 may revise the target count when compression is decided at M3.
The count must not exceed the available scan I/O pins. The architecture example
values (64 chains, 2500-bit target length) are illustrative and are **not**
adopted here.

**State.** BLOCKED — TBD-04 and DFT; TBD-05 is a later change risk.

**Resolution.** DFT sets count and lengths from the ATE pin budget and block
size; Design/DFT confirm the 5% skew or replace it.

### 2.5 Clock plan

**Requirement.** Test clock names and top-level ports; clock domains and
compatible groups; shift versus capture role; generated-clock relationships; a
reference to the SDC or clock table with its path and hash.

**Draft.** Test clock names/ports, domains, compatible groups, shift/capture
roles, generated-clock relationships, and the SDC path/hash are **UNRESOLVED**.
No clock frequency, edge, or timing relationship is assumed.

**State.** BLOCKED — Design/DFT SDC intake. TBD-06 defines the capture procedure
and is separate; it is not specified here.

**Resolution.** Add the frozen SDC/clock-table path and SHA-256; enumerate every
test clock and top-level port; state shift and capture roles; list generated
clocks and compatible groups.

### 2.6 Scan enable

**Requirement.** SE port name or names; active level; functional/capture value
(**must be 0 for the G1.3 `SE=0` proof**); shared per compatible group, per
domain, or single; reset/test-mode default.

**Draft.** SE port(s), active level, sharing scope, and reset/test-mode default
are **UNRESOLVED**. The functional/capture value is **0 — fixed**.

**State.** BLOCKED — Design port confirmation; the capture value is fixed.

**Resolution.** Confirm every SE port and active level at the top level; choose
single, per-compatible-group, or per-domain sharing; state the reset/test-mode
default. The `SE=0` value may not be changed by a later revision.

### 2.7 Test-mode pins

**Requirement.** Test-mode port name, direction, active level, default, and its
relationship to SE. Every test-mode control needed by insertion must be
reachable from the top level.

**Draft.** Port name, direction, active level, default, and SE relationship are
**UNRESOLVED**. Top-level reachability is **UNVERIFIED**.

**State.** BLOCKED — TBD-02a and Design.

**Resolution.** Confirm the complete test-mode pin list against the netlist, and
prove each control is reachable from a top-level port.

### 2.8 Scan I/O

**Requirement.** Scan-in and scan-out port names, counts and directions; bit
order and numbering convention (1-based from scan-in unless explicitly stated
otherwise); the chain count must not exceed the available pins.

**Draft.** Scan-in/out names, counts and directions are **UNRESOLVED**. Bit
order is **1-based from scan-in** unless DFT explicitly records a different
convention. The pin-budget constraint is fixed: chain count ≤ available scan-in
pins and ≤ available scan-out pins.

**State.** BLOCKED — TBD-04 and TBD-02a.

**Resolution.** Freeze the ATE/pin map and confirm the scan I/O ports against the
netlist. If the ATE convention differs, state it and the mapping explicitly.

### 2.9 Lockup and clock mixing

**Requirement.** Lockup policy (`auto`, `always`, `never`) and insertion points
at domain crossings or segment boundaries; incompatible clock groups that may
never share a chain.

**Draft.** Lockup policy: **`auto` (provisional)**. Insertion points and
incompatible groups are **UNRESOLVED**. No domain may share a chain until its
compatibility is recorded.

**State.** PROVISIONAL — DFT and the SDC clock plan.

**Resolution.** Confirm `auto`, `always`, or `never`; list lockup insertion
points; enumerate incompatible groups and enforce them in stitching.

### 2.10 Asynchronous and gated controls

**Requirement.** Async set/reset pins and groups, with the test-mode value or
bypass that controls them; gated-clock bypass method and ports. Uncontrolled
async pins or gated clocks are errors, not defaults.

**Draft.** Async groups/pins, their test-mode values/bypasses, and the gated-clock
bypass method/ports are **UNRESOLVED**. The error rule is fixed: an uncontrolled
async control or gated clock fails insertion DRC.

**State.** BLOCKED — TBD-02a/TBD-02b and DFT.

**Resolution.** Inventory every async control and gated clock; name the test-mode
value or bypass for each; confirm the bypass is top-level reachable.

### 2.11 Power domains

**Requirement.** Power-domain boundaries for chain partitioning, or an explicit
single-domain statement.

**Draft.** Power-domain boundaries and a single-domain statement are both
**UNRESOLVED**. No UPF file is assumed: full UPF-aware ATPG is outside v1, so the
needed boundary information must come from the netlist/design intake or be
stated explicitly.

**State.** BLOCKED — TBD-02a and Design.

**Resolution.** Provide boundaries, or sign an explicit single-domain statement.
Either answer must be deterministic and recorded before stitching.

### 2.12 Chain order and scan-map numbering

**Requirement.** Deterministic order and tie-break rules for cells within a
chain, and the scan-map bit numbering convention.

**Draft.** Proposed deterministic policy:

1. Partition by compatible clock group, then by power domain.
2. Allocate the target chain count across partitions; pack with
   first-fit-decreasing; break ties by instance name.
3. Within a chain, order by clock domain, then by physical proximity when DEF is
   available, then by instance name.
4. Assign scan-map bit positions **1-based from scan-in**; assign chain IDs in
   the same deterministic partition order.

Physical proximity is unavailable in v1 (TBD-12 arrives at M13), so name order
is the current tie-break. The policy is deterministic but not yet accepted.

**State.** PROVISIONAL — DFT acceptance; TBD-12 reopens physical-aware ordering.

**Resolution.** DFT accepts or replaces the policy before freeze. If DEF is
introduced later, the change must preserve deterministic tie-breaks and be
recorded as a spec revision.

## 3. Profile skeleton (not loadable)

The frozen artifact must use the architecture section 3 `scan:` and
`constraints:` keys. This skeleton is illustrative only; `null` means unresolved
and must not be read as a default. Do not copy it into `profiles/` before TBD-10
is selected and the spec is frozen.

```yaml
# DRAFT ONLY — target profiles/g100/<block>-scan.yaml after TBD-10.
chip: G100
block: null                 # TBD-10
sdc: null                   # path + SHA-256 required
scan:
  style: mux_d              # provisional; dedicated path depends on test_cell
  max_chains: null          # TBD-04; must be <= available scan I/O pins
  target_chain_length: null # DFT, with maximum length, not the example value
  lockup: auto              # provisional
  scan_enable: null         # unresolved shape; see signed table
  test_clocks: null         # names/ports/roles/generated relationships
  async_controls: null      # groups + test-mode value/bypass
constraints:
  test_mode_ports: null     # name/direction/active/default/SE relationship
  x_sources: null           # TBD-07; not invented here
```

### 3.1 Signed-table values not covered by section 3 keys

The package requires these values in the same frozen file, in a signed table:

| Value | Draft state |
|---|---|
| Block, top module, profile path | UNRESOLVED — TBD-10/TBD-02a |
| Scannable cell types, latches, black boxes, unclassified-cell rule | Policy fixed; list UNRESOLVED — TBD-02a/b/c |
| Maximum chain length and maximum length skew | Length UNRESOLVED; skew 5% provisional |
| SE port(s), active level, sharing, reset/test-mode default | UNRESOLVED; capture value 0 fixed |
| Test-mode port details and SE relationship | UNRESOLVED — TBD-02a/Design |
| Scan-in/out ports, counts, directions, bit order | UNRESOLVED; 1-based default fixed |
| Clock domains, compatible groups, shift/capture, generated clocks, SDC hash | UNRESOLVED — Design/DFT |
| Lockup points and incompatible clock groups | UNRESOLVED — DFT/SDC |
| Gated-clock bypass method and ports | UNRESOLVED — TBD-02a/b |
| Power-domain boundaries or single-domain statement | UNRESOLVED — TBD-02a/Design |
| Chain order, tie-breaks, chain IDs, scan-map bit numbering | Policy provisional; bit order fixed |
| `constraints.x_sources` | UNRESOLVED — TBD-07 |

## 4. Unresolved G100 assumptions

Every row is an assumption or missing G100 input that the draft does **not**
resolve. A row marked BLOCKED prevents TBD-03 freeze; a row marked DEFERRED does
not, but must be recorded so it is not mistaken for a frozen value.

| ID | Assumption or missing input | State | Resolve via | Effect on this spec |
|---|---|---|---|---|
| A01 | The G100 bring-up block and top module are TBD-10/TBD-02a; no provisional substitute is named. | BLOCKED | TBD-10, TBD-02a | Field 2.1; profile path and all cell references |
| A02 | The netlist modes, black-box list, resolved cell references and hash are not available. | BLOCKED | TBD-02a | Fields 2.1, 2.3, 2.10 |
| A03 | The G100 Liberty set, authoritative corner, scan library and `test_cell` definitions are not available; G0-D05 is open. | BLOCKED | TBD-02b, G0-D05 | Fields 2.2, 2.3, 2.10; mux-D versus dedicated path |
| A04 | No G100 scan-cell mapping table exists yet. | BLOCKED | TBD-02c | Field 2.3; cell-type mapping |
| A05 | The SDC or clock table, its path/hash, clock domains, generated clocks and shift/capture roles are not available. | BLOCKED | Design/DFT intake; TBD-06 remains separate | Field 2.5 |
| A06 | SE and test-mode port names, active levels, defaults and top-level reachability are unverified. | BLOCKED | TBD-02a, Design | Fields 2.6, 2.7 |
| A07 | The ATE pin map and available scan I/O pins are not available. | BLOCKED | TBD-04 | Fields 2.4, 2.8; chain count and I/O width |
| A08 | Target chain count, target/maximum length and final skew are not set; 5% is only a provisional default. | BLOCKED | DFT, TBD-04; TBD-05 may revise | Field 2.4 |
| A09 | Async set/reset groups and gated-clock inventory/bypasses are not available. | BLOCKED | TBD-02a/b, DFT | Field 2.10 |
| A10 | Power-domain boundaries are not available; no explicit single-domain statement is signed, and no UPF is assumed. | BLOCKED | TBD-02a, Design | Field 2.11 |
| A11 | Incompatible clock groups and lockup insertion points are not defined. | BLOCKED | DFT, SDC clock plan | Field 2.9 |
| A12 | Physical placement is unavailable (`TBD-12`, M13); v1 proposes name-order tie-breaks. | DEFERRED | TBD-12 | Field 2.12; physical-aware order reopens later |
| A13 | The X-source inventory/policy is not available; `constraints.x_sources` is outside TBD-03's twelve fields. | DEFERRED | TBD-07 (M2) | Profile use for X-aware work; WP-209, not WP-101 |
| A14 | The architecture section 3 values (64 chains, 2500-bit length, `tck_a`/`tck_b`, `tsmc_n5`, `test_mode`) are illustrative, not G100 data. | CONFIRMED — not data | Architecture section 3 | All fields; these values must not be copied as decisions |
| A15 | The compression decision (TBD-05, M3) may change the target chain count after this spec freezes. | DEFERRED | TBD-05 | Field 2.4; any change reopens the spec |
| A16 | The ATE platform/tester is not selected; the profile's `tester` key remains outside this scan spec. | DEFERRED | TBD-04 | WP-211, not WP-101 |

No other G100 values are assumed beyond the provisional defaults recorded above.
In particular, this draft does **not** assume a
single clock, a single power domain, a particular scan I/O count, a dedicated
scan cell, an active level for SE, or a final chain count.

## 5. Freeze and acceptance status

This draft does **not** satisfy TBD-03 acceptance. Every BLOCKED row above must
be resolved, every `UNRESOLVED` field replaced, and every provisional value
either confirmed or replaced with a dated decision before freeze.

Acceptance requires all of the following:

1. Every field in section 2 is present and no value is blank, `TBD`, or an empty
   list; every remaining provisional value names its replacement decision and
   blocked package.
2. The DFT lead signs the frozen spec.
3. Design confirms every clock, test-mode and scan-I/O port name against the
   netlist.
4. The WP-101 lead confirms the spec contains every value needed to map
   sequential cells to scan equivalents and to produce the `SE=0` netlist.
5. The accepted path and SHA-256 are recorded in the G0 gate pack; only then may
   G0-D04 close.

### 5.1 Signatures (not signed)

| Role | Name | Accept / reject | Signature | Date |
|---|---|---|---|---|
| DFT lead (owner) | | | | |
| Design (port/clock confirmation) | | | | |
| WP-101 lead (consumability) | | | | |

No signature has been applied. The table is present only to show the required
acceptance record.

## 6. What this draft does not do

- It does not close G0-D04 or G0-D05.
- It does not authorize or start WP-101 or WP-102.
- It does not create a profile, insert scan cells, modify the netlist, or change
  source, tests, or build configuration.
- It does not define the TBD-06 capture procedure or the TBD-07 X-source policy.
- It does not select a G100 block, library, clock, pin map, or chain count by
  implication.

## 7. Next actions

1. DFT selects the G100 bring-up block (TBD-10), or records an explicitly
   labelled provisional substitute with its risk.
2. Design delivers TBD-02a/TBD-02b/TBD-02c and the SDC/clock table; DFT audits
   the Liberty `test_cell` set.
3. DFT resolves the BLOCKED rows and replaces the `UNRESOLVED` fields; Design
   confirms all port and clock names.
4. The WP-101 lead reviews consumability, then the DFT lead freezes and signs the
   artifact and records its path and SHA-256 in the gate pack.
5. A separate written WP-101 start authorization remains required. Until then,
   WP-101 stays blocked even if this document is reviewed or merged.

---

Draft date: 2026-11-24. Derived from `docs/gates/G0-sign-off-package.md` section
5 and the architecture section 3 profile keys. This document is not a gate
record and does not amend the gate pack or sign-off package.
