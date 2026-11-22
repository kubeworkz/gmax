# Diagnosis Benchmark — P3 Top-1/Top-5 Target

**Status:** Draft for review; not committed.  
**Date:** 2026-11-22  
**Owner:** DFT lead for the injected corpus; Architecture for scoring and the decision/result records; Verification for independent scoring.  
**Companion documents:** `dft-atpg-platform-architecture-grx930-g100.md` sections 17.1 and 20.1; `transition-parity-protocol-p2.md` for the P3 transition context.

## 1. Purpose

This document defines the benchmark behind the P3 diagnosis target:

> True site in the top 5 suspects for at least 90% of injected stuck-at/transition failures, with top-1 exact at least 70%.

It defines what is injected, how chain and cell failures are separated, what counts as a suspect, how suspects are ranked, and how top-1 and top-5 accuracy are computed. It does not define the diagnosis algorithm or claim that P3 already meets the target. The architecture target is unchanged; section 2 proposes a per-stratum interpretation, and `D-DIAG-01` at M9 selects the evaluation mode and adopts the target/TMAX baseline. `D-DIAG-RESULT-01` records the later P3 in-house result.

The benchmark is a **diagnosis** benchmark, not an ATPG benchmark. A case is scored only when the applied pattern set actually detects the injected defect and produces an observable failing response. Undetected faults are reported, not counted as successful diagnoses.

## 2. Target and strata

The architecture target is a single 90% top-5 / 70% top-1-exact statement. This benchmark **proposes** interpreting it as a per-stratum gate. That interpretation is not effective until the M9 decision `D-DIAG-01` explicitly adopts it; until then the architecture target remains the single 90/70 policy and this table is a proposed evaluation method, not a redefinition. Section 2.1 defines the available modes and the fallback when `D-DIAG-01` is not approved.

| Stratum | Failure class | Required under proposed per-stratum decision | Reported separately |
|---|---|---|---|
| D-CELL-SA | Cell/logic stuck-at, SA0/SA1 | Yes | Yes |
| D-CELL-TR | Cell/logic transition, STR/STF | Yes | Yes |
| D-CHAIN-SA | Scan-chain stuck-at on a chain segment | Yes | Yes |
| D-CHAIN-BR | Scan-chain bridging between adjacent chain nets | Yes, if qualified; N/A if not | Yes, or N/A |
| D-CONTROL | Clock, reset, scan-enable, test-mode control defects | No | Exploratory |
| D-CHAIN-TR | Chain slow-shift/transition defect | No | Exploratory |

Rules:

- **If `D-DIAG-01` selects `PER_STRATUM` or `DEFAULT`, cell and chain are separate gates.** Passing the cell strata does not compensate for a failing chain stratum, and passing chain does not compensate for cell.
- **D-CHAIN-BR** is part of the chain suite only when the M9 decision qualifies bridging injection. If it is not qualified, mark it **N/A** in the decision record, exclude it from the overall denominator, and record why. N/A is neither a pass nor a failure; it may not be silently omitted or converted to a pass.
- **D-CONTROL** and **D-CHAIN-TR** are reported as extensions. They are not allowed to raise or lower the 90/70 result.
- The architecture's 90/70 target is unchanged. `D-DIAG-01` must record one of the evaluation modes in section 2.1.

### 2.1 Evaluation mode and P3 overall result

`D-DIAG-01` must record one evaluation mode. The mode determines the P3 overall result recorded in `D-DIAG-RESULT-01`; it may not be chosen after seeing the numbers.

| Mode | Overall result computation | Approval |
|---|---|---|
| `PER_STRATUM` | PASS only if every required, non-N/A stratum meets top-1 ≥70% and top-5 ≥90%; FAIL if any fails; INCOMPLETE if a required stratum is missing or underpowered without an approved waiver | Must be explicitly approved in `D-DIAG-01` |
| `POOLED` | PASS only if total top-1 hits divided by total scored cases is ≥70% and total top-5 hits divided by total scored cases is ≥90%, across all required, non-N/A cases; the worst top-1 and top-5 strata are still reported as mandatory risk flags | Must be explicitly approved in `D-DIAG-01`; approval accepts the risk that a weak stratum can be averaged over |
| `DEFAULT` (worst-case) | PASS only if the minimum top-1 across required, non-N/A strata is ≥70% and the minimum top-5 is ≥90%; FAIL otherwise; pooled numbers are informational | Applies automatically when `D-DIAG-01` is not approved or records no mode |

