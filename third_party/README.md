# third_party

Empty on purpose. Architecture 19.5 requires every dependency to be vendored or
pinned by commit hash, with no floating `latest`, and a clean build to succeed
offline once the cache is populated.

The freeze is blocked by **TBD-11 (legal/license review, needed by M0)** and is
sequenced with **TBD-09 (build vs buy)**, because a buy decision leaves only the
WP-001..WP-010 subset standing. Until both are recorded:

- nothing is fetched during configure or build;
- `tests/unit/version_test.cpp` is framework-free so ctest has a test without one;
- a dependency lands here only with its commit hash, its license, and the ADR or
  package that introduced it.
