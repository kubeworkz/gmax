# Yosys 0.71 pin — WP-004

**Status:** engineering pin verified; **not frozen**. TBD-09 (build vs buy) and
TBD-11 (legal/license review) remain open. No Yosys source or binary is tracked
in this repository.

## Pin

| Field | Value |
|---|---|
| Version | Yosys 0.71 |
| Release tag | `v0.71` |
| Annotated tag object | `3d96f11b75d8cb521128f55ae9baf6f945837b02` |
| Release commit | `b4c586fd37d0ec61bb38fc1a85fd6a2fde48fcd1` (`Release version 0.71`) |
| Source archive | `https://github.com/YosysHQ/yosys/releases/download/v0.71/yosys.tar.gz` |
| Archive SHA-256 | `2ec7c2ee63bb9b4065d6d5fff2786a1455e834f90ede8eb1d47791153ebef2a5` |
| Archive size | 18,847,330 bytes |
| Embedded archive commit | `5628c13240f670b97aaaca3d47940c4afb8ff572` |
| Source tree | `dfda54af5d9bb47de818337d24e0f770c9456a29` |
| License | ISC (copyright notice in `licenses/Yosys-ISC.txt`) |
| Role | Verilog elaboration and JSON export, subprocess only (ADR-002) |

The release asset is built from merge commit `5628c132…`; its tree is
`dfda54af…`, the same tree as release commit `b4c586f…`. The asset is
self-contained: it vendors ABC and `libs/`, so a build never needs git or a
submodule fetch. The archive hash, not the moving branch, is the authority.

## Why 0.71

- It is the latest stable release as of 2026-11-23. WP-004 uses only the mature
  front-end path (`read_verilog`, `hierarchy`, `flatten`, `opt_clean`,
  `write_json`), not synthesis or plugin APIs; the release's internal changes
  are outside the subprocess/JSON boundary.
- The 0.70 toolchain is the fallback if legal or bring-up rejects 0.71; moving
  the pin is a lock-file change, not a code change.

## Build and feature set

The fixed recipe is in `yosys-0.71.lock`. The minimal configuration is:

- `CMAKE_BUILD_TYPE=Release`;
- ABC disabled: WP-004 elaborates, it does not synthesize or prove equivalence;
- Tcl, readline, editline, libffi, Slang and zlib disabled;
- Yosys unit tests disabled.

This avoids every dependency this machine does not have while keeping the
front-end passes WP-004 needs. ABC can be enabled later from the same verified
archive if the WP-101 equivalence path chooses ABC `cec`; that is a new pin
decision, not a silent change.

The bootstrap script prints the exact `yosys -V` string. The run manifest in
WP-011 records it together with the script hash, so a result names the tool that
produced it. Verification on 2026-11-23 (Ubuntu 24.04 on WSL, GCC 13.3.0) gave:

```
Yosys 0.71+post (git sha1 5628c13240f670b97aaaca3d47940c4afb8ff572, Release, GNU /usr/bin/c++ 13.3.0)
```

## Offline gate

`tools/bootstrap-yosys.sh` populates the developer cache (normally a manual,
one-time step; the CI gate job also invokes it). It downloads only when
invoked, verifies the SHA-256, and builds with the fixed options.
`tools/check-yosys.sh` then runs the WP-004 recipe on
`tests/differential/yosys/smoke.v` and validates the JSON module and ports.

`tools/check-offline-build.sh` runs that check inside its network namespace when
the cache exists. If the cache is absent it prints a skip, because the default
gmax build does not link or execute Yosys. The separate G0.2 SAT smoke gate CI
job bootstraps or restores the pinned Yosys/MiniSat caches and runs the gate
with `GMAX_G0_2_GATE=1`; a missing tool fails that gate rather than passing on
Yosys-only evidence.

## Decisions required

### TBD-09 — build vs buy

**Engineering recommendation: build.** The architecture already selects Yosys
for elaboration and keeps the DB independent (ADR-002). The integration is a
subprocess plus a JSON reader; it does not commit the program to a synthesis
flow. The alternative, buying TMAX, would leave only the WP-001..WP-010 subset
and remove the differentiated ATPG path.

**Decision owner:** Executive + DFT lead. **Needed:** approve the build branch
and record the decision in the plan/architecture; otherwise do not freeze this
pin.

### TBD-11 — legal/license review

**Evidence supplied:** Yosys ISC text and copyright; exact source archive hash;
self-contained archive; list of bundled libraries below. **Legal must confirm**
internal-use attribution and, if the tool is distributed, the notice package.

Bundled components present in the archive (clearance and exact linked set are
part of the review):

- Yosys — ISC;
- ABC — MIT-style (vendored but **not built** by this pin);
- Slang — MIT (vendored but **not built**);
- SymFPU — dual GPL-3.0/BSD-3-Clause; **not linked** by this pin, so no GPL
  election is needed;
- fmt — MIT (Yosys CMake configured `{fmt} 12.2.1`); linked as `libfmt.a`;
- bigint — public domain; sha1 — public domain; json11 — MIT; minisat — MIT;
- ezSAT and SubCircuit are Yosys-authored and have no separate license file;
  confirm they are covered by the Yosys ISC notice.

The observed link line for this build pulls object code from `libs/bigint`,
`libs/ezsat`, `libs/json11`, `libs/minisat`, `libs/sha1` and
`libs/subcircuit`, and links `libfmt.a`; the techlibs are Yosys's own. ABC,
Slang, SymFPU and FST are not linked because they were disabled or are unused.

**Decision owner:** Legal. **Needed:** approve or reject the pin for internal
use and distribution. Internal-use approval with attribution is the minimum to
unblock WP-004; external distribution can remain a separate gate. The pin stays
outside git until this is recorded.

## WP-004 entry criteria

- `sh tools/bootstrap-yosys.sh` succeeds on the developer machine;
- `sh tools/check-yosys.sh` passes with networking disabled;
- the pin and its build options are unchanged from `yosys-0.71.lock`;
- the exact `yosys -V` string is carried into the run manifest when WP-004/WP-011
  records the stage.