Rules:

- The architecture's 90/70 thresholds are unchanged in every mode. The mode changes only how per-case results are rolled up.
- N/A strata are excluded from the denominator and listed with the reason; they are neither pass nor fail.
- The binding strata are always reported: the required stratum with the lowest top-1 and the required stratum with the lowest top-5, listed separately if they differ.
- Pooled top-1 and top-5 are always reported; they are the target result only in `POOLED` mode.
- The `DEFAULT` worst-case rule is deliberately conservative: its minima rule is equivalent to requiring every required stratum to meet both thresholds, but it does not depend on `D-DIAG-01` being approved.
- There is no implicit pooled fallback. If `D-DIAG-01` is absent, not approved, or leaves the mode blank, `DEFAULT` worst-case governs.

## 3. Corpus and fault-injection method

### 3.1 Golden corpus

Build the M9 corpus from the frozen P2 parity blocks and the M9 diagnosis pattern package. Do not wait for P3 patterns; P3 starts after the decision. This keeps `D-DIAG-01` at M9 and prevents the target from moving once P3 ATPG starts.

The M9 pattern package is:

- cell stuck-at: the frozen P2 stuck-at patterns for the same block;
- cell transition: the TMAX transition patterns generated on the same P2 parity block under `transition-parity-protocol-p2.md`, frozen at the M9 run;
- chain stuck-at and bridging: the scan shift/load-unload patterns in the frozen P2 scan configuration; if a dedicated chain test pattern exists, use it and record it.

The corpus contains:

- the P2 parity netlist and libraries;
- the scan map, including chain order and bit positions;
- the P2 stuck-at patterns, the M9 TMAX transition patterns, and the chain shift patterns;
- the failing response for each injected case;
- the hidden ground truth for each case.

Use a deterministic PRNG seed and a published sampling plan. The diagnosis tool receives the failing response and the design data, never the ground-truth record. P3 may add an extension corpus using in-house transition patterns, but it does not change or replace the M9 corpus or `D-DIAG-01`.

### 3.2 One defect per case

Each case has exactly one injected defect. Do not inject multiple defects, and do not reuse a case after changing the defect. If a case produces failures that cannot be attributed to the injected defect, discard it and record the reason.

### 3.3 Independent site selection

Do not select injection sites from the diagnosis tool's suspect list or from the ATPG run's target fault list. Build a separate injection list by stratified random sampling over eligible sites:

- cell type and pin direction;
- fanout and reconvergence;
- clock domain;
- chain and bit position;
- stuck-at versus transition;
- slow-to-rise versus slow-to-fall.

Record the eligible-site count, the sampled count, and the seed. A convenience sample chosen after seeing diagnosis results invalidates the benchmark.

### 3.4 Cell/logic injection

**Stuck-at (D-CELL-SA)**

- Inject SA0 or SA1 on a cell output net or a primary-input net.
- Ground truth is the canonical driver site: `(cell_instance, output_pin)` for a cell output net, or `(primary_input, port)` for a primary-input net. Record the net alias.
- Do not inject the same net separately at a receiving cell input; it is one physical site. If the net has multiple drivers, exclude the case rather than guessing which driver is the site.
- Do not inject on clock, reset, scan-enable, test-mode, analog, or memory black-box control pins in this stratum; those belong to D-CONTROL.

**Transition (D-CELL-TR)**

- Inject STR or STF at a cell output pin.
- Use a launch/capture pattern pair from the M9 TMAX transition pattern set generated on the same P2 parity block (see `transition-parity-protocol-p2.md`).
- Ground truth is `(cell_instance, output_pin, slow_to_rise | slow_to_fall)`.
- Cell-internal resistive/open defects and path-delay faults are out of scope for this stratum.

### 3.5 Chain injection

**Chain stuck-at (D-CHAIN-SA)**

- Inject SA0 or SA1 on the scan-chain net between two scan cells.
- Ground truth is `(chain_id, bit_index, SA0 | SA1)`.
- `bit_index` is the scan-cell position in shift order, 1-based from scan-in, using the frozen scan map. State the convention in the corpus manifest; do not mix 0-based and 1-based data.
- Use the scan shift/load-unload patterns from the frozen P2 scan configuration. A chain stuck-at should produce a stable shift/unload signature; record the failing chain and bit range.

