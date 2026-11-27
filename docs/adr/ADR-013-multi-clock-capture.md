# ADR-013: Multi-clock capture and constrained capture coverage

- Status: Proposed
- Date: 2026-11-26
- Context: WP-201; architecture 7.3, 8.3 and 9.7; successor to the deferral
  in ADR-012 section 4

## Context

ADR-012 attaches clocks, clock groups, generated clocks and false paths to a
database, and `run_atpg` enforces one of them: a complete `set_case_analysis`
set on top-level inputs. Everything else is validated, recorded and reported as
`capture_constraints: not-enforced`, because the engine is a combinational
stuck-at engine and never pulses a clock.

WP-201 adds the capture model. Architecture 9.7 fixes its semantics: load,
capture, unload, compare; clocks in different groups are not pulsed together
unless explicitly allowed; sequential capture requires the intermediate state to
be scanned or X-masked; test-mode gaters are bypassed and asynchronous resets are
held inactive. Architecture 7.3 requires multi-clock timing sets per capture
procedure and forbids implicit simultaneous capture. Architecture 8.3 already
defines DFT003 (crossing without a lockup latch) and DFT019 (false path hides a
capture clock).

What was missing is the decision that makes those implementable together: what a
procedure is, when a run may claim it applied one, which report fields carry the
claim, and what is refused instead of approximated. Without it, WP-201 would
invent a report shape that ADR-012's hash contract cannot absorb.

This ADR supersedes three specific claims in ADR-012 and nothing else: the
`capture_constraints: not-enforced` report in section 4 item 2, the enforced set
`case_analysis` in section 4 item 4, and the deferral in the paragraph after item
5. Section 2 (the canonical constraint payload) and section 3 (completeness) are
unchanged and are not reinterpreted here.

## Decision

### 1. A capture procedure is an ordered list of capture cycles

A **clock** is a declared SDC clock, primary or generated. A **group** is a
`set_clock_groups` group; a clock with no declared group is its own group, so
undeclared clocks are never simultaneous. A **cycle** pulses one or more groups
at that cycle's edge times. A clock appears in exactly one group of exactly one
cycle.

Three kinds are defined, and no others:

- **simultaneous**: one cycle containing every capture clock in one group. Legal
  only when the SDC declares those clocks in one group, so the procedure cannot
  pulse clocks that `set_clock_groups` puts in different groups.
- **grouped**: one cycle with two or more groups at different edge times. Legal
  when every ordered group pair has a lockup latch or an excusing false path
  (section 5).
- **sequential**: two or more cycles, one group each, in order. Legal only when
  every domain captured before the last cycle is scanned or masked (section 2).

The profile (TBD-06) chooses the kind and the order; the SDC decides which
clocks exist, their groups and their waveforms. When the profile declares no
procedure, the engine derives one and marks it `derived`: sequential, one
single-clock cycle per clock, ordered by clock name. Transition faults, at-speed
launch and capture pairs, and compression-aware masking are out of scope; they
extend this list later rather than adding a fourth kind.

### 2. Intermediate state: scanned or masked

In a sequential procedure, a domain captured before the last cycle holds a value
later cycles read. That value is usable in exactly two ways:

- **scanned**: every state-holding element that holds the intermediate value is
  a scan cell, so the value is both controllable and observable through the
  chains. A partially scanned domain does not qualify.
- **masked**: the procedure marks that domain's intermediate state masked. Every
  observation that depends on it becomes X at the earliest legal point, and the
  faults that needed it are counted as `masked`. `masked` is a subset of
  `untestable` in the existing denominators, so the coverage formulas in
  architecture 9.9 are unchanged, while the count and the per-fault list
  distinguish "cannot be tested" from "chose to mask" as 9.7 requires.

Anything else is refused (section 4). A mask is recorded in the pattern IR
(ADR-010) on the cycle and observation it applies to, so replay reproduces it
exactly.

### 3. Per-group timing sets

