# Transition-Parity Protocol — P2 Parity Blocks (M9 Baseline)

**Status:** Draft for review; command order pending validation against the installed TMAX release; not committed.  
**Date:** 2026-11-22  
**Owner:** DFT lead for the TMAX baseline; Architecture for the target and decision record; Verification for the parity ledger.  
**Companion document:** `dft-atpg-platform-architecture-grx930-g100.md`, sections 14.3, 16.2, 16.5, 17.1, and 20.1.

## 1. Purpose

This protocol defines how the P2 parity blocks are used to establish the transition-delay baseline and how the M9 target is recorded.

It answers four questions:

1. What exactly is run in TetraMAX, and with which LOC/LOS assumptions?
2. How are transition coverage and pattern count compared without comparing different problems?
3. What does the M9 decision mean in practice?
4. Where and how is that decision recorded so the P3 exit gate can reference it?

The M9 decision adopts the target. It does not claim that the in-house transition engine already meets it. The in-house comparison is the P3 activity; M9 fixes the TMAX baseline and the target against which P3 will be judged.

## 2. Scope

**In scope**

- The three P2 parity blocks selected under architecture section 16.2.
- TMAX transition-delay ATPG in LOC, with LOS only where explicitly qualified.
- A frozen TMAX baseline at M9: coverage, fault states, pattern counts, and reports.
- The per-block M9 decision record and the P3 comparison method.
- Parity ledgers for every material coverage or pattern-count difference.

**Out of scope**

- Path-delay ATPG.
- Diagnosis accuracy, which has its own M9 decision in architecture section 20.1.
- Compression IP selection and compression-aware ATPG.
- ATE program generation, silicon correlation, and yield analysis.
- Any code or tool changes. This is a documentation-only protocol.

## 3. P2 parity set

The P2 parity set is the same minimum set of three representative blocks used for P2 stuck-at parity. The actual block names are frozen at M9; until then, use these role labels.

| ID | Profile from section 16.2 | Primary purpose | LOC | LOS |
|---|---|---|---|---|
| P2-A | 20–50 k gates, single clock, no analog, few memories | End-to-end flow and basic transition coverage | Required | Only if qualified |
| P2-B | 100–300 k gates, two clock domains, compression off | Multi-clock launch/capture and DRC | Required | Only if qualified |
| P2-C | 0.5–1 M gates, memories and black boxes, compression on | X handling, scale, compression impact | Required | Only if qualified |

Rules:

- The three blocks are a set. A block may not be replaced after the M9 baseline without a new decision ID.
- The full-chip block is not a P2 parity block. It may be added later as an extension, but it does not replace one of P2-A/B/C.
- At least one block must exercise multi-clock capture. P2-B is the default for that role.
- At least one block must exercise X sources and compression. P2-C is the default for that role.

## 4. Freeze package

Parity is valid only when both tools see the same problem. Freeze and SHA-256 hash the following before the M9 run:

| Item | Required content |
|---|---|
| Netlist | Gate-level, test-mode netlist actually used for insertion and ATPG |
| Libraries | Liberty and TMAX library view, including scan-cell and timing information |
| SDC | Test clocks, generated clocks, false paths, and case analysis |
| Test protocol | SPF or equivalent, including at-speed waveform tables and capture procedures |
| Scan configuration | Chain count, chain order, bit positions, lockup latches, scan-enable polarity |
| Pin map | Logical-to-physical mapping and test-mode pins |
| Fault list | Transition fault list, checkpoint policy, collapse policy, and fault count |
| TMAX environment | Version, license feature, command script, and non-default options |
| Compression state | Off or the exact frozen compression configuration |
| Machine | Host class, thread count, and memory limit |

The same hashes go into the TMAX report package and the P3 in-house run manifest. A changed input invalidates the comparison unless the change is recorded and both sides are rerun.

## 5. TMAX transition setup

### 5.1 Entry conditions

