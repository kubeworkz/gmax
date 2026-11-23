#!/usr/bin/env sh
#
# clang-format gate (WP-001). CI pins clang-format 18.1.8, so pin it locally:
#
#   python -m venv build/tools/venv
#   build/tools/venv/bin/python -m pip install clang-format==18.1.8
#   CLANG_FORMAT=build/tools/venv/bin/clang-format sh tools/check-format.sh
#
# Usage: sh tools/check-format.sh [--fix]
#
# Tracked files are checked, plus untracked files that git does not ignore. CI
# runs in a clean checkout, where those sets are the same; locally the gate also
# sees a new source file from the moment it exists, not only once it is staged.

set -eu

here=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$here"

format=${CLANG_FORMAT:-clang-format}
if ! command -v "$format" >/dev/null 2>&1; then
    echo "check-format: '$format' not found; set CLANG_FORMAT (CI pins 18.1.8)" >&2
    exit 2
fi

sources=$(git ls-files --cached --others --exclude-standard -- '*.cpp' '*.h')
if [ -z "$sources" ]; then
    echo "check-format: no C++ sources found"
    exit 0
fi

if [ "${1:-}" = "--fix" ]; then
    printf '%s\n' "$sources" | tr '\n' '\000' | xargs -0 "$format" -i
    echo "check-format: rewrote sources"
    exit 0
fi

printf '%s\n' "$sources" | tr '\n' '\000' | xargs -0 "$format" --dry-run --Werror
echo "check-format: clean ($("$format" --version))"
