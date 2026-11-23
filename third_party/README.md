# third_party

No dependency is frozen yet. Architecture 19.5 requires every dependency to be
vendored or pinned by commit hash, with no floating `latest`, and a clean build
to succeed offline once the cache is populated.

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

While the freeze is open:

- nothing is fetched during configure or build;
- a manual bootstrap may populate a cache, but CI and the build do not depend on
  it until TBD-09/TBD-11 are recorded;
- `tests/unit/version_test.cpp` stays framework-free so ctest has a test without
  a dependency;
- a dependency lands in git only with its commit hash, its license, and the ADR
  or package that introduced it.