- The TetraMAX transition-delay feature is licensed. If it is not, the M9 decision is blocked; do not substitute a stuck-at run or published data.
- The block has a valid SPF/test protocol with at-speed launch and capture waveforms.
- The scan configuration is DRC-clean in TMAX for the selected mode.
- The block's scan enable is controllable in the required state for LOC, or is qualified for at-speed switching for LOS.
- The fault list and abort budget are frozen before the baseline run.

### 5.2 Common TMAX setup — through DRC

The following is an illustrative command skeleton, not a version-independent script. Command spelling and option support vary by TMAX release; freeze the exact script and report `report_settings` with the baseline.

```tcl
# Common: read, build, and initial structural/scan DRC only.
read_netlist libs/block_libs.v -library
read_netlist netlists/p2_a.v
run_build_model p2_a

set_drc protocols/p2_a_common.spf
read_sdc constraints/p2_a_functional.sdc
run_drc
```

This block stops at the common DRC. It contains no fault selection, no fault addition, and no launch/capture delay settings. If the flow has no common SPF, use the LOC SPF for this initial DRC and record that choice; do not use the LOS SPF.

Immediately after the common DRC, select the transition model without adding faults:

```tcl
set_faults -model transition
```

The LOC and LOS branches below start from this point. The common DRC is the structural/scan check; each branch's DRC is the transition-specific check. Do not move `add_faults` above either branch DRC.

Freeze and record these settings:

- TMAX version and transition license.
- Fault model: `transition`; fault list source; slow-to-rise and slow-to-fall counts.
- Common, LOC, and LOS SPF names and hashes; launch and capture waveform tables; capture procedure.
- `set_delay` launch mode and all non-default delay options.
- Scan-enable constraints; primary-input constraints; primary-output masks.
- `set_atpg` capture cycles, effort, abort limit, and merge/compression settings.
- Compression configuration, or explicitly off.
- Thread count and any parallel options.

Use a single-event capture procedure where possible: the capture procedure should contain the clock pulse and should not insert force-PI or measure-PO events between launch and capture. The TMAX documentation warns that a three-event capture procedure inserts delay between launch and capture and can weaken the transition test. If the block's SPF requires additional events, record them and use the same events in the in-house model.

### 5.3 LOC setup — primary mode

LOC is the primary mode for the M9 target because it does not require the scan enable to switch at-speed.

TMAX settings:

```tcl
set_drc protocols/p2_a_loc.spf
set_delay -launch_cycle system_clock
set_delay -common_launch_capture_clock   ;# only when launch and capture share a clock
set_delay -nopi_changes
set_delay -nopo_measures
add_po_masks -all
add_pi_constraints 0 scan_en
run_drc

# Faults are added only after the branch DRC.
add_faults -all
# Or: read_faults frozen/p2_a_transition.faults
set_atpg -capture_cycles 2
run_atpg -auto
write_patterns out/p2_a_loc.stil -format stil
write_faults out/p2_a_loc.faults -all
```

Assumptions:

- Scan enable is inactive before the launch pulse and remains inactive through capture.
- The launch and capture pulses are functional-speed clock pulses; the final scan shift is slow.
- The launch state is the state after the final scan load. The launch pulse creates the transition at the target; the capture pulse observes it.
- Primary inputs are held constant between launch and capture when `-nopi_changes` is used. The in-house flow must model the same hold.
- Primary outputs are masked between launch and capture when `add_po_masks -all` is used. The in-house flow must apply the same masks.
- Clocks in incompatible groups are not pulsed simultaneously. Multi-clock capture follows the SPF and the architecture's section 9.7 rules.

`-nopi_changes` adds an unclocked tester cycle in TMAX. Count that cycle in the pattern-count comparison, and model it in the in-house pattern IR if the same constraint is used.

### 5.4 LOS setup — secondary mode

LOS is run only when the block's scan enable is routed and timed for at-speed switching, the SPF supports last-shift launch, and TMAX DRC passes. If any condition fails, record `LOS: not qualified` and use LOC only. Do not substitute launch-on-extra-shift (LOES) without a separate scope decision.

