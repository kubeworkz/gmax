# ADR-012: SDC attachment, constraint identity, and ATPG enforcement

- Status: Accepted
- Date: 2026-11-24
- Context: WP-110 follow-on; architecture 7.3, 9.7 and 19.1

## Context

WP-110 adds `read_sdc` as a parser and reporter. Before this change it did not
store the parsed constraints in the design database, and `run_atpg` did not
consume them. Before wiring the two together, four decisions need one owner:
how constraints are attached to a database, what identity a constraint set
has, what happens when the SDC reader cannot represent part of the input, and
which constraints the current ATPG engine actually enforces.

The constraints cannot simply be made implicit. Architecture 7.3 requires
unsupported SDC commands to be warnings with command and line information, and
requires that unsupported constraints are never silently dropped. Architecture
9.7 adds clock-group and capture rules that the current combinational stuck-at
engine does not model yet. A design that claims constrained coverage without
recording those limits would be worse than an unconstrained run.

## Decision

### 1. `read_sdc` attachment CLI

The existing inspection form remains valid and unchanged in meaning:

```text
gmax read_sdc <sdc>
```

It parses and reports the file without loading or modifying a design database.

Constraints are attached to a database through an explicit output path:

```text
gmax read_sdc <sdc> --design <input.dfdb> --output <output.dfdb>
```

The following rules apply:

1. `--design` and `--output` are supplied together. Either one without the
   other is a usage error.
2. `--output` must differ from `--design`. `read_sdc` never overwrites its
   input database. A caller that wants a replacement path must choose a
   separate output and perform the replacement outside the command.
3. The command parses the SDC, loads the design, resolves and validates the
   constraints against that design, and writes the output only after all
   validation has completed. Parse, resolution or validation errors produce
   exit code 2 (`design`) and no output database.
4. A successful attachment writes a `.dfdb` with a canonical constraints
   section and prints the input SDC path, output database path, resolved
   counts, constraints hash, and completeness status.
5. The report-only form has no design against which to resolve object names.
   It prints parser diagnostics and the canonical hash of what it parsed, but
   it does not claim that names resolve or that the set is attachable.
6. `read_verilog`, `write_faults` and `run_atpg` keep their current positional
   forms. `run_atpg` continues to load one `.dfdb`; callers do not pass a
   separate SDC file at grading time.

### 2. Constraints hash policy

The existing `netlist_hash` remains a netlist identity. It is computed with the
same canonical netlist-only serializer that produced 1.3 files, so existing
values stay byte-for-byte stable after the constraints schema is introduced.
The constraints section and any newer schema header are excluded from that
computation. Attaching or changing constraints must not change `netlist_hash`.
Databases written without constraints may continue to use the 1.3 container
layout, so existing `.faults` files remain valid.

A separate `constraints_hash` is defined as the CRC32 of a canonical semantic
constraint payload. The payload contains, in fixed order:

- the constraint schema version;
- the completeness flag;
- clocks, including names, periods, waveforms, source objects and
  generated-clock relationships;
- clock groups, including relationship kind, group name and member clocks;
- false paths, including from/through/to object references;
- case analysis, including object and binary value;
- input/output delays, including object, clock, min/max and value;
- driving cells and loads, including object and cell/value fields;
- the sorted, deduplicated names of unsupported constructs that made the set
  incomplete.

Canonical ordering is part of the hash contract:

- Top-level fields appear in the order listed above.
- Names are canonical unescaped names compared bytewise, case-sensitively, in
  ascending order.
- Repeated records use these primary sort keys: clocks by clock name;
  generated clocks by generated-clock name; clock groups by relationship kind,
  group name and sorted member clocks; false paths by their from, through and
  to object-name lists, with names sorted within each list; case analysis by
  object name; input/output delays by direction, object name, clock name,
  min/max and value; driving cells and loads by object name and cell/value;
  and unsupported construct names by name after deduplication.
- Numeric values use one canonical decimal encoding, independent of source
  formatting. Remaining ties are broken by the full canonical record.