**Chain bridging (D-CHAIN-BR)**

- Inject a low-resistance bridge between two adjacent chain nets.
- Ground truth is `(chain_id, bit_i, bit_i+1, bridge)`.
- Use the same shift pattern set and scan map. Record whether the bridge is to the next bit, the previous bit, or a named neighbor.
- If D-CHAIN-BR is not qualified at M9, mark it N/A in the decision record and do not inject this stratum.

**Chain transition (D-CHAIN-TR, exploratory)**

- Inject a slow-shift defect on a chain segment only if the M9 decision includes this class. It is reported separately and does not change the D-CHAIN-SA result.

### 3.6 Detection and exclusions

A case enters the scored denominator only when:

1. the injected defect is detected by the applied pattern set;
2. the failing response is deterministic and attributable to the defect; and
3. the ground truth is unique.

Report undetected cases, X-masked cases, and discarded cases separately. If more than 5% of a stratum is undetected on the frozen pattern set, the stratum is under-observed: add patterns or record the limitation before claiming the target. Do not remove undetected cases silently.

### 3.7 Sample size

Aim for at least **300 scored cases per required stratum per chip**, or all eligible cases if fewer exist. At 90% observed accuracy, 300 cases give an approximate 95% Wilson interval of about ±3.4 percentage points; 500 cases give about ±2.6 points. Record the exact sample size and confidence interval. Do not report a point estimate without the denominator.

Distribute cases across the P2 parity blocks so that no single block supplies more than 60% of a stratum. If a block cannot support a stratum, record the limitation and do not substitute a different block after seeing results.

## 4. Suspect representation and ranking

### 4.1 Typed suspects

Every diagnosis output suspect must be a typed physical site:

| Type | Required fields | Canonical identity |
|---|---|---|
| Cell pin | instance, pin | `(instance, pin)` |
| Net | net name, driver instance/pin | driver `(instance, pin)` |
| Scan cell | chain ID, bit index | `(chain_id, bit_index)` |
| Chain segment | chain ID, start bit, end bit | `(chain_id, start_bit, end_bit)` |

A suspect such as "block B" or "the scan chain" is too coarse to count as a top-1 site. It may appear, but it is a low-resolution suspect and is scored by the same group rules.

### 4.2 Canonicalization and deduplication

- Map net suspects to their driver pin when the net has a unique driver. A net and its driver cell pin are one physical site, not two suspects.
- Map scan-cell and chain-segment suspects to the scan map. A segment that contains the true bit is a hit only under the group rules below.
- Remove duplicate representations of the same canonical site before ranking.
- Keep a stable canonical ID for every suspect. The display order of tied suspects may use that ID; scoring uses the rank and equivalence group, not an arbitrary display tie-break.

### 4.3 Equivalence groups

Two suspects are equivalent if they produce the same predicted failing-pattern signature for the applied pattern set under the golden model. Equivalence is a property of the observable failure, not a tool's claim.

- Build the signature from the failing patterns and failing scan outputs.
- Group suspects with identical signatures.
- If the tool reports its own equivalent-suspect group, verify the group against the golden signature; do not accept an unverified group.
- Canonicalize aliases before grouping so a cell pin and its net do not appear as two groups.
- A group has a rank and a size. The size is the number of distinct canonical sites in the group.

For a chain segment, the group contains the scan-cell bits in that segment. A segment covering ten bits has group size ten even if it contains the true bit.

### 4.4 Ranking rules

1. Sort suspect groups by their best suspect score, descending.
2. Use competition ranking: equal scores share a rank; the next distinct score takes the rank after the tie group.
3. Compute the true site's rank as the rank of its equivalence group.
4. If the true site is absent from the output, its rank is infinity.
5. If the tool misclassifies the failure type — for example, reports a cell failure for a chain injection — the case is a miss at every k. Do not give partial credit for a nearby chain or cell.
6. A chain-level diagnosis that names only the correct chain, with no bit/segment, is a group covering the whole chain; it can be a top-5 hit but cannot be a top-1 exact hit.

## 5. Scoring top-1 and top-5

### 5.1 Definitions

For each scored case, let `r` be the rank of the true site's equivalence group and `s` its group size.

