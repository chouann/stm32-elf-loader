#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
APP_DIR="$SCRIPT_DIR/apps"
NPROC=$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)

# Rebuild apps and regenerate embedded ELF headers (apps/Makefile emits
# static const ... __attribute__((aligned(4))) headers into Core/Inc/)
echo "=== Building apps ==="
make -C "$APP_DIR" clean
make -C "$APP_DIR" -j"$NPROC"

# Build firmware
echo "=== Building firmware ==="
make -C "$SCRIPT_DIR" -j"$NPROC"

# Flash
echo "=== Flashing ==="
openocd -f board/stm32f4discovery.cfg \
    -c "program $SCRIPT_DIR/build/elf_loader.elf verify reset exit"

echo "=== Done ==="