TMAX settings:

```tcl
set_drc protocols/p2_a_los.spf
set_delay -launch_cycle last_shift
# Scan enable remains active through the last shift; it must switch
# inactive before the capture clock. Do not constrain it low for launch.
run_drc

# Faults are added only after the branch DRC.
add_faults -all
# Or: read_faults frozen/p2_a_transition.faults
run_atpg -auto
write_patterns out/p2_a_los.stil -format stil
write_faults out/p2_a_los.faults -all
```

Assumptions:

- The last scan shift launches the transition; the scan enable is active during that launch edge.
- The scan enable switches inactive before the capture clock. The scan-enable path is treated as an at-speed signal.
- There is one at-speed capture pulse after the launch.
- LOS is not qualified on blocks with an on-chip clock controller or scan-enable topology that cannot support last-shift launch. Record the limitation; do not hide it by mixing modes.
- LOS patterns use the basic-scan engine in TMAX; do not compare them to LOC fast-sequential patterns as if they were the same experiment.

### 5.5 Command-order validation

The order above follows the documented TMAX flow, but it has not been validated against an installed TMAX release in this environment. Before M9, run the exact script in the qualified release and record:

- TMAX version and `report_settings`;
- whether the common DRC accepts the common SPF before the transition model is selected;
- whether `set_faults -model transition` is accepted before the branch `set_delay`;
- whether the branch DRC accepts the mode-specific SPF after the delay and constraints;
- whether `add_faults` is accepted only after the branch DRC;
- any command spelling or option differences.

If the installed release requires a different order, update this protocol and the M9 decision record before running the baseline. Never move `add_faults` ahead of a DRC to make a command sequence work.

### 5.6 TMAX baseline outputs

The M9 package must contain:

- TMAX command script and full log.
- `report_settings` output.
- Pattern files in STIL, plus the pattern-count report.
- Fault list and per-fault state report.
- Coverage report with numerator, denominator, and exclusion reasons.
- DRC report and any waivers.
- SHA-256 hashes for every frozen input and for the command script.

Run the baseline once with the frozen environment. If TMAX is rerun, keep both logs and use the same hashes; do not silently replace a baseline to improve a number.

## 6. LOC/LOS assumptions compared

| Aspect | LOC — primary | LOS — secondary |
|---|---|---|
| Launch event | First of two at-speed capture pulses | Last scan-shift edge |
| Scan enable at launch | Inactive | Active |
| Scan enable before capture | Inactive | Switches inactive |
| Capture event | Second at-speed pulse | One at-speed capture pulse |
| TMAX `set_delay -launch_cycle` | `system_clock` | `last_shift` |
| TMAX scan-enable constraint | `add_pi_constraints 0 scan_en` | No low constraint at launch; DRC checks the transition |
| Primary inputs | Held when `-nopi_changes` is used | Match the TMAX/SPF setting; record it |
| Primary outputs | Masked when `add_po_masks -all` is used | Match the TMAX/SPF setting; record it |
| At-speed scan enable | Not required | Required; otherwise not qualified |
| Multi-clock use | Per SPF; no incompatible simultaneous pulses | Only if all domains receive the last shift and SE timing is qualified |
| M9 target basis | Yes, unless the decision record says otherwise | Reported separately; not mixed into the LOC target |

Do not combine LOC and LOS patterns into one coverage or pattern-count number. Do not compare an in-house LOC run with a TMAX LOS run.

## 7. Coverage comparison

### 7.1 Fault universe and denominator

- Use the same transition fault list on both sides: same fault sites, same slow-to-rise and slow-to-fall pairs, same checkpoint and collapse policy.
- If TMAX must generate the list, compare counts and checkpoint policy before ATPG. A count mismatch invalidates a raw coverage comparison until explained.
- Compute testable coverage as:

```text
testable coverage = detected / (detected + undetected + aborted)
```

