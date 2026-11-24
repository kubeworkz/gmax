# third_party

No dependency is frozen yet. Architecture 19.5 requires every dependency to be
vendored or pinned by commit hash (or, when upstream publishes no tag, by a
verified release-archive hash), with no floating `latest`, and a clean build to
succeed offline once the cache is populated.

The freeze is blocked by **TBD-11 (legal/license review, needed by M0)** and is
sequenced with **TBD-09 (build vs buy)**, because a buy decision leaves only the
WP-001..WP-010 subset standing.

## Pinned candidate: Yosys 0.71 (WP-004)

Engineering has selected and verified a Yosys pin so WP-004 can start, but it is
**not frozen and not a tracked dependency**. The pin is recorded in:

- `yosys-0.71.lock` — exact tag/commit/source hash and fixed CMake options;
- `yosys-0.71.md` — rationale, build evidence, and the TBD-09/TBD-11 decisions;
- `licenses/Yosys-ISC.txt` — the upstream ISC notice.

The source archive and binary stay outside git. `tools/bootstrap-yosys.sh`
verifies the archive hash, builds the pin, and installs it into a developer
cache (`${XDG_CACHE_HOME:-$HOME/.cache}/gmax/yosys/0.71` by default).
`tools/check-yosys.sh` runs the WP-004 recipe offline against that cache. The
offline build gate runs the check when the cache exists and states when it is
skipping it.

## Pinned candidate: MiniSat 2.2.1 (G0.2)

G0.2 names MiniSat as the independent small-instance cross-check. Engineering
has verified a MiniSat 2.2.1 pin, but it is **not frozen and not a tracked
dependency**. The pin is recorded in:

- `minisat-2.2.1.lock` — release archive hash, size, license, and fixed build
  arguments;
- `minisat-2.2.1.md` — rationale, build evidence, and the TBD-09/TBD-11
  decisions;
- `licenses/MiniSat-MIT.txt` — the upstream MIT notice.

The source archive and binary stay outside git. `tools/bootstrap-minisat.sh`
verifies the archive hash, builds the pin, and installs it into a developer
cache (`${XDG_CACHE_HOME:-$HOME/.cache}/gmax/minisat/2.2.1` by default).
`tools/check-minisat.sh` runs a small SAT/UNSAT probe offline. The G0.2 process
test uses the binary through `GMAX_MINISAT` and records a `minisat_check`
object next to the Yosys `cross_check` in `reports/sat_smoke.json`; without
`GMAX_MINISAT` the default developer run uses the Yosys leg and says so. Set
`GMAX_G0_2_GATE=1` to make a missing MiniSat a hard gate failure; the gate
cannot pass on Yosys alone.

While the freeze is open:

- nothing is fetched during configure or build;
- the default build and test matrix do not depend on a cache; the separate
  G0.2 SAT smoke gate job bootstraps the pinned Yosys/MiniSat caches (or
  restores them from the Actions cache) and fails when MiniSat is unavailable.
  That is an engineering gate, not a freeze; TBD-09/TBD-11 still govern any
  tracked or frozen dependency;
- `tests/unit/version_test.cpp` stays framework-free so ctest has a test without
  a dependency;
- MiniSat is a test-only subprocess; neither its source nor its binary is
  linked into gmax or tracked in git;
- a dependency lands in git only with its commit or verified archive hash, its
  license, and the ADR or package that introduced it.
