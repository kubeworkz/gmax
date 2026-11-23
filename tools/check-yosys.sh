#!/usr/bin/env sh
#
# Offline check for the pinned Yosys cache (WP-004). It does not download
# anything and does not read the network: point it at a populated prefix.
#
# Usage: sh tools/check-yosys.sh [prefix]
#
# `prefix` is the directory containing bin/yosys. It defaults to
# ${GMAX_YOSYS_PREFIX}, then to the cache that tools/bootstrap-yosys.sh fills.

set -eu

here=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
. "$here/third_party/yosys-0.71.lock"

prefix=${1:-${GMAX_YOSYS_PREFIX:-${GMAX_DEP_CACHE:-${XDG_CACHE_HOME:-$HOME/.cache}/gmax}/yosys/$YOSYS_VERSION}}
bin=$prefix/bin/yosys

if [ ! -x "$bin" ]; then
    echo "check-yosys: no executable at $bin" >&2
    echo "check-yosys: run 'sh tools/bootstrap-yosys.sh' first" >&2
    exit 2
fi

version=$("$bin" -V)
case $version in
*"Yosys $YOSYS_VERSION"*) ;;
*)
    echo "check-yosys: expected Yosys $YOSYS_VERSION, got: $version" >&2
    exit 1
    ;;
esac

if ! command -v python3 >/dev/null 2>&1; then
    echo "check-yosys: python3 is required to validate the JSON output" >&2
    exit 2
fi

tmp=$(mktemp -d "${TMPDIR:-/tmp}/gmax-yosys-check.XXXXXX")
trap 'rm -rf "$tmp"' EXIT INT TERM
cp "$here/tests/differential/yosys/smoke.v" "$tmp/smoke.v"

(
    cd "$tmp"
    "$bin" -q -p 'read_verilog smoke.v; hierarchy -check -top smoke; flatten; opt_clean; write_json smoke.json'
)

if [ ! -s "$tmp/smoke.json" ]; then
    echo "check-yosys: write_json produced no output" >&2
    exit 1
fi

python3 - "$tmp/smoke.json" <<'PY'
import json
import sys

with open(sys.argv[1], encoding="utf-8") as stream:
    design = json.load(stream)

module = design["modules"]["smoke"]
ports = set(module["ports"])
expected = {"a", "b", "y"}
if ports != expected:
    raise SystemExit(f"check-yosys: expected ports {sorted(expected)}, got {sorted(ports)}")
PY

echo "check-yosys: ok ($version)"
