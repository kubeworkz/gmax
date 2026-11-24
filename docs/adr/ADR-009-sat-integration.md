# ADR-009: SAT solver boundary and independent smoke evidence

- Status: Accepted (amended 2026-11-24: MiniSat 2.2.1 pin)
- Date: 2026-11-24
- Context: WP-008 / G0.2, with WP-012 as the follow-on SAT work

## Context

The ATPG flow now uses SAT as a closer after PODEM: a target fault is encoded
as a two-copy miter, and SAT either produces a distinguishing assignment or
proves that no such assignment exists. The current implementation is an
internal CDCL solver. G0.2 asks for smoke evidence that a SAT result and an
UNSAT result are genuine, and specifically names MiniSat agreement as the
target check.

Architecture ADR-003 already selects CaDiCaL as the primary solver and MiniSat
as the independent small-instance cross-check behind an `ISatSolver` boundary.
No MiniSat, CaDiCaL, or pysat executable or package was installed when this ADR
was first accepted, and `third_party` is still unfrozen under TBD-11. Adding a
solver as a frozen dependency would require a pin, license review, and an
accepted ADR; this ADR is amended below to introduce the test-only MiniSat pin.
Silently treating a second call into the same solver as independent evidence
would not satisfy the intent of G0.2.

## Decision

1. `ISatSolver` in `src/core/atpg/sat.h` is the permanent solver boundary for
   ATPG. The fault miter and solver construction depend on the interface, not
   on the concrete CDCL implementation. The interface owns variable and clause
   creation, solving with a conflict/time budget, model access, metrics, and
   timeout state.

2. The current deterministic CDCL implementation is the P0 adapter behind that
   boundary (`make_internal_sat_solver()`). Each fault gets a fresh solver
   instance. Incremental reuse, preprocessing, and proof logging are not part
   of this P0 adapter.

3. A SAT request keeps the existing safety policy. The closer receives the
   remaining per-fault `max_time_ms`; an exhausted deadline or conflict budget
   returns `Unknown`, never `Untestable`. In zero-backtrack mode the SAT
   fallback is not invoked at all, so the zero-backtrack contract is unchanged.

4. The independent G0.2 evidence includes a Yosys SAT cross-check, not a
   relabeling of internal results. A two-copy c432 miter is checked with the
   pinned Yosys 0.71 `sat` command:
   - fault 168 stuck-at-1 must produce a model (`SAT` / testable);
   - fault 51 stuck-at-0 must produce no model (`UNSAT` / redundant).

   The process test runs both cases and fails on any disagreement before it
   writes `"agree": true`. The gate evidence is intended for
   `reports/sat_smoke.json`; the test writes it under its work directory so a
   clean build does not modify tracked files. The schema is
   `gmax-sat-smoke-1`; the report contains the internal case metadata plus the
   Yosys tool version, proof result, and source label.

5. The G0.2 MiniSat cross-check is pinned to MiniSat 2.2.1 (MIT). The pin is
   `third_party/minisat-2.2.1.lock`, `third_party/minisat-2.2.1.md`, and
   `third_party/licenses/MiniSat-MIT.txt`. `tools/bootstrap-minisat.sh`
   verifies the release archive SHA-256 and builds it into a developer cache;
   `tools/check-minisat.sh` runs an offline SAT/UNSAT probe. MiniSat is a
   test-only subprocess, never linked into `gmax`. The process test passes the
   binary through `GMAX_MINISAT`, solves the same Yosys-dumped CNFs for 168 SA1
   and 51 SA0, and records a `minisat_check` object next to the existing Yosys
   `cross_check`. A disagreement fails the test before `agree: true` is
   written. The default developer run may omit `GMAX_MINISAT`, but
   `GMAX_G0_2_GATE=1` makes the pinned MiniSat binary mandatory: a missing
   MiniSat (or Yosys) fails the test before any Yosys-only report can pass.

6. Disagreement or an unexpected internal status is a test failure, not
   evidence that a fault should be called untestable. Timeouts remain
   incomplete/unknown and are never promoted to a proof.

## Consequences

- ATPG code can add a CaDiCaL or MiniSat adapter later without changing the
  miter, report, or command layers. The internal adapter remains the default
  until a dependency is pinned and justified.
- G0.2's MiniSat agreement is satisfied when the pinned developer cache is
  present. The evidence is `reports/sat_smoke.json` with `cross_check` (Yosys)
  and `minisat_check` (MiniSat) both set to `agree: true`. The default
  developer run skips only the MiniSat leg when `GMAX_MINISAT` is absent and
  prints that skip. With `GMAX_G0_2_GATE=1`, that absence is a hard failure:
  the gate requires both engines and cannot pass on Yosys alone. CI runs the
  gate in the separate `sat-gate` job with the pinned caches; the default test
  matrix remains skip-friendly.
- The MiniSat pin is an engineering pin, not a frozen dependency. TBD-11
  legal/license review must approve MiniSat 2.2.1 (MIT) for internal use and,
  if `gmax` or its evidence is distributed, the notice package. TBD-09 must
  confirm the build branch. Until then the source and binary stay outside git
  and no configure or build step fetches them. The exact request is recorded in
  `third_party/minisat-2.2.1.md`.
- Yosys, MiniSat, and the internal solver share no code and use independent
  engines; Yosys and MiniSat read the same Yosys-dumped CNF, while the internal
  adapter builds its own clauses. This is stronger than an internal replay and
  weaker than a formally diverse proof portfolio.
- The smoke test depends on the pinned Yosys binary through `GMAX_YOSYS` and on
  the pinned MiniSat binary through `GMAX_MINISAT`, like the existing ISCAS'85
  process test. In the default developer run each leg is skipped when its
  binary is absent; with `GMAX_G0_2_GATE=1` both binaries are required. Neither
  mode claims evidence that was not produced.

## References

- `dft-atpg-platform-architecture-grx930-g100.md`, ADR-003
- `dft-atpg-implementation-plan.md`, WP-008 (G0.2) and WP-012
- `src/core/atpg/sat.h`, `src/core/atpg/sat.cpp`
- `tests/process/sat_smoke.cmake`, `tests/process/sat_smoke.py`
- `third_party/README.md`, TBD-11
- `third_party/minisat-2.2.1.lock`, `third_party/minisat-2.2.1.md`
- `tools/bootstrap-minisat.sh`, `tools/check-minisat.sh`
