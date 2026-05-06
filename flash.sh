#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
APP_DIR="$SCRIPT_DIR/apps"

# Rebuild apps
echo "=== Building apps ==="
make -C "$APP_DIR" clean
make -C "$APP_DIR"

# Regenerate embedded ELF headers
echo "=== Embedding apps ==="
cd "$APP_DIR"
for app in blink_green blink_orange; do
    xxd -i ${app}.o | sed "s/${app}_o/${app}_elf/g; s/unsigned char/const unsigned char/; s/unsigned int/const unsigned int/" \
        > "$SCRIPT_DIR/Core/Inc/${app}_elf.h"
    echo "  ${app}.o → ${app}_elf.h"
done

# Build firmware
echo "=== Building firmware ==="
make -C "$SCRIPT_DIR" clean
make -C "$SCRIPT_DIR"

# Flash
echo "=== Flashing ==="
openocd -f board/stm32f4discovery.cfg \
    -c "program $SCRIPT_DIR/build/elf_loader.elf verify reset exit"

echo "=== Done ==="