Proven untestable faults are excluded from the denominator only when the proof is recorded. Report the excluded count separately.
- Do not use a vendor headline "test coverage" number without recomputing from fault states. Normalize TMAX and in-house results to the same formula.
- Report slow-to-rise and slow-to-fall coverage separately, and report per-clock-domain coverage where the block has multiple domains.

### 7.2 Target

At M9, adopt the following per-block target for the qualified primary mode:

> **At least 90.0% testable transition coverage and within 1.0 percentage point of the frozen TMAX transition baseline on each P2 parity block.**

Rules:

- The target is per block and per mode. Do not hide a failing block in an aggregate average.
- The aggregate is reported for information only; it is not a substitute for a per-block pass.
- `1.0 pt` means one percentage point, not one percent relative.
- If the TMAX baseline is below 90.0%, the 1.0-pt parity band may not contain any value at or above 90.0%. The M9 record must state whether the absolute value, parity, or both govern, and must either fix the block/test protocol or explicitly waive or retarget the absolute value. Do not silently lower it.
- If the block cannot support the primary mode, the M9 record must say so and name the qualified mode. A mode substitution requires an explicit decision, not an assumption.

### 7.3 Required comparison detail

For each block and mode, report:

| Field | Meaning |
|---|---|
| Fault count | Total, slow-to-rise, slow-to-fall |
| Detected | Faults with a generated and validated pattern |
| Undetected | Testable faults with no pattern |
| Aborted | Search budget exhausted, with the budget recorded |
| Untestable | Proven untestable, with proof metadata |
| Testable coverage | Detected divided by detected + undetected + aborted |
| TMAX coverage | Same formula, same fault list |
| Delta | In-house minus TMAX, in percentage points |
| Pattern count | Scan patterns and tester cycles, separately |
| X masks | Count and source of masks that affect observation |
| Replay | Gate-level simulation match rate |

Every fault-state difference and every coverage or pattern-count difference that affects the decision goes into the parity ledger. A difference is not "explained" by naming a category; it needs an owner and a resolution.

## 8. Pattern-count comparison

### 8.1 Units

Pattern count is not one number. Report all three:

| Unit | Definition | Use |
|---|---|---|
| Scan patterns | Load/unload frames, each with its launch/capture sequence | ATPG and compaction comparison |
| Capture cycles | Launch and capture cycles per scan pattern | LOC/LOS timing and pattern structure |
| Tester cycles | Total cycles after scan expansion and timing generation | ATE test-time estimate |

TMAX may report patterns and vectors differently from the in-house IR. Reconcile the counts from the written STIL and the pattern report; do not compare a TMAX "vector" count to an in-house "pattern" count without mapping.

### 8.2 Ratios

For each block and mode:

```text
scan-pattern ratio   = in-house scan patterns / TMAX scan patterns
tester-cycle ratio   = in-house tester cycles / TMAX tester cycles
```

Report both at the full run. If coverage differs by more than 0.2 pt, also report a matched-coverage ratio derived from the per-pattern coverage curve, with the method stated. Do not truncate a pattern set in a way that changes the fault list without rerunning both sides.

### 8.3 Compression and constraints

- Compare like with like. Never compare in-house uncompressed patterns to TMAX compressed patterns.
- For P2-C, run two configurations if compression is enabled: an uncompressed parity baseline and the frozen compressed configuration. The M9 record names which configuration is the target basis. The recommendation is uncompressed for ATPG-core parity, with compressed results recorded for ATE impact; if both tools use equivalent compression, the compressed comparison may be used instead.
- `-nopi_changes` adds a TMAX tester cycle. Count it on both sides or exclude it on both sides; do not let a constraint difference masquerade as a pattern-count difference.
- Pattern merging, dynamic compaction, and static compaction settings must be recorded. Different merge settings are a ledger item, not a free pass.

### 8.4 Acceptance and review trigger

The P3 exit gate does not currently contain a transition pattern-count target. The architecture's 1.10× figure is the P2 stuck-at target; do not silently apply it to transition.