```text
top-1 exact = 1 if r = 1, s = 1, and the true group is the only group at rank 1; otherwise 0
top-5       = 1 if r <= 5, otherwise 0
```

- **Top-1 exact** requires a unique rank-1 group containing only the true site. A rank-1 group with several indistinguishable sites is not exact; report it separately as `top-1 class`.
- **Top-5** requires the true site's equivalence group to be at rank 5 or better. A group at rank 5 with many sites still counts as top-5, but the group size is reported so a broad guess is visible.
- A correct chain ID with the wrong bit is not top-1 exact. It may be top-5 only if the true bit is in a group ranked 5th or better.
- A correct cell instance with the wrong pin is not top-1 exact; it may count only if the true pin is in the same verified equivalence group.

### 5.2 Accuracy

For each stratum:

```text
top-1 exact accuracy = cases with top-1 exact / scored cases
top-5 accuracy       = cases with top-5       / scored cases
```

Report:

- numerator and denominator for each stratum;
- 95% Wilson confidence interval;
- per-block and per-chip results;
- the evaluation mode recorded in `D-DIAG-01` and the overall result/binding strata per section 2.1;
- the pooled accuracy over required, non-N/A strata only (never D-CONTROL or D-CHAIN-TR); target result only in `POOLED` mode, informational otherwise;
- N/A strata and reasons;
- median and maximum equivalence-group size for top-1 exact and top-5 hits;
- chain-versus-cell classification confusion matrix;
- undetected and discarded counts.

### 5.3 Resolution reporting

Accuracy alone can hide a useless diagnosis. Report these alongside it:

| Metric | Why |
|---|---|
| Median group size at rank 1 | A large group is not an exact site |
| Median chain-segment length for chain hits | A whole-chain hit is low resolution |
| Number of suspects in the top 5 | A top-5 with hundreds of ties is weak evidence |
| Type-classification accuracy | A chain/cell mix-up is a different failure mode |
| True-chain rank for chain cases | Separates chain localization from bit localization |

Resolution metrics are descriptive at M9. They do not replace the 90/70 target, and they may not be traded against it without a new decision.

## 6. TMAX confirmation at M9

The architecture requires the target to be confirmed against TMAX diagnosis at M9. Use the same M9 P2 corpus and failing responses.

Two capabilities are prerequisites, confirmed at M8 with the rest of the diagnosis setup and recorded in `D-DIAG-01`. The corpus is not purely TMAX-generated — it mixes the frozen P2 stuck-at patterns, the M9 TMAX transition patterns, and the chain shift patterns (section 3.1) — so neither can be assumed:

- **Failing-pattern input in STIL or WGL.** TMAX diagnosis must accept failing-pattern logs supplied to it, not only patterns the tool generated itself. If it cannot, the baseline covers only the TMAX-generated strata; every other stratum is marked unavailable in Appendix A.4, and the corpus is not re-cut to make the comparison work.
- **Machine-readable suspect export.** The export must carry, for every case, the full ranked suspect list with its type and group information, because step 2's canonicalization, equivalence, rank, and top-k rules cannot be applied to a headline accuracy figure.

A missing prerequisite is escalated at M8, not discovered at M9: without either, the confirmation cannot produce a baseline that is scoreable under step 2.

1. Run TMAX diagnosis on the frozen netlist, scan map, and failing pattern logs. Record the TMAX version, options, and license.
2. Score TMAX with the same canonicalization, equivalence, rank, and top-k rules. Do not use TMAX's headline accuracy if its suspect granularity differs.
3. Record TMAX per-stratum top-1 exact and top-5 alongside the in-house target.
4. If TMAX cannot diagnose a stratum, or its output cannot be represented in the typed-suspect schema, record the limitation. Do not substitute a different corpus to make the comparison work.

The M9 decision is not "TMAX says 90%, so we adopt 90%." The target is the architecture's 90/70 policy; TMAX confirms that the corpus, granularity, and scoring are realistic.

## 7. M9 target and TMAX-baseline record

Record the diagnosis benchmark decision as `D-DIAG-01` in Appendix A. It does not replace the architecture target; it records the evaluation mode, adopts the target, and freezes the TMAX baseline. The P3 in-house result is recorded separately in `D-DIAG-RESULT-01` (Appendix B).

`D-DIAG-01` must explicitly select one evaluation mode from section 2.1:

