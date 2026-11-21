#!/usr/bin/env bash
# Fixed difficulty benchmark for the shipped five-chapter campaign, intrinsic-1.
# The difficulty table is printed on stdout when all runs finish; progress goes to stderr.
# Run from anywhere: ./run-intrinsic-1-benchmark.sh [--times 3] [--seed 7] ...
set -euo pipefail
cd "$(dirname "$0")"

BIN=build-play/bin/kot_aiplay
if [[ ! -x "$BIN" ]]; then
    echo "run-intrinsic-1-benchmark: building kot_aiplay with 'make play'..." >&2
    make play
fi

if (( $# )); then
    "$BIN" --campaign intrinsic-1 "$@"
else
    "$BIN" --campaign intrinsic-1 --times 200 --seed 1 --profiles simple
fi