The payload excludes the source file path, raw file bytes, comments,
whitespace, line and column numbers, resolved database IDs, and diagnostic
message text. Two files have the same canonical hash input, and therefore the
same `constraints_hash`, when their supported constraints and completeness
state match and, for an incomplete set, their sorted unsupported construct
names also match. Checkout path, comments, whitespace and diagnostic text do
not affect the hash. A different unsupported construct name is a different
payload and must not be treated as the same constraint set. The report-only and
attached forms print the same hash for the same file.

The constraints hash and status are recorded in `coverage.json` and
`manifest.json`. The report schema gains the fields rather than overloading the
netlist or fault-list hashes; the schema version is incremented when those
fields land. `constraints_status` is one of `absent`, `complete` or
`incomplete`.

For the initial attachment, the `.dfdb` schema minor is bumped from 1.3 to 1.4
and a new optional constraints section carries the canonical model. Older databases
load with an absent constraint set. The constraints source path may be stored
for provenance but is not part of the hash.

`.faults` remains netlist-only in this change. `write_faults` does not consume
constraints, and the `.faults` header continues to carry `netlist_hash`, not
`constraints_hash`. If a later feature prunes or reorders faults based on
constraints, that is a new `.faults` schema and must carry a constraints hash.

### 3. Incomplete SDC behavior

Warnings do not make a file malformed, but they can make the constraint set
incomplete. The rule is:

1. Syntax errors, malformed supported commands, conflicting case analysis, and
   unresolved or ambiguous supported object references are errors. No attached
   database is written.
2. Unsupported commands, unsupported options that can change meaning, and
   unsupported object-query forms are warnings. They set
   `constraints_status = incomplete` and are retained in the attachment's
   diagnostics. They are never silently discarded.
3. `read_sdc` may still write an incomplete attached database. It exits
   successfully, prints the warnings, and records the incomplete status. This
   preserves the parsed results for inspection and for a later corrected run.
4. `run_atpg` refuses to grade a database whose constraints are `incomplete`.
   It prints the stored warnings and exits with code 2 (`design`) before
   reading faults or writing coverage or a manifest. There is no implicit
   "ignore SDC" mode. A caller must correct the source and reattach, or use a
   database with no constraints section.
5. A database with no constraints section is not incomplete. It is `absent`
   and runs with the current unconstrained behavior. This is the compatibility
   path for existing databases.
6. Constraints that are supported and complete but not enforced by the current
   engine are not "incomplete". They are recorded and identified as
   unenforced in the report, as described below.

The distinction is deliberate: incomplete means the reader knows some input was
not represented; unenforced means the reader represented the constraint but
this ATPG engine does not yet apply it to fault outcomes.

### 4. Exact constraints enforced by `run_atpg`

The current `run_atpg` is a combinational/structural stuck-at engine. It does
not model scan load/unload, capture cycles, launch/capture clock pairs, or
transition faults. Its enforcement scope is therefore limited and explicit:

1. **`set_case_analysis` is enforced.** Each resolved case value is a hard
   binary assignment. In PODEM it is applied after reset and before fault
   activation; in SAT it is added as a unit clause. If the assignment conflicts
   with fault activation, the fault is `untestable` under constraints, not
   `aborted`. If the assignment is itself inconsistent with the design, the
   database is invalid and `run_atpg` exits with code 2.
   For this initial scope, the enforceable targets are exactly the nets that
   resolve to top-level input ports. This includes top-level clock and reset
   inputs: they are ordinary primary inputs to the current combinational
   engine, so a case value on one is a hard primary-input assignment. It does
   not create capture semantics or imply that a clock is pulsed. The separate
   rule that clock groups and capture constraints are not enforced still
   applies.

   Every other target marks the constraint set incomplete and is not applied.
   This includes internal pins and nets, hierarchical pins, DFF/state outputs,
   gate outputs, top-level outputs, and internal clock, reset or other
   state-control nets. If one object query expands to a mixture of enforceable
   and unenforceable targets, the whole set is incomplete; supported targets
   are not silently kept while the rest are dropped.

2. **`create_clock`, `create_generated_clock`, `set_clock_groups` and
   `set_false_path` are persisted and validated but are not applied to
   stuck-at fault outcomes.** They are retained for clock-domain reporting,
   scan DRC, and the later multi-clock capture engine. `run_atpg` records them
   and reports `capture_constraints: not-enforced`; it must not claim that
   clock groups or false paths changed the coverage.

