# ADR-011: Deterministic threading and merge order

- Status: Accepted
- Date: 2026-11-24
- Context: WP-012 / G0.6, with WP-011 and G0.3 as the implementing package and
  gate criterion

## Context

G0.3 requires the same flow run with 1 thread and with N threads to produce
identical pattern and coverage hashes, and G0.4 makes a deterministic-hash test
part of the commit gate. Today the flow is single-threaded: `run_atpg` has no
thread option, `manifest.json` records `"threads":1,"seed":0`, and the word
engine in `src/core/sim/fault.h` is 64-fault parallel within a word, not
across workers. Nothing in P0 starts a thread, so G0.3 has no N-thread run to
compare against yet; only repeat-run equality is evidenced, by the c499 re-run
in `tests/process/iscas85.cmake`.

Architecture ADR-005 decides the model — a `std::thread` pool with a bounded
work queue and per-worker state, results merged by fault ID or pattern ID
rather than completion order — and 10.3 states why: fault groups are
independent, each worker owns good/faulty state, and the union is
order-independent. The implementation decision left open is the work unit, the
merge point, and which nondeterminism sits outside the guarantee. Writing those
after the pool exists would make the hash test a description of whatever the
scheduler happened to do.

## Decision

1. The threading model is a fixed-size `std::thread` pool fed by a bounded
   queue. Worker count is an explicit run option, `--threads N`, defaulting to
   1. N-thread execution is opt-in in P0 and becomes the default only once G0.3
   evidence exists. `manifest.json` records the worker count actually used,
   replacing the hardcoded `"threads":1`.

2. The work unit is a fixed 64-fault chunk in fault-list order — the same
   chunking `src/core/sim/fault.h` already uses for the word engine, so a
   fault's lane stays its position in its chunk. Chunks are enumerated
   deterministically and handed out by index; no worker identity, thread ID or
   completion order enters the result.

3. Each worker owns its good/faulty state. Results are written to a dense
   array indexed by fault ID, so writes from different workers are disjoint by
   construction and the union is an order-independent assignment, not a
   reduction. Coverage counts, the aborted count and the exit status are
   computed from that array in fault-ID order.

4. Pattern generation is parallelized by fault bucket, as architecture 10.3
   allows. Candidate patterns are merged and sorted before compaction by
   (fault ID, generation ordinal); compaction is single-threaded in P0. A
   parallel compactor needs its own deterministic reduction and is not assumed
   by this ADR.

5. The seed is a run option recorded in the manifest, defaulting to 0. Where a
   bucket's fault order is shuffled to avoid pathological structural ordering,
   the shuffle derives from (seed, bucket index) and nothing else — never from
   thread count, wall clock or address. The same seed and budgets produce the
   same candidate order for any N.

6. The hash guarantee applies to runs that complete without a budget abort.
   `max_time_ms` is wall-clock and cannot be deterministic; a time-budget abort
   is recorded (`aborted`, exit code 4) and such a run is not used as
   determinism evidence. Backtrack budgets are deterministic and are the bound
   G0.3's test uses. This is the rule the 1 ms c432 regression already enforces
   by keeping aborted faults out of the SAT closer.

7. What the hash test compares: `coverage.json` and, from WP-106, the pattern
   file hashes. The manifests differ by construction in the recorded thread
   count, so the manifest is compared field by field with `options.threads`
   excluded, not byte for byte. The stage hashes in the manifest must match.

8. Forbidden without an ADR amendment: work stealing, completion-order
   accumulation, atomic-order-dependent reductions, floating-point reductions
   over fault results, iteration over unordered containers in any output path,
   and time-based tie-breaks. A new source of ordering must be added to the
   hash test in the same change set.

9. Failure handling follows the same ordering: a worker error is captured with
   the fault ID it was processing, and the first error in fault-ID order is the
   one reported, so the diagnostic does not depend on which thread noticed it
   first.

## Consequences

- G0.3's evidence is a process test that runs the same c432 flow with
  `--threads 1` and `--threads N` (N > 1) and asserts identical coverage
  hashes; from WP-106 it also compares pattern hashes. G1.6 repeats the test
  with patterns as the primary artifact, and G0.4's commit gate can run the
  small-circuit version.
- P0's default remains 1 thread, so the existing hashes (`413dd789`,
  `424032e8`, …) and the ISCAS'85 expectations do not move when the pool
  lands. Changing the default is a separate, evidenced step.
- Per-worker good/faulty state duplicates the scalar good machine N times.
  That is the cost of independence, and it is bounded by the fault-chunk size,
  not by netlist size.
- Future parallel engines — PPSFP and the GPU path (10.5) — are subject to the
  same test: the CPU bit-parallel engine is the reference, and a kernel that
  cannot reproduce its hashes is not an implementation of this design.
- Wall-clock budgets stay outside the guarantee by design. A determinism
  failure caused by a timeout is a budget diagnosis, not a merge-order bug, and
  the manifest says which one happened.

## References

- `dft-atpg-platform-architecture-grx930-g100.md`, ADR-005, sections 10.1,
  10.3, 10.5, 14.6, 17.1
- `dft-atpg-implementation-plan.md`, WP-009, WP-011, G0.3, G1.6, section 9.2
- `docs/adr/ADR-010-pattern-ir.md` (the pattern hash)
- `src/core/sim/fault.h`, `src/core/sim/fault.cpp`
- `src/core/report/report.cpp` (`manifest_json`), `src/core/report/report.h`
- `tests/process/iscas85.cmake`, `tests/process/run_atpg.cmake`
