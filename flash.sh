#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
APP_DIR="$SCRIPT_DIR/apps"
NPROC=$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)

# Rebuild apps
echo "=== Building apps ==="
make -C "$APP_DIR" clean
make -C "$APP_DIR" -j"$NPROC"

# Regenerate embedded ELF headers from all .o files
echo "=== Embedding apps ==="
cd "$APP_DIR"
for obj in *.o; do
    app="${obj%.o}"
    xxd -i "$obj" | sed "s/${app}_o/${app}_elf/g; s/unsigned char/const unsigned char/; s/unsigned int/const unsigned int/" \
        > "$SCRIPT_DIR/Core/Inc/${app}_elf.h"
    echo "  ${obj} → ${app}_elf.h"
done

# Build firmware
echo "=== Building firmware ==="
make -C "$SCRIPT_DIR" -j"$NPROC"

# Flash
echo "=== Flashing ==="
openocd -f board/stm32f4discovery.cfg \
    -c "program $SCRIPT_DIR/build/elf_loader.elf verify reset exit"

echo "=== Done ==="