At M9, record the TMAX baseline and use these review triggers:

- scan-pattern ratio greater than 1.25×, or
- tester-cycle ratio greater than 1.25×, or
- a compression-state mismatch that makes the counts non-comparable.

A trigger requires an explanation and an owner before P3 commits to a test-time budget. It is not an automatic failure, and it does not change the coverage target.

## 9. M9 decision record

### 9.1 Decision text

The M9 decision is recorded as a single controlled record, `D-TR-01`, with this substance:

> For P3 transition ATPG, adopt a per-block target of at least 90.0% testable transition coverage and within 1.0 percentage point of the frozen TMAX transition baseline on each P2 parity block, in the qualified primary mode (LOC unless this record states otherwise). Pattern count is reported and reviewed, but is not a P3 exit-gate criterion. LOS is included only for blocks explicitly qualified for it.

The record may add block-specific exceptions, but it may not silently change the 90.0% or 1.0-pt values. A change requires a new decision ID and a ledger entry.

### 9.2 Recording procedure

1. **Before M9:** DFT freezes the inputs and runs the TMAX baseline on P2-A/B/C. Verification independently checks the fault-state counts and coverage arithmetic.
2. **At M9:** DFT, Architecture, Verification, and ATE review the baseline package. They fill Appendix A, including the per-block TMAX values, mode qualification, compression state, and any exceptions.
3. **Approval:** The DFT lead and Architecture owner sign the record. Status becomes `Approved`. The decision ID is `D-TR-01`.
4. **Reference:** The P3 exit gate in architecture section 17.1 points to the M9 targets in section 20.1; `D-TR-01` is the block-specific instance of that policy. The P3 team uses Appendix A, not a verbal target.
5. **Change control:** Any target, mode, block, or baseline change gets a new decision ID and a new ledger entry. Do not edit an approved baseline in place.

### 9.3 How the P3 gate uses it

At the P3 gate, fill the in-house columns in Appendix A. The gate passes only when, for every P2 parity block in the qualified primary mode:

- testable transition coverage is at least 90.0%;
- in-house coverage is within 1.0 pt of the recorded TMAX baseline;
- patterns replay in gate-level simulation;
- the parity ledger has no open unexplained difference; and
- the diagnosis target is handled separately under its own M9 decision.

A block that is not qualified for LOS is not a failure; it simply has no LOS row. A block that fails LOC cannot be rescued by an aggregate.

## 10. Roles and sequence

| When | Owner | Action | Output |
|---|---|---|---|
| M8 | DFT lead | Freeze P2-A/B/C, SPF, scan config, libraries, SDC, fault list | Freeze package with hashes |
| M8 | DFT lead / license owner | Confirm TMAX transition license and version | Environment record |
| M8 | DFT lead | Qualify LOC; assess LOS on each block | Mode qualification table |
| M9 | DFT lead | Run TMAX transition baseline | Patterns, fault reports, coverage, pattern counts |
| M9 | Verification | Recompute coverage and check replay/ledger | Independent baseline review |
| M9 | DFT + Architecture + Verification + ATE | Approve target and record `D-TR-01` | Appendix A signed |
| M9–M15 | Tools team | Implement and run in-house transition ATPG | P3 comparison package |
| P3 gate | DFT lead + Architecture | Check per-block target and ledger | Gate decision |

## 11. Parity ledger

Every difference gets a row. Do not use "tool difference" as a resolution.

| ID | Block | Mode | Dimension | TMAX value | In-house value | Delta | Cause | Owner | Resolution | Status |
|---|---|---|---|---|---|---|---|---|---|---|
| TR-001 | P2-A | LOC | Example: fault-count delta | 100,000 | 100,240 | +240 | Checkpoint policy | DFT | Rerun with frozen list | Open |

Minimum dimensions to track:

