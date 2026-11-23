#!/usr/bin/env sh
#
# Populate a developer cache with the pinned Yosys (WP-004).
#
# This is not a configure or build step. It is run once, by hand, when network
# access is available; the rest of the build stays offline. The source archive
# and binary live outside git because TBD-11 has not frozen third_party.
#
# Usage: sh tools/bootstrap-yosys.sh [--check]
#
#   (default) download (if needed), verify, build and install Yosys
#   --check   verify the existing cache and run the smoke recipe; no download
#
# Environment:
#   GMAX_DEP_CACHE     cache root (default ${XDG_CACHE_HOME:-$HOME/.cache}/gmax)
#   GMAX_YOSYS_PREFIX  installed prefix (default $GMAX_DEP_CACHE/yosys/<version>)
#   GMAX_BUILD_JOBS    parallel build jobs (default detected CPU count)

set -eu

here=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
. "$here/third_party/yosys-0.71.lock"

mode=bootstrap
case ${1:-} in
"") ;;
--check) mode=check ;;
*)
    echo "usage: sh tools/bootstrap-yosys.sh [--check]" >&2
    exit 2
    ;;
esac

cache_root=${GMAX_DEP_CACHE:-${XDG_CACHE_HOME:-$HOME/.cache}/gmax}
prefix=${GMAX_YOSYS_PREFIX:-$cache_root/yosys/$YOSYS_VERSION}
bin=$prefix/bin/yosys

if [ "$mode" = check ]; then
    sh "$here/tools/check-yosys.sh" "$prefix"
    exit 0
fi

echo "bootstrap-yosys: TBD-09/TBD-11 are still open; this is a developer cache, not a frozen dependency" >&2

command -v cmake >/dev/null 2>&1 || {
    echo "bootstrap-yosys: cmake 3.20 or newer is required on PATH" >&2
    exit 2
}
command -v curl >/dev/null 2>&1 || {
    echo "bootstrap-yosys: curl is required" >&2
    exit 2
}
command -v tar >/dev/null 2>&1 || {
    echo "bootstrap-yosys: tar is required" >&2
    exit 2
}

hash_file() {
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$1" | awk '{print $1}'
    elif command -v shasum >/dev/null 2>&1; then
        shasum -a 256 "$1" | awk '{print $1}'
    else
        echo "bootstrap-yosys: no sha256sum or shasum available" >&2
        return 2
    fi
}

archive=$cache_root/download/yosys-$YOSYS_VERSION.tar.gz
src=$cache_root/src/yosys-$YOSYS_VERSION
build=$cache_root/build/yosys-$YOSYS_VERSION

mkdir -p "$cache_root/download" "$cache_root/src" "$cache_root/build"

if [ ! -f "$archive" ]; then
    echo "bootstrap-yosys: downloading $YOSYS_SOURCE_URL"
    curl -fL --retry 3 --retry-delay 2 -o "$archive.part" "$YOSYS_SOURCE_URL"
    mv "$archive.part" "$archive"
fi

actual_bytes=$(wc -c < "$archive" | tr -d ' ')
if [ "$actual_bytes" != "$YOSYS_SOURCE_BYTES" ]; then
    echo "bootstrap-yosys: source size mismatch" >&2
    echo "  expected $YOSYS_SOURCE_BYTES bytes" >&2
    echo "  actual   $actual_bytes bytes" >&2
    rm -f "$archive"
    echo "bootstrap-yosys: removed the bad archive; rerun to download it again" >&2
    exit 1
fi

actual=$(hash_file "$archive")
if [ "$actual" != "$YOSYS_SOURCE_SHA256" ]; then
    echo "bootstrap-yosys: source hash mismatch" >&2
    echo "  expected $YOSYS_SOURCE_SHA256" >&2
    echo "  actual   $actual" >&2
    rm -f "$archive"
    echo "bootstrap-yosys: removed the bad archive; rerun to download it again" >&2
    exit 1
fi

if [ ! -x "$bin" ]; then
    echo "bootstrap-yosys: building $YOSYS_VERSION from the verified archive"
    rm -rf "$src" "$build"
    mkdir -p "$src"
    tar -xzf "$archive" -C "$src"

    jobs=${GMAX_BUILD_JOBS:-}
    if [ -z "$jobs" ]; then
        jobs=$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)
    fi

    # The archive is self-contained. Make CMake fail rather than fetch if a
    # future option tries to reach the network during configure or build.
    export FETCHCONTENT_FULLY_DISCONNECTED=ON

    # YOSYS_CMAKE_ARGS is a space-separated list from the lock file.
    # shellcheck disable=SC2086
    cmake -S "$src" -B "$build" $YOSYS_CMAKE_ARGS -DCMAKE_INSTALL_PREFIX="$prefix"
    cmake --build "$build" --parallel "$jobs"
    cmake --install "$build"
fi

sh "$here/tools/check-yosys.sh" "$prefix"
echo "bootstrap-yosys: installed $bin"
