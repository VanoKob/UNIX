#!/bin/bash

set -e

if [ $# -ne 1 ]; then
    echo "Usage: $0 <output_file>"
    exit 1
fi

OUTFILE="$1"
SIZE=$((4 * 1024 * 1024 + 1)) 

rm -f "$OUTFILE"

truncate -s $SIZE "$OUTFILE"


printf '\x01' | dd of="$OUTFILE" bs=1 count=1 seek=0 conv=notrunc status=none


printf '\x01' | dd of="$OUTFILE" bs=1 count=1 seek=10000 conv=notrunc status=none


printf '\x01' | dd of="$OUTFILE" bs=1 count=1 seek=$((SIZE - 1)) conv=notrunc status=none


if [ ! -f "$OUTFILE" ]; then
    echo "Error: Failed to create $OUTFILE"
    exit 1
fi

actual_size=$(stat -c %s "$OUTFILE" 2>/dev/null || echo "0")
if [ "$actual_size" -ne "$SIZE" ]; then
    echo "Warning: Expected size $SIZE, got $actual_size"
fi

echo "Test file '$OUTFILE' created ($SIZE bytes)"
