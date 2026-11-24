#!/usr/bin/env sh
#
# Offline check for the pinned MiniSat cache (G0.2). It does not download
# anything and does not read the network: point it at a populated prefix.
#
# Usage: sh tools/check-minisat.sh [prefix]
#
# `prefix` is the directory containing bin/minisat. It defaults to
# ${GMAX_MINISAT_PREFIX}, then to the cache that tools/bootstrap-minisat.sh
# fills.

set -eu

here=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
. "$here/third_party/minisat-2.2.1.lock"

prefix=${1:-${GMAX_MINISAT_PREFIX:-${GMAX_DEP_CACHE:-${XDG_CACHE_HOME:-$HOME/.cache}/gmax}/minisat/$MINISAT_VERSION}}
bin=$prefix/bin/minisat

if [ ! -x "$bin" ]; then
    echo "check-minisat: no executable at $bin" >&2
    echo "check-minisat: run 'sh tools/bootstrap-minisat.sh' first" >&2
    exit 2
fi

help=$("$bin" --help 2>&1 || true)
case $help in
*"USAGE:"*) ;;
*)
    echo "check-minisat: $bin does not look like MiniSat" >&2
    exit 1
    ;;
esac

tmp=$(mktemp -d "${TMPDIR:-/tmp}/gmax-minisat-check.XXXXXX")
trap 'rm -rf "$tmp"' EXIT INT TERM

cat > "$tmp/sat.cnf" <<'CNF'
p cnf 2 2
1 0
-1 2 0
CNF
cat > "$tmp/unsat.cnf" <<'CNF'
p cnf 1 2
1 0
-1 0
CNF

if "$bin" "$tmp/sat.cnf" > "$tmp/sat.out" 2>&1; then
    sat_rc=0
else
    sat_rc=$?
fi
if "$bin" "$tmp/unsat.cnf" > "$tmp/unsat.out" 2>&1; then
    unsat_rc=0
else
    unsat_rc=$?
fi

if [ "$sat_rc" != 10 ] || ! grep -qx "SATISFIABLE" "$tmp/sat.out"; then
    echo "check-minisat: expected SAT exit 10 / SATISFIABLE, got exit $sat_rc" >&2
    cat "$tmp/sat.out" >&2
    exit 1
fi
if [ "$unsat_rc" != 20 ] || ! grep -qx "UNSATISFIABLE" "$tmp/unsat.out"; then
    echo "check-minisat: expected UNSAT exit 20 / UNSATISFIABLE, got exit $unsat_rc" >&2
    cat "$tmp/unsat.out" >&2
    exit 1
fi

echo "check-minisat: ok (MiniSat $MINISAT_VERSION)"