- **`PER_STRATUM`:** evaluate 90/70 per required stratum; overall PASS only if every required non-N/A stratum passes.
- **`POOLED`:** evaluate the pooled case-weighted result; overall PASS only if pooled top-1 and top-5 meet 70/90; the worst top-1 and top-5 strata are mandatory risk flags.
- **`DEFAULT` (worst-case):** PASS only if the minimum top-1 and minimum top-5 across required, non-N/A strata meet 70/90; this is also the automatic mode if `D-DIAG-01` is not approved or leaves the mode blank.

No other mode is allowed without a new decision ID. The record must state the mode explicitly; it may not be inferred from the results.

The record must contain:

- the evaluation mode (`PER_STRATUM`, `POOLED`, or `DEFAULT`);
- the target thresholds (top-1 ≥70%, top-5 ≥90%);
- the N/A strata;
- corpus manifest and SHA-256;
- injection seed and sampling plan;
- pattern-set hashes;
- scan-map and netlist hashes;
- per-stratum case counts, detected counts, and exclusions;
- TMAX per-stratum top-1 exact and top-5;
- any qualified, excluded, or N/A stratum;
- the P3 handoff package and scoring rules;
- equivalence-group and tie-handling notes;
- sign-offs from DFT, Architecture, and Verification.

No in-house P3 result is recorded in `D-DIAG-01`; that result belongs only to `D-DIAG-RESULT-01`.

Change control:

- An approved corpus or scoring rule may not be edited in place.
- A new stratum, injection class, or scoring rule gets a new decision ID.
- A target change gets a new decision ID and a new benchmark record.

## 8. P3 gate use

At P3, run the in-house diagnosis on the same M9 P2 corpus and fill Appendix B (`D-DIAG-RESULT-01`). P3 may add an extension corpus, but the extension does not replace or change the M9 corpus for the gate. The M9 `D-DIAG-01` record is frozen and is not updated with these results. Compute the overall result with the evaluation mode recorded in `D-DIAG-01`:

- `PER_STRATUM`: PASS only if every required, non-N/A stratum meets top-1 ≥70% and top-5 ≥90%.
- `POOLED`: PASS only if pooled top-1 and top-5 across required, non-N/A cases meet 70/90; report the worst top-1 and top-5 strata as mandatory risk flags.
- `DEFAULT`: PASS only if the minimum top-1 and minimum top-5 across required, non-N/A strata meet 70/90; this is the automatic mode when `D-DIAG-01` is not approved or leaves the mode blank.

In all modes, FAIL if the selected roll-up misses either threshold; INCOMPLETE if a required stratum is missing or underpowered without an approved waiver. Report the binding/worst strata, pooled numbers, and N/A strata. Mark an unqualified D-CHAIN-BR N/A and exclude it from the denominator.

Also require:

- the chain/cell classification confusion matrix is reported;
- no required stratum is underpowered or silently excluded;
- N/A strata are listed with reasons and are not counted as passes;
- all scoring disputes are closed in the benchmark ledger;
- patterns and failing responses replay deterministically; and
- the TMAX confirmation record is present.

Under `PER_STRATUM` or `DEFAULT`, a cell-only pass does not satisfy the chain strata, and a pooled informative number does not rescue a failing stratum. Under `POOLED`, the pooled result governs by explicit approval, and the worst top-1 and top-5 strata are mandatory risk flags. If `D-DIAG-01` is not approved or leaves the mode blank, DEFAULT worst-case governs; pooled is not the fallback.

## 9. Roles and sequence

| When | Owner | Action | Output |
|---|---|---|---|
| M8 | DFT lead | Freeze P2 parity netlists, P2 stuck-at patterns, scan map, and eligible-site lists; confirm TMAX transition and diagnosis setup, including failing-pattern input in STIL/WGL and machine-readable suspect export | Corpus inputs |
| M8 | DFT lead | Generate stratified injection manifest and seed | Injection manifest |
| M9 | DFT lead | Run TMAX transition ATPG on the P2 parity blocks and freeze the transition patterns | M9 transition pattern package |
| M9 | Verification | Build the full corpus and reproduce a sample of injected failures | Injection sanity check |
| M9 | DFT lead | Run TMAX diagnosis on the corpus | TMAX baseline |
| M9 | Architecture + Verification | Select evaluation mode and adopt target/TMAX baseline | `D-DIAG-01` |
| P3 gate | DFT + Architecture + Verification | Record P3 scores and overall result | `D-DIAG-RESULT-01` |

