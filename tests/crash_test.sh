#!/usr/bin/env bash
set -u
HARNESS="$1"
WORKDIR=$(mktemp -d)
trap 'rm -rf "$WORKDIR"' EXIT

N=50
PREFIX_TORN="$WORKDIR/torn"
PREFIX_REDO="$WORKDIR/redo"

echo "=== torn-write crash scenario ==="
"$HARNESS" crash-torn "$PREFIX_TORN" "$N"
"$HARNESS" verify-torn "$PREFIX_TORN" "$N"
STATUS1=$?

echo "=== redo (committed-but-not-yet-applied) crash scenario ==="
"$HARNESS" crash-redo "$PREFIX_REDO" "$N"
"$HARNESS" verify-redo "$PREFIX_REDO" "$N"
STATUS2=$?

if [ "$STATUS1" -eq 0 ] && [ "$STATUS2" -eq 0 ]; then
    echo "crash recovery test passed"
    exit 0
fi
echo "crash recovery test FAILED"
exit 1
