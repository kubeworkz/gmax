# MiniSat 2.2.1 pin — G0.2

**Status:** engineering pin verified; **not frozen**. TBD-11 (legal/license
review) remains open, and TBD-09 (build vs buy) still governs the dependency
freeze. No MiniSat source or binary is tracked in this repository.

## Pin

| Field | Value |
|---|---|
| Version | MiniSat 2.2.1 |
| Upstream | https://github.com/niklasso/minisat (project page: https://minisat.se/) |
| Source archive | `https://archive.ubuntu.com/ubuntu/pool/universe/m/minisat2/minisat2_2.2.1.orig.tar.gz` |
| Archive SHA-256 | `e54afa3c192c1753bc8075c0c7e126d5c495d9066e3f90a2588091149ac9ca40` |
| Archive size | 44,229 bytes |
| Source tree SHA-1 (`git write-tree` of the extracted archive) | `c9bcea58c496d1fd9076a761d2b2f90ded1893e8` |
| License | MIT (copyright notice in `licenses/MiniSat-MIT.txt`) |
| Role | Independent G0.2 SAT cross-check, subprocess only (ADR-009) |
| Build recipe | `make r CXXFLAGS=-fpermissive` |

## Pin identity

The upstream GitHub mirror publishes tags for `releases/2.0` and
`releases/2.2.0`, but not 2.2.1. The 2.2.1 release is therefore pinned by the
verified archive hash and its extracted tree hash, not by a tag. The archive is
the `minisat2` Debian source package's `orig.tar.gz`: it contains the upstream
tree and MIT licence, with Debian packaging and patches in a separate
`debian.tar.xz` that this pin does not use. The Ubuntu `minisat` binary package
version `1:2.2.1-8build1` names the same upstream release.

The archive hash is the authority. The tree hash is a second identity check in
case an archive is recompressed without changing its contents.

## Why 2.2.1

- Architecture ADR-003 names MiniSat as the independent small-instance
  cross-check behind `ISatSolver`; this pin supplies exactly that oracle.
- It is small, MIT-licensed, and solves the two c432 CNFs in well under a
  second, so it does not change the smoke-test budget.
- The internal deterministic CDCL adapter remains the P0 production solver.
  MiniSat is only a test subprocess; it is not linked into `gmax`, does not
  enter the ATPG code path, and is not a runtime dependency.

## Build and feature set

GCC 13 defaults to C++17. Upstream's `friend Lit mkLit(Var var, bool sign =
false);` declaration is rejected by that standard, so the fixed recipe adds
`-fpermissive` to the upstream `r` (release) target. The release target also
uses `--static` and therefore needs zlib's static library (`zlib1g-dev` on
Ubuntu). No source changes or Debian patches are applied.

Verification on 2026-11-24 (Ubuntu 24.04 on WSL, GCC 13.3.0) produced a static
`minisat` binary. MiniSat has no version flag; the version is the pin, and the
report records the binary SHA-256 so a different binary is visible in the
evidence. The hash is build-path dependent because the release target keeps
debug info, so it is not a reproducibility claim; the source archive hash is.

## Cross-check integration

`tests/process/sat_smoke.py` runs Yosys `sat -prove diff 0 -dump_cnf` on the
same two-copy c432 miter for 168 SA1 and 51 SA0, then runs MiniSat on each CNF.
MiniSat exit 10 / `SATISFIABLE` must match the internal `sat` verdict; exit 20
/ `UNSATISFIABLE` must match the internal `unsat` verdict. The report keeps the
existing Yosys `cross_check` object and adds:

```json
"minisat_check": {
  "tool": "MiniSat",
  "version": "2.2.1",
  "source_sha256": "e54afa3c...",
  "binary_sha256": "...",
  "cases": [{"net": 168, "stuck": 1, "verdict": "sat", "proof": "SATISFIABLE", "exit_code": 10}],
  "agree": true
}
```

The MiniSat leg runs when `GMAX_MINISAT` points at the developer-cache binary.
The default developer invocation may omit it; it then runs the Yosys leg and
prints that the MiniSat leg was skipped.

The G0.2 gate is stricter. Set `GMAX_G0_2_GATE=1`:

```sh
GMAX_G0_2_GATE=1 \
GMAX_YOSYS=$HOME/.cache/gmax/yosys/0.71/bin/yosys \
GMAX_MINISAT=$HOME/.cache/gmax/minisat/2.2.1/bin/minisat \
ctest --test-dir build/debug -R cli.sat-smoke --output-on-failure
```

With the gate set, an absent MiniSat (or absent Yosys) is a hard failure before
the Yosys-only report can pass. The gate cannot pass on Yosys alone. It never
writes a MiniSat agreement that was not produced. CI runs this gate in the
dedicated `sat-gate` job, which bootstraps or restores the pinned Yosys/MiniSat
caches; the default developer `ctest` invocation does not set the gate and may
skip the MiniSat leg.

## Approval required

### TBD-11 — legal/license review

**Approval requested:** approve MiniSat 2.2.1 under the MIT licence for internal
use as a test-only subprocess, and approve the notice/attribution package if
`gmax` or its evidence is distributed. The evidence supplied is the MIT
copyright text, the exact source archive hash and size, the extracted tree
hash, and the fixed build recipe above. **Decision owner:** Legal. The pin stays
a developer cache and outside git until this approval is recorded.

### TBD-09 — build vs buy

**Approval requested:** confirm the build branch keeps the WP-001..WP-010
subset, including this test-only MiniSat pin. If the buy branch is chosen, the
pin is unnecessary and should be removed. **Decision owner:** Executive + DFT
lead.

## Entry criteria

- `sh tools/bootstrap-minisat.sh` succeeds on the developer machine;
- `sh tools/check-minisat.sh` passes with networking disabled;
- the archive hash and build arguments are unchanged from
  `minisat-2.2.1.lock`;
- `GMAX_MINISAT` points at that cache when producing the G0.2 report.