## 10. Validity rules and common failure modes

- **Do not use ATPG targets as ground truth.** The injection list is independent.
- **Do not choose sites after seeing diagnosis results.** Use the seeded sampling plan.
- **Do not count undetected faults as misses.** Report them separately; an under-observed stratum is invalid, not zero accuracy.
- **Do not accept a whole-chain suspect as top-1 exact.** Group size must be one.
- **Do not compare a cell-pin suspect with a net suspect as two independent candidates.** Canonicalize first.
- **Do not let a tool's unverified "equivalent" label inflate accuracy.** Verify the failing-pattern signature.
- **Do not use a mixed chain/cell pooled number as the target result.** Report the strata separately; label any pooled number informational only unless `POOLED` mode was explicitly approved.
- **Do not infer the overall mode from the results.** `D-DIAG-01` must record `PER_STRATUM`, `POOLED`, or `DEFAULT`; a missing mode resolves to `DEFAULT` worst-case.
- **Do not hide a low-resolution result behind top-5.** Report group size and segment length.

## Appendix A — `D-DIAG-01` M9 target and TMAX-baseline record

### A.1 Decision header

| Field | Value |
|---|---|
| Decision ID | `D-DIAG-01` |
| Status | Proposed / Approved / Waived |
| Evaluation mode | PER_STRATUM / POOLED / DEFAULT (worst-case) |
| Mode approval | Approved / Default applied |
| Target adoption | Top-1 exact ≥70%; top-5 ≥90% |
| N/A strata | |
| Date | |
| DFT lead | |
| Architecture owner | |
| Verification owner | |
| TMAX version / license | |
| TMAX failing-pattern input (STIL/WGL) | Confirmed / Not available, with reason in A.4 |
| TMAX suspect export (machine-readable) | Confirmed / Not available, with reason in A.4 |
| Corpus SHA-256 | |
| Injection seed | |
| Pattern-set hashes | |
| Netlist / scan-map hashes | |

### A.2 Corpus and scoring

| Item | Record |
|---|---|
| Sampling plan | |
| Corpus source | P2 parity blocks; frozen P2 stuck-at patterns; M9 TMAX transition patterns; P2 chain shift patterns |
| Eligible sites per stratum | |
| Undetected / excluded cases | |
| Equivalence construction | |
| Tie handling | |
| Top-1 exact definition | Unique rank-1 group of size 1 |
| Top-5 definition | True group rank <= 5 |

### A.3 TMAX baseline and target

| Stratum | Scored cases | TMAX top-1 exact | TMAX top-5 | Target top-1 | Target top-5 | Status |
|---|---|---|---|---|---|---|
| D-CELL-SA | | | | ≥70% | ≥90% | |
| D-CELL-TR | | | | ≥70% | ≥90% | |
| D-CHAIN-SA | | | | ≥70% | ≥90% | |
| D-CHAIN-BR | | | | ≥70% if qualified; N/A if not | ≥90% if qualified; N/A if not | N/A if not qualified |
| D-CONTROL | | | | informational | informational | Informational |
| D-CHAIN-TR | | | | informational | informational | Informational |

This table contains only the TMAX baseline and the adopted target. The M9 record contains no in-house P3 score and no overall result.

### A.4 Exceptions and approvals

| Item | Record |
|---|---|
| Stratum not qualified, with reason | |
| D-CHAIN-BR not qualified (N/A), with reason | |
| Evaluation mode not approved; DEFAULT worst-case applied, with reason | |
| TMAX stratum unavailable, with reason | |
| Corpus limitation or underpowered stratum | |
| Approved target or scoring change, with new decision ID | |
| Sign-off | DFT lead: __________  Architecture: __________  Verification: __________ |

### A.5 M9 handoff to P3

At M9, freeze `D-DIAG-01` and hand off:

- the approved record and its referenced corpus, injection, pattern, netlist, and scan-map manifests;
- the TMAX baseline and confirmation report;
- the scoring, canonicalization, equivalence, and tie rules;
- a benchmark ledger snapshot with open disputes identified.

The handoff does not contain an in-house P3 result. The P3 result record links to this handoff and does not replace it; only its result fields are new.

