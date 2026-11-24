#!/usr/bin/env sh
#
# Offline clean-build check (WP-001, architecture 19.5: "a clean build from a
# fresh checkout must succeed offline once the dependency cache is populated").
#
# Usage: sh tools/check-offline-build.sh [preset] [ref]
#        preset  debug (default), release or asan
#        ref     git ref to build (default HEAD)
#
# The build runs against an export of <ref>, not the working tree, because the
# requirement is about a fresh checkout: untracked files and a warm build
# directory must not be able to make it pass.
#
# Order matters. The download probe runs before the build, because a barrier
# that has quietly stopped working would let the build pass while proving
# nothing. The probe is the check's negative control, not a formality.

set -eu

preset=${1:-debug}
ref=${2:-HEAD}

here=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$here"

tmp=$(mktemp -d "${TMPDIR:-/tmp}/gmax-offline.XXXXXX")
trap 'rm -rf "$tmp"' EXIT INT TERM

mkdir -p "$tmp/src"
git archive --format=tar "$ref" > "$tmp/tree.tar"
tar -xf "$tmp/tree.tar" -C "$tmp/src"

# A network namespace is a real barrier: a downed loopback and no routes. Where
# the kernel refuses unprivileged namespaces (containers often do), fall back to
# one every client here honours: proxies pointed at a closed port, plus CMake's
# own refusal to fetch once it cannot consult a cache.
# GMAX_OFFLINE_ISOLATION=proxy exercises the fallback on a machine that does
# have namespaces, which is how that path gets tested before a CI runner is the
# first thing to use it.
isolation=${GMAX_OFFLINE_ISOLATION:-auto}
case $isolation in
auto)
    if command -v unshare >/dev/null 2>&1 && unshare -rn true 2>/dev/null; then
        isolation=netns
    else
        isolation=proxy
    fi
    ;;
netns)
    if ! command -v unshare >/dev/null 2>&1 || ! unshare -rn true 2>/dev/null; then
        echo "offline-build: netns requested but the kernel refused it" >&2
        exit 2
    fi
    ;;
proxy) ;;
*)
    echo "offline-build: unknown GMAX_OFFLINE_ISOLATION '$isolation'" >&2
    exit 2
    ;;
esac

proxy=http://127.0.0.1:9
http_proxy=$proxy
https_proxy=$proxy
all_proxy=$proxy
HTTP_PROXY=$proxy
HTTPS_PROXY=$proxy
ALL_PROXY=$proxy
no_proxy=
NO_PROXY=
export http_proxy https_proxy all_proxy HTTP_PROXY HTTPS_PROXY ALL_PROXY no_proxy NO_PROXY
export FETCHCONTENT_FULLY_DISCONNECTED=ON
export GIT_TERMINAL_PROMPT=0

run_isolated() {
    if [ "$isolation" = netns ]; then
        unshare -rn "$@"
    else
        "$@"
    fi
}

cat > "$tmp/probe.cmake" <<'PROBE'
file(DOWNLOAD "https://example.com/" "${CMAKE_CURRENT_LIST_DIR}/probe.out"
     TIMEOUT 15 STATUS status)
list(GET status 0 code)
list(GET status 1 reason)
if(code EQUAL 0)
    message(FATAL_ERROR "offline barrier is not live: https://example.com/ downloaded")
endif()
message(STATUS "download refused as expected (${code}: ${reason})")
PROBE

echo "offline-build: ref=$ref preset=$preset isolation=$isolation"
run_isolated cmake -P "$tmp/probe.cmake"

cd "$tmp/src"
run_isolated cmake --preset "$preset"
run_isolated cmake --build "build/$preset" --parallel
run_isolated ctest --test-dir "build/$preset" --output-on-failure
run_isolated "./build/$preset/gmax" --version
# Yosys is not part of the gmax build yet. When its developer cache exists,
# prove the pinned binary and the WP-004 recipe under the same network barrier;
# when it does not, say so instead of implying the dependency was checked. Run
# the check from the exported tree so it uses the ref under test, not the
# working tree's untracked additions.
yosys_prefix=${GMAX_YOSYS_PREFIX:-${GMAX_DEP_CACHE:-${XDG_CACHE_HOME:-$HOME/.cache}/gmax}/yosys/0.71}
if [ ! -x "$yosys_prefix/bin/yosys" ]; then
    echo "offline-build: yosys cache not populated; skipping WP-004 dependency check"
elif [ ! -f "$tmp/src/tools/check-yosys.sh" ]; then
    echo "offline-build: ref $ref has no Yosys check; skipping WP-004 dependency check"
else
    run_isolated sh "$tmp/src/tools/check-yosys.sh" "$yosys_prefix"
fi

# MiniSat is the test-only G0.2 cross-check. It is not linked into gmax either;
# when its developer cache exists, prove the pinned binary under the same
# network barrier, and otherwise say that the check was skipped.
minisat_prefix=${GMAX_MINISAT_PREFIX:-${GMAX_DEP_CACHE:-${XDG_CACHE_HOME:-$HOME/.cache}/gmax}/minisat/2.2.1}
if [ ! -x "$minisat_prefix/bin/minisat" ]; then
    echo "offline-build: minisat cache not populated; skipping G0.2 dependency check"
elif [ ! -f "$tmp/src/tools/check-minisat.sh" ]; then
    echo "offline-build: ref $ref has no MiniSat check; skipping G0.2 dependency check"
else
    run_isolated sh "$tmp/src/tools/check-minisat.sh" "$minisat_prefix"
fi

echo "offline-build: ok"
