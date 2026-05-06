#!/bin/bash
# Run this after CubeMX "Generate Code" to restore our customizations
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
MAKEFILE="$SCRIPT_DIR/Makefile"
CONFIG="$SCRIPT_DIR/Core/Inc/FreeRTOSConfig.h"

# 1. Add our source files to Makefile (if not already there)
if ! grep -q "Core/Src/elf_loader.c" "$MAKEFILE"; then
    sed -i '' '/Core\/Src\/syscalls.c/a\
Core/Src/elf_loader.c \\\
Core/Src/kernel_api.c \\\
Core/Src/task_manager.c \\' "$MAKEFILE"
    echo "[OK] Added elf_loader.c, kernel_api.c, task_manager.c to Makefile"
else
    echo "[SKIP] Source files already in Makefile"
fi

# 2. Update FreeRTOS heap size to 64KB
if grep -q "configTOTAL_HEAP_SIZE.*15360" "$CONFIG"; then
    sed -i '' 's/configTOTAL_HEAP_SIZE.*((size_t)15360)/configTOTAL_HEAP_SIZE                    ((size_t)(64 * 1024))/' "$CONFIG"
    echo "[OK] Updated configTOTAL_HEAP_SIZE to 64KB"
else
    echo "[SKIP] Heap size already updated"
fi

echo "Done. Now run: ./flash.sh"
