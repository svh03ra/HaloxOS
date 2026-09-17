#!/usr/bin/env bash
# Auto-fit boot-requirement generator (see Makefile's ram_requirement rule).
# Usage: gen_rr.sh <kernel.bin> <output.h> <embed_state: 0|1>
#
# gate_bytes = info_addr + one 64 KiB guard: the loader handoff block plus
# the deepest allocation the loader makes above its own image (the 64 KiB-
# aligned compressed-kernel staging window, which it places just below the
# info block). The gate is purely the real footprint - no hardcoded floor -
# so a 5.34 MB kernel boots a 6 MB machine and the requirement always
# follows the kernel size.
set -eu

KERNEL=$1
OUT=$2
EMBED=$3

mem_end=$(nm "$KERNEL" | awk '/ B __bss_end$/ {print $1}')
if [ -z "$mem_end" ]; then
    echo "gen_rr.sh: could not find __bss_end in $KERNEL" >&2
    exit 1
fi
mem_bytes=$(python3 -c "print(int('$mem_end', 16))")

if [ "$EMBED" = "1" ]; then
    info_addr=$((22 * 1048576))
    gate_bytes=$((22 * 1048576))
else
    info_addr=$(python3 -c "print((($mem_bytes + 0xFFFF) & ~0xFFFF) + 0x10000)")
    gate_bytes=$((info_addr + 0x10000))
fi
req_mb=$(( (gate_bytes + 1048575) / 1048576 ))
req_bytes=$((req_mb * 1048576))

{
    echo '#ifndef HALOXOS_RAM_REQUIREMENT_H'
    echo '#define HALOXOS_RAM_REQUIREMENT_H'
    echo ''
    echo "/* Auto-fit boot requirement, generated from the kernel's real span. */"
    echo "#define HALOXOS_KERNEL_MEMORY_BYTES ${mem_bytes}u"
    echo "#define HALOXOS_LOADER_INFO_ADDR ${info_addr}u"
    echo "#define HALOXOS_KERNEL_RAM_REQUIRED_MB ${req_mb}u"
    echo "#define HALOXOS_KERNEL_RAM_REQUIRED_BYTES ${req_bytes}u"
    echo "#define HALOXOS_KERNEL_RAM_GATE_BYTES ${gate_bytes}u"
    echo ''
    echo '#endif'
} > "$OUT.tmp"
if [ -f "$OUT" ] && cmp -s "$OUT.tmp" "$OUT"; then
    rm -f "$OUT.tmp"
else
    mv -f "$OUT.tmp" "$OUT"
fi

printf '[OK] Auto-fit RAM gate: kernel span %.2f MB, info block 0x%X, boot floor %d MB\n' \
    "$(python3 -c "print($mem_bytes/1048576)")" "$info_addr" "$req_mb"
