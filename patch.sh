#!/bin/bash
# Run this after CubeMX "Generate Code" to restore our customizations
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
MAKEFILE="$SCRIPT_DIR/Makefile"
CONFIG="$SCRIPT_DIR/Core/Inc/FreeRTOSConfig.h"
FFCONF="$SCRIPT_DIR/FATFS/Target/ffconf.h"

# Detect OS and set sed flags accordingly
if [[ "$OSTYPE" == "darwin"* ]]; then
    SED_FLAG=(-i '') # for mac
else
    SED_FLAG=(-i) # for linux
fi

# 1. Add our source files to Makefile (if not already there)
if ! grep -q "Core/Src/elf_loader.c" "$MAKEFILE"; then
    sed "${SED_FLAG[@]}" '/Core\/Src\/syscalls.c/a\
Core/Src/elf_loader.c \\\
Core/Src/kernel_api.c \\\
Core/Src/task_manager.c \\' "$MAKEFILE"
    echo "[OK] Added elf_loader.c, kernel_api.c, task_manager.c to Makefile"
else
    echo "[SKIP] Source files already in Makefile"
fi

# 2. Update FreeRTOS heap size to 64KB
if grep -q "configTOTAL_HEAP_SIZE" "$CONFIG"; then
    sed "${SED_FLAG[@]}" -E 's/configTOTAL_HEAP_SIZE[[:space:]]+\(\(size_t\)[0-9]+\)/configTOTAL_HEAP_SIZE                    ((size_t)(64 * 1024))/' "$CONFIG"
    echo "[OK] Updated configTOTAL_HEAP_SIZE to 64KB"
else
    echo "[SKIP] Heap size not found"
fi

# 3. Add FATFS related source files (check each and add if missing)
if ! grep -q "Core/Src/myprintf.c" "$MAKEFILE"; then
    sed "${SED_FLAG[@]}" '/Core\/Src\/task_manager.c/a\
Core/Src/myprintf.c \\\
Core/Src/File_Handling.c \\\
Core/Src/elf_rw.c \\\
FATFS/Target/user_diskio_spi.c \\\
Middlewares/Third_Party/FatFs/src/option/unicode.c \\' "$MAKEFILE"
    echo "[OK] Added myprintf.c, File_Handling.c, elf_rw.c, user_diskio_spi.c, unicode.c"
else
    echo "[SKIP] FATFS related source files already in Makefile"
fi

# 4. Enable FatFS Long File Names (LFN) support
if grep -q "_USE_LFN" "$FFCONF"; then
    sed "${SED_FLAG[@]}" 's/_USE_LFN[[:space:]]*0/_USE_LFN     2/' "$FFCONF"
    echo "[OK] Enabled LFN in FatFS configuration"
else
    echo "[SKIP] LFN not found in ffconf.h"
fi

# 5. Add WiFi related source files
if ! grep -q "Core/Src/wifi.c" "$MAKEFILE"; then
    sed "${SED_FLAG[@]}" '/Core\/Src\/elf_rw.c/a\
Core/Src/wifi.c \\' "$MAKEFILE"
    echo "[OK] Added wifi.c to Makefile"
else
    echo "[SKIP] wifi.c already in Makefile"
fi

echo "Done. Now run: ./flash.sh"