Each group of a cycle contributes one timing set, and the pattern IR carries one
per cycle. A timing set names the group's clocks, each clock's period, rise and
fall from the SDC waveform or the profile override, and the group's edge time
inside the cycle.

- Edge times come from the profile. When it gives none, groups are one slowest
  period apart inside the cycle, which is deterministic and keeps each pulse in
  its own cycle.
- A group whose clocks disagree on period or waveform is refused (section 4).
- A clock that clocks no state-holding gate in the design is reported and left
  out of the cycles; if no clock remains, the run is refused (section 4).
- A gated clock whose test-mode bypass is not modelled, or an asynchronous reset
  the procedure does not drive, makes that domain untestable rather than
  captured, as 9.7 requires. The run continues, the domain is reported, and its
  clock stays in the procedure (section 4).

### 4. Refusals

A refusal is a run-level failure: print the finding, exit 2, write no coverage
and no manifest. It happens after ADR-012's completeness check and before
grading, so an incomplete set still fails with its own warnings first. A refusal
never rewrites the database and never changes the constraint status: the set
stays complete or absent exactly as attached.

The conditions are exactly these, each with its own message naming the clock,
group or cycle:

1. simultaneous capture of clocks that are not in one group;
2. a sequential procedure whose intermediate state is neither scanned nor
   masked;
3. a group whose clocks disagree on period or waveform;
4. a clock that appears more than once in the procedure;
5. no capture clock left after section 3's clock-level exclusion;
6. a DFT003 error (section 5).

Condition 5 counts only the clock-level exclusion in section 3. The domain-level
one never refuses: an unmodelled gated-clock bypass or an undriven asynchronous
reset keeps its clock in the procedure, reports the domain untestable, and the
rest of the procedure runs. A run whose domains are all excluded that way is
still `enforced`, because the procedure was applied and its faults are counted
`untestable`, not refused: its procedure is well formed.

A malformed procedure is condition 4 or an unreadable profile; it is not a new
category.

### 5. DFT003 and DFT019 at capture time

- **DFT003** is evaluated for every ordered pair of groups in the same cycle, and
  for every sequential pair where the earlier domain feeds the later one across
  a clock boundary. It passes with a lockup latch, or with a `set_false_path`
  between the two clocks that is not itself a DFT019 finding. Anything else is
  an error and refuses the procedure (section 4).
- **DFT019** fires when both endpoints of a false path are clocks the procedure
  pulses in different groups or cycles, because that path removes exactly the
  capture edge the procedure creates. It is a warning naming both clocks and the
  path's line, recorded in the DRC report and repeated in the run manifest; the
  procedure still runs. A path that names a pulsed clock on one side only is not
  evaluated, because the constraint model cannot tell whether it hides that
  clock's capture edge. Only clock endpoints count, which is why false-path
  endpoints record whether they came from a clock query.

A waiver may accept a DFT019 finding. It cannot make a refused procedure run,
because that would change the coverage claim rather than the review status.

### 6. Report, identity, and what moves off `not-enforced`

The coverage and manifest reports gain one `capture` object inside the existing
`constraints` object, replacing the `capture_constraints` string:

- `status`: `absent` when there is no capture claim (no constraints section, or
  no capture clock), or `enforced` when a procedure ran and its effects are in
  the coverage, independent of `constraints.status`. There is no third status:
  a one-clock run is `enforced` with `procedure: simultaneous` and one clock,
  and a refused procedure writes no report at all.
- `procedure`: `simultaneous`, `grouped` or `sequential`.
- `derived`: whether the profile declared the procedure or the engine derived
  it.
- `hash`: CRC32 of the canonical procedure payload.
- In the manifest only, `cycles`: the canonical procedure, one entry per cycle
  with its groups, clocks and edge times.

The procedure payload is canonical: cycles in order, groups in edge order,
clocks sorted bytewise within a group, numeric fields in one canonical decimal
encoding, and one `intermediate <cycle> <clock> <state>` line per clock, sorted
by clock name, for each domain a non-final cycle captures. The state is keyed by
the captured domain because that is the domain a later cycle reads; a domain the
cycle leaves unpulsed is not intermediate at that cycle and is not written, and
the final cycle writes no such line. It excludes source paths, comments,
whitespace, line and column numbers, resolved database IDs, and the `derived`
flag, which is provenance rather than procedure. Two procedures that differ only
in those respects have the same `capture.hash`.