3. **`set_input_delay`, `set_output_delay`, `set_driving_cell` and `set_load`
   are persisted and validated but are not evaluated.** They are timing and
   load annotations; v1 ATPG does not perform timing analysis. Their presence
   does not make a set incomplete and does not change fault results.

4. `run_atpg` reports the constraints hash, status, and enforcement scope in
   coverage and the run manifest. When constraints are `absent`, it records
   `constraints_status: absent`; when complete, it records the hash and the
   enforced set (`case_analysis`); when incomplete, it refuses as above.

5. The current behavior remains unchanged for databases without constraints:
   no SDC attachment, no new exit conditions, and no change to existing
   `netlist_hash` or `.faults` compatibility.

Enforcement of clock groups and false paths is deferred to the scan/multi-clock
capture work (architecture 9.7, WP-201). That work must amend this ADR or add a
successor ADR before claiming constrained capture coverage; it cannot change
the meaning of `constraints_hash` silently.

## Consequences

- `read_sdc` becomes useful as an explicit design-transform step, but never
  overwrites a database and never attaches unresolved constraints.
- Netlist identity stays stable. Existing `.faults` and existing unconstrained
  runs keep their hashes; constraints get their own identity and report fields.
- Incomplete SDC is inspectable but cannot silently weaken an ATPG run. The
  cost is that a file containing an unsupported command must be corrected or
  explicitly replaced with a complete supported file before grading.
- Coverage is honest about scope: case-analysis effects are real, while clock
  groups, false paths and timing annotations are recorded but not enforced
  until the capture engine exists.
- The future scan/multi-clock implementation has a fixed attachment format,
  validation boundary, and hash policy to build against.

## Test obligations

- CLI tests for report-only, attach, missing/paired options, input-output path
  collision, unresolved references, and no output on error.
- Schema and hash tests for 1.3 compatibility, 1.4 round trips, empty versus
  absent constraints, canonical ordering, and unchanged `netlist_hash`.
- A hash test with two incomplete sets that have identical supported
  constraints and completeness state but different unsupported construct
  names; their hashes must differ. A companion test uses the same sets with
  identical sorted unsupported names but different source ordering,
  formatting and path; their hashes must match.
- Resolver tests for ports, pins, clocks, generated clocks, groups, false
  paths, and case analysis. Case analysis must be enforceable on every net
  that resolves to a top-level input port, including clock and reset inputs;
  it must mark the set incomplete for internal
  pins/nets, hierarchical pins, DFF/state outputs, gate outputs, top-level
  outputs, and internal clock/reset/state-control nets. A query that expands
  to both enforceable and unenforceable targets must mark the whole set
  incomplete.
- PODEM and SAT tests for enforceable top-level input case assignments,
  including clock and reset inputs, assignment conflicts, constrained
  untestability, mixed-query incompleteness, refusal of incomplete constraints
  by `run_atpg`, and unchanged results with absent constraints.
- Report tests for constraints hash, status, enforcement metadata, and schema
  version.
- A process test that attaches constraints, grades a constrained design, and
  proves that a repeated run is byte-identical; a companion test proves that
  incomplete constraints are refused without reports.

## References

- `dft-atpg-platform-architecture-grx930-g100.md`, sections 6.6, 7.3, 9.7,
  12.1 and 19.1
- `dft-atpg-implementation-plan.md`, WP-110, WP-201, section 15
- `dft-atpg-platform-architecture-grx930-g100.md`, ADR-004 (versioned design
  database schema policy)
- `docs/adr/ADR-010-pattern-ir.md` (canonical hash precedent)
- `docs/adr/ADR-011-deterministic-threading.md` (deterministic identity and
  report comparison)
- `src/core/frontend/sdc.h`, `src/core/frontend/sdc.cpp`
- `src/core/db/db.h`, `src/core/db/dfdb.h`, `src/core/db/dfdb.cpp`
- `src/core/atpg/podem.h`, `src/core/atpg/sat.h`
- `src/shell/commands.cpp`, `src/core/report/report.h`
