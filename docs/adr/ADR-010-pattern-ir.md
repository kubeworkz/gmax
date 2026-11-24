# ADR-010: Canonical pattern IR and the STIL boundary

- Status: Accepted
- Date: 2026-11-24
- Context: WP-012 / G0.6, with WP-106 as the implementing package

## Context

P0 produces coverage and run manifests; it writes no test patterns yet. The
next package that emits vectors is WP-106, and every consumer after it — the
WP-107 replay check, the WP-204 parity harness, and the WP-211 and WP-215
writers — needs one representation to agree on. Architecture ADR-007 already
decides that the representation is a canonical internal IR with STIL as the
reference format, and architecture 12.1 fixes what the IR carries. The
implementation decision left open is where the boundary sits and what makes
two IRs equal, because G1.5's round-trip criterion and the 1-thread/N-thread
hash test both need an equality defined by the IR rather than by the bytes a
writer happens to emit.

Without that decision the first writer defines the IR by accident: STIL
syntax, field order and vendor extensions leak into ATPG, and the replay and
parity tools each grow a private parser.

## Decision

1. The IR lives in `src/core/pattern/` and is the only pattern type that
   crosses a module boundary. ATPG produces it; the STIL 1.1 writer and
   reader, replay, and the parity harness consume it. No writer-specific field
   or vendor extension is representable in the IR core.

2. The IR holds exactly architecture 12.1's six parts: the signal map (logical
   name, direction, physical pin), timing sets (period, drive edges, strobe,
   clock pulses), cycles (drive, compare, mask, high-Z), scan frames (chain,
   bit order, load/unload), expected values with per-cycle masks, and
   annotations (pattern ID, detected faults, coverage contribution). Field
   semantics are those of section 12, not a new interpretation.

3. Identity rules are part of the IR, not the writer:
   - signal order is the netlist's stable signal order, then name — never map
     iteration or insertion accident;
   - patterns are ordered by pattern ID, and IDs are assigned in deterministic
     ATPG order, not completion order (ADR-011);
   - X is a mask bit, never a substituted 0/1; high-Z is preserved or masked
     and flagged (12.4);
   - capture procedures are explicit per timing set; there is no implicit
     simultaneous capture (12.4).

4. Structural equality is defined over normalized IR fields: signal map,
   timing sets, cycles, scan frames, masks and annotations compared in the
   fixed order above. The round-trip test parses the written STIL back into the
   IR and compares structurally; it does not compare STIL text. Whitespace,
   comments, and the ordering of independent declarations inside a writer are
   outside equality.

5. The IR carries a canonical hash over the same normalized fields. G1.5's
   round-trip evidence and the determinism hash in G0.3 and G1.6 use this hash.
   A change that alters the hash must be explained in the change set that
   makes it, per plan 9.2.

6. Writers and readers are thin over the IR. STIL 1.1 is the first writer over
   the validated subset in 12.2 (`Signals`, `SignalGroups`,
   `Timing`/`WaveformTable`, `Pattern` with waveform-table references,
   `ScanStructures`, and load/unload/capture procedures), with vendor
   extensions behind a flag. WGL (WP-215) and tester cycle files (WP-211) add
   writers; neither may add IR semantics.

7. Scope boundary: an IR field that changes what a pattern means is an
   architecture change under plan section 15, not a WP-106 edit. Annotations
   that do not affect tester semantics are additive.

## Consequences

- WP-106 can implement the IR and the STIL writer against a fixed boundary,
  and WP-107 replay, WP-204 parity and the WP-211/WP-215 writers do not need
  ATPG changes.
- The round-trip criterion is testable before an ATE vendor simulator is
  available: parse-back structural equality is the local gate, and the vendor
  run (12.5 item 3) is the P1 activity engaged on call-off.
- Replay is timing-agnostic (12.5 item 2): the simulator consumes cycles, scan
  frames and masks and ignores timing sets. Timing sets exist for the writer
  and the tester.
- The canonical hash makes pattern changes reviewable. A compaction change that
  alters patterns changes the hash, which is the same mechanism G0.3 uses to
  catch a nondeterministic merge.
- P0 has no pattern output and does not need the IR. G0.3's evidence therefore
  begins as coverage-hash equality; the pattern leg of the hash test is added
  by WP-106 and repeated at G1.6.
- Physical pin mapping is versioned with the IR (12.4). A mapping change alters
  written vectors without changing ATPG, so it is recorded as an input hash in
  the run manifest once WP-106 emits one.

## References

- `dft-atpg-platform-architecture-grx930-g100.md`, ADR-007, sections 12.1–12.5,
  14.4, 17.1
- `dft-atpg-implementation-plan.md`, WP-106, WP-107, WP-204, WP-211, WP-215,
  G1.5, section 9.2
- `docs/adr/ADR-009-sat-integration.md` (G0.6 sibling)
- `docs/adr/ADR-011-deterministic-threading.md` (the pattern/coverage hash)
- `src/core/pattern/`