`constraints_hash` does not change. A procedure is how constraints are used, not
a constraint, so it stays out of the payload in ADR-012 section 2; every
existing 1.4 hash remains byte-identical and a re-saved database is unchanged.
The capture identity is the pair of hashes: same constraints and same procedure
must reproduce the same coverage bytes.

`constraints.enforced` is computed from the run, never from the database:
`case_analysis` as today, plus `clock_groups` and `false_paths` when the run
applied them. Generated clocks resolve domains and appear in the procedure, but
they are not an enforcement claim. A run that refuses writes nothing, so it
cannot claim enforcement.

The coverage and manifest schema version is incremented once, when the object
lands. A reader that does not know `capture` treats the run as unconstrained
capture, which is the conservative direction. `set_input_delay`,
`set_output_delay`, `set_driving_cell` and `set_load` remain recorded and
unenforced: capture here is edge-based and does not evaluate delay annotations.

## Consequences

- WP-201 has a fixed procedure model, a fixed refusal list and a fixed report
  shape, so its DRC hooks can be mutation-tested against an input that does not
  move.
- ADR-012's identity contract survives: no existing hash changes, and capturing
  a design does not make it a different constraint set.
- A reader can tell an unconstrained run from one that applied a procedure
  without reading logs, and a refused procedure is an exit code rather than a
  claim in a report.
- Sequential capture is gated on scan observability or an explicit mask, so the
  engine cannot report coverage for state it never observed.
- Cost: a procedure is another input that can be wrong, refusal is a new exit
  condition for `run_atpg`, and TBD-06 must be decided before grouped or
  sequential capture is claimed.

## Test obligations

- A golden `constraints_hash` set from the current 1.4 fixtures is compared
  before and after the capture engine lands; every existing value must match
  byte for byte.
- Report tests for `absent` and `enforced`, the procedure kind and `derived`
  flag, the capture hash, `masked` in the coverage counts, and `cycles` in the
  manifest only, with the schema version asserted.
- One refusal test per condition in section 4, each asserting exit code 2 and
  that no coverage or manifest is written; an incomplete set still fails with
  its own warnings first.
- An exclusion test per granularity: a clock that clocks no state-holding
  element is reported and dropped and refuses only when it empties the list; a
  domain excluded by an unmodelled gated clock or an undriven reset is reported
  untestable and the run completes with `enforced`.
- Intermediate-state tests: a fully scanned domain runs; a masked domain counts
  its faults as masked and those faults stay inside `untestable`; with neither,
  the run is refused.
- Determinism: the same design, faults, constraints and procedure produce
  byte-identical coverage and manifest across repeated runs and across thread
  counts (ADR-011).
- DRC mutation tests: a crossing without a lockup latch fires DFT003 and refuses
  the procedure; a false path between two pulsed clocks fires DFT019 and the run
  completes; a one-sided path does not fire; an expired waiver fails the run.
- Replay: patterns written for a grouped and for a sequential procedure replay
  through the IR with the recorded per-cycle timing sets and masks.
- The enforced list never names an effect the run did not apply: a single-group
  run reports no `clock_groups`, and a run with no constraints does not report
  `case_analysis`.

## References

- `docs/adr/ADR-012-sdc-constraint-integration.md`, sections 2, 3 and 4.
- `docs/adr/ADR-010-pattern-ir.md` (per-cycle timing sets and masks) and
  `docs/adr/ADR-011-deterministic-threading.md` (repeat-run equality).
- `dft-atpg-platform-architecture-grx930-g100.md`, 7.3, 8.3, 9.7 and 9.9.
- `dft-atpg-implementation-plan.md`, WP-201, WP-203, WP-208 and WP-209, TBD-06
  and TBD-07.