## Appendix B — `D-DIAG-RESULT-01` P3 result record

### B.1 Result header

| Field | Value |
|---|---|
| Result ID | `D-DIAG-RESULT-01` |
| Linked decision | `D-DIAG-01` |
| Status | Draft / Complete / Accepted / Rejected |
| Date | |
| In-house diagnosis version | |
| Run manifest SHA-256 | |
| Corpus SHA-256 (must match `D-DIAG-01`) | |
| Evaluation mode (inherited; must match `D-DIAG-01`) | PER_STRATUM / POOLED / DEFAULT |
| DFT lead | |
| Architecture owner | |
| Verification owner | |

The gate corpus is the frozen `D-DIAG-01` corpus, not a new corpus; the SHA-256 above is checked against A.1 and a mismatch blocks scoring. P3 extension results are separate and cannot substitute for this corpus. The evaluation mode and target thresholds are inherited from `D-DIAG-01` and are not reselected here.

### B.2 Per-stratum results

Targets and the TMAX baseline are in `D-DIAG-01` Appendix A.3 and are not copied here. The status applies those inherited thresholds. Δ is in-house minus the TMAX baseline; positive is better.

| Stratum | Scored cases | In-house top-1 | In-house top-5 | Δ top-1 vs TMAX | Δ top-5 vs TMAX | Status |
|---|---|---|---|---|---|---|
| D-CELL-SA | | | | | | |
| D-CELL-TR | | | | | | |
| D-CHAIN-SA | | | | | | |
| D-CHAIN-BR | | | | | | N/A if not qualified |
| D-CONTROL | | | | | | Informational |
| D-CHAIN-TR | | | | | | Informational |

### B.3 Overall roll-up

| Reporting view | Top-1 exact | Top-5 | Result |
|---|---|---|---|
| Binding required strata (worst top-1 / worst top-5) | | | |
| Pooled required, non-N/A strata (excludes D-CONTROL and D-CHAIN-TR) | | | Target only in POOLED mode; otherwise informational |
| Overall architecture result | | | PASS / FAIL / INCOMPLETE |

### B.4 Exceptions and sign-off

| Item | Record |
|---|---|
| Stratum underpowered or missing, with reason | |
| D-CHAIN-BR not qualified (N/A), with reason | |
| Evaluation mode fallback or waiver, with reason | |
| TMAX baseline deviation, with reason | |
| P3 extension corpus used, with manifest | |
| Scoring dispute, with ledger ID | |
| Corpus or run-manifest mismatch, with reason | |
| Sign-off | DFT lead: __________  Architecture: __________  Verification: __________ |

This record does not change the target, mode, corpus, or N/A decisions in `D-DIAG-01`. A correction that would change any of them requires a new decision ID; this result record cannot alter `D-DIAG-01`. After acceptance, a correction to this result gets a new result ID with the reason recorded.

### B.5 Attachments

At the P3 gate, attach:

- injection manifest and seed;
- failing responses and pattern logs;
- per-stratum scoring worksheet;
- equivalence-group evidence;
- chain/cell classification confusion matrix;
- TMAX confirmation report;
- benchmark ledger with open disputes closed;
- in-house diagnosis run manifest and version;
- P3 extension corpus manifest, if used.

## Appendix C — Benchmark ledger

| ID | Case / stratum | Dimension | Expected | Observed | Cause | Owner | Resolution | Status |
|---|---|---|---|---|---|---|---|---|
| DB-001 | Example | Type classification | chain | cell | Chain/cell classifier | DFT | Re-run with chain signature | Open |

Track at least:

- ground-truth canonicalization disputes;
- equivalence-group disagreements;
- tie/rank disputes;
- chain bit-index convention errors;
- undetected or X-masked cases;
- type-classification errors;
- corpus sampling deviations;
- TMAX granularity differences.

## Appendix D — References

- Architecture section 17.1: P3 phase and exit gate.
- Architecture section 20.1: diagnosis accuracy decision.
- Architecture section 9.9: coverage and fault-state reporting.
- `transition-parity-protocol-p2.md`: transition-parity corpus and M9 baseline context.
- TetraMAX ATPG and Diagnosis User Guide: diagnosis input and suspect reporting for the installed release. Freeze the exact version used for the M9 confirmation.