- Fault count and checkpoint/collapse policy.
- Fault states: detected, undetected, aborted, untestable.
- Coverage denominator and exclusions.
- X-source and mask policy.
- Clock groups and false paths.
- Scan-enable and primary-input constraints.
- Primary-output masking.
- Abort/time limits.
- Compression state.
- Pattern merge/compaction settings.
- Pattern count units and conversion.

## 12. Exit criteria

### M9 baseline exit

- TMAX transition license and version recorded.
- P2-A/B/C frozen; hashes complete.
- LOC baseline complete on all three blocks.
- LOS qualification recorded for each block; unqualified blocks are explicitly marked, not silently omitted.
- TMAX coverage, fault states, pattern counts, and reports complete.
- `D-TR-01` signed in Appendix A with the per-block baseline.
- No unresolved setup ambiguity that would change the target.

### P3 gate exit

- Every P2 parity block in its qualified primary mode meets the 90.0% and 1.0-pt targets.
- Pattern replay matches in gate-level simulation.
- Parity ledger closed for every decision-affecting difference.
- Pattern-count ratios reported; review triggers resolved or accepted by the owners.
- Diagnosis accuracy is evaluated under its separate M9 decision.

If any of these cannot be met, record the failure and the proposed remedy. Do not replace a missing measurement with an assumption.

## Appendix A — M9 decision record template

### A.1 Decision header

| Field | Value |
|---|---|
| Decision ID | `D-TR-01` |
| Status | Proposed / Approved / Waived |
| Date | |
| DFT lead | |
| Architecture owner | |
| Verification owner | |
| ATE owner | |
| TMAX version / license | |
| TMAX command script SHA-256 | |
| Compression state | Off / frozen configuration |
| Primary mode | LOC (or name the approved substitute) |
| LOS qualification | P2-A: / P2-B: / P2-C: |

### A.2 Frozen inputs

| Input | Path | SHA-256 |
|---|---|---|
| Netlist | | |
| Libraries | | |
| SDC | | |
| SPF/test protocol | | |
| Scan map | | |
| Pin map | | |
| Fault list | | |

### A.3 TMAX baseline and target

| Block | Profile | Mode | Faults (STR/STF) | TMAX testable coverage | TMAX scan patterns | TMAX tester cycles | Target | In-house P3 coverage | Delta | Status |
|---|---|---|---|---|---|---|---|---|---|---|
| P2-A | 20–50 k, single clock | LOC | | | | | ≥90.0%, within 1.0 pt of TMAX | | | |
| P2-B | 100–300 k, two clocks | LOC | | | | | ≥90.0%, within 1.0 pt of TMAX | | | |
| P2-C | 0.5–1 M, X/compression | LOC | | | | | ≥90.0%, within 1.0 pt of TMAX | | | |

### A.4 Exceptions and approvals

| Item | Record |
|---|---|
| Blocks not qualified for LOS, with reason | |
| Blocks with compression differences | |
| TMAX baseline below 90.0%, with remedy | |
| Approved waivers or retargets, with new decision ID | |
| Pattern-count review triggers | |
| Sign-off | DFT lead: __________  Architecture: __________  Verification: __________  ATE: __________ |

### A.5 P3 result attachment

At the P3 gate, attach:

- in-house run manifest and input hashes;
- per-block coverage reports using the section 7 denominator;
- pattern-count report in scan-pattern and tester-cycle units;
- gate-level replay results;
- parity ledger with all decision-affecting differences closed;
- any mode or block exception approved by `D-TR-01`.

## Appendix B — References

- Architecture section 14.3: general TetraMAX parity protocol.
- Architecture section 16.2: P2 block selection.
- Architecture section 16.5: acceptance and the three-block comparison report.
- Architecture section 17.1: P3 phase and exit gate.
- Architecture section 20.1: transition coverage and diagnosis accuracy decisions.
- TetraMAX ATPG User Guide, transition-delay fault ATPG: `set_delay -launch_cycle`, scan-enable constraints, capture procedures, and transition pattern formatting. Freeze the exact TMAX release used for the baseline.
