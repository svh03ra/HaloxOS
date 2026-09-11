#!/bin/bash
# BSS / RAM audit for the HaloxOS kernel
cd /mnt/c/Users/Test/Documents/HaloxOS
OBJ=build/kernel.o

echo "=== section sizes ==="
size $OBJ

echo ""
echo "=== top BSS consumers ==="
nm -S --size-sort $OBJ 2>/dev/null | grep ' [bB] ' | tail -45 | awk '{printf "%8.0f KB  %s\n", $2/1024, $4}'

echo ""
echo "=== total BSS ==="
nm -S $OBJ 2>/dev/null | grep ' [bB] ' | awk '{s+=$2} END {printf "%.0f KB\n", s/1024}'

echo ""
echo "=== large .data / .rodata consumers (flashed, not RAM) ==="
nm -S --size-sort $OBJ 2>/dev/null | grep ' [dDrR] ' | tail -15 | awk '{printf "%8.0f KB  %s\n", $2/1024, $4}'

echo ""
echo "=== end of kernel (link addr 2M + dec) ==="
python3 - <<'EOF'
import subprocess
out = subprocess.run(['size', 'build/kernel.o'], capture_output=True, text=True).stdout
line = [l for l in out.splitlines() if l.strip().endswith('build/kernel.o')][0].split()
text, data, bss = int(line[0]), int(line[1]), int(line[2])
mem = 2*1024*1024 + text + data + bss
print(f"kernel ends at {mem/1024/1024:.2f} MB (text {text/1024:.0f}K + data {data/1024:.0f}K + bss {bss/1024:.0f}K above 2M base)")
EOF
