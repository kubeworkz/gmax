#!/usr/bin/env sh
#
# Populate a developer cache with the pinned MiniSat (G0.2).
#
# This is not a configure or build step. It is run once, by hand, when network
# access is available; the rest of the build stays offline. The source archive
# and binary live outside git because TBD-11 has not frozen third_party.
#
# Usage: sh tools/bootstrap-minisat.sh [--check]
#
#   (default) download (if needed), verify, build and install MiniSat
#   --check   verify the existing cache and run the SAT/UNSAT probe; no download
#
# Environment:
#   GMAX_DEP_CACHE      cache root (default ${XDG_CACHE_HOME:-$HOME/.cache}/gmax)
#   GMAX_MINISAT_PREFIX installed prefix (default $GMAX_DEP_CACHE/minisat/<version>)

set -eu

here=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
. "$here/third_party/minisat-2.2.1.lock"

mode=bootstrap
case ${1:-} in
"") ;;
--check) mode=check ;;
*)
    echo "usage: sh tools/bootstrap-minisat.sh [--check]" >&2
    exit 2
    ;;
esac

cache_root=${GMAX_DEP_CACHE:-${XDG_CACHE_HOME:-$HOME/.cache}/gmax}
prefix=${GMAX_MINISAT_PREFIX:-$cache_root/minisat/$MINISAT_VERSION}
bin=$prefix/bin/minisat

if [ "$mode" = check ]; then
    sh "$here/tools/check-minisat.sh" "$prefix"
    exit 0
fi

echo "bootstrap-minisat: TBD-11 is still open; this is a developer cache, not a frozen dependency" >&2

for tool in curl tar make c++; do
    command -v "$tool" >/dev/null 2>&1 || {
        echo "bootstrap-minisat: $tool is required on PATH" >&2
        exit 2
    }
done

hash_file() {
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$1" | awk '{print $1}'
    elif command -v shasum >/dev/null 2>&1; then
        shasum -a 256 "$1" | awk '{print $1}'
    else
        echo "bootstrap-minisat: no sha256sum or shasum available" >&2
        return 2
    fi
}

archive=$cache_root/download/minisat-$MINISAT_VERSION.tar.gz
src=$cache_root/src/minisat-$MINISAT_VERSION

mkdir -p "$cache_root/download" "$cache_root/src"

if [ ! -f "$archive" ]; then
    echo "bootstrap-minisat: downloading $MINISAT_SOURCE_URL"
    curl -fL --retry 3 --retry-delay 2 -o "$archive.part" "$MINISAT_SOURCE_URL"
    mv "$archive.part" "$archive"
fi

actual_bytes=$(wc -c < "$archive" | tr -d ' ')
if [ "$actual_bytes" != "$MINISAT_SOURCE_BYTES" ]; then
    echo "bootstrap-minisat: source size mismatch" >&2
    echo "  expected $MINISAT_SOURCE_BYTES bytes" >&2
    echo "  actual   $actual_bytes bytes" >&2
    rm -f "$archive"
    echo "bootstrap-minisat: removed the bad archive; rerun to download it again" >&2
    exit 1
fi

actual=$(hash_file "$archive")
if [ "$actual" != "$MINISAT_SOURCE_SHA256" ]; then
    echo "bootstrap-minisat: source hash mismatch" >&2
    echo "  expected $MINISAT_SOURCE_SHA256" >&2
    echo "  actual   $actual" >&2
    rm -f "$archive"
    echo "bootstrap-minisat: removed the bad archive; rerun to download it again" >&2
    exit 1
fi

if [ ! -x "$bin" ]; then
    echo "bootstrap-minisat: building $MINISAT_VERSION from the verified archive"
    rm -rf "$src"
    mkdir -p "$src"
    tar -xzf "$archive" -C "$src" --strip-components=1

    # MINISAT_MAKE_ARGS is a space-separated list from the lock file.
    # shellcheck disable=SC2086
    (cd "$src" && make $MINISAT_MAKE_ARGS)

    mkdir -p "$prefix/bin"
    cp "$src/build/release/bin/minisat" "$bin"
    chmod 755 "$bin"
fi

sh "$here/tools/check-minisat.sh" "$prefix"
echo "bootstrap-minisat: installed $bin"
