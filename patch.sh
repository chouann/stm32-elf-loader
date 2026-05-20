#!/bin/bash
# Run this after CubeMX "Generate Code" to restore our customizations
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
MAKEFILE="$SCRIPT_DIR/Makefile"
CONFIG="$SCRIPT_DIR/Core/Inc/FreeRTOSConfig.h"
MAIN_H="$SCRIPT_DIR/Core/Inc/main.h"
MAIN_C="$SCRIPT_DIR/Core/Src/main.c"
HAL_MSP="$SCRIPT_DIR/Core/Src/stm32f4xx_hal_msp.c"
FFCONF="$SCRIPT_DIR/FATFS/Target/ffconf.h"

if [[ "$OSTYPE" == "darwin"* ]]; then
    SED_FLAG=(-i '')
else
    SED_FLAG=(-i)
fi

# 1. Add our source files to Makefile
if ! grep -q "Core/Src/elf_loader.c" "$MAKEFILE"; then
    sed "${SED_FLAG[@]}" '/Core\/Src\/syscalls.c/a\
Core/Src/elf_loader.c \\\
Core/Src/kernel_api.c \\\
Core/Src/task_manager.c \\\
Core/Src/shell.c \\\
Core/Src/shell_cmds.c \\\
Core/Src/app_registry.c \\\
Core/Src/sd_card.c \\\
Core/Src/wifi.c \\' "$MAKEFILE"
    echo "[OK] Added custom source files to Makefile"
else
    echo "[SKIP] Custom source files already in Makefile"
fi

# 2. Add FATFS SPI diskio to Makefile
if ! grep -q "user_diskio_spi.c" "$MAKEFILE"; then
    sed "${SED_FLAG[@]}" '/FATFS\/Target\/user_diskio.c/a\
FATFS/Target/user_diskio_spi.c \\' "$MAKEFILE"
    echo "[OK] Added user_diskio_spi.c to Makefile"
else
    echo "[SKIP] user_diskio_spi.c already in Makefile"
fi

# 3. Update FreeRTOS heap size to 64KB
if grep -q "configTOTAL_HEAP_SIZE" "$CONFIG"; then
    sed "${SED_FLAG[@]}" -E 's/configTOTAL_HEAP_SIZE[[:space:]]+\(\(size_t\)[0-9]+\)/configTOTAL_HEAP_SIZE                    ((size_t)(64 * 1024))/' "$CONFIG"
    echo "[OK] Updated configTOTAL_HEAP_SIZE to 64KB"
else
    echo "[SKIP] Heap size not found"
fi

# 4. Add SD_CS pin define to main.h
if ! grep -q "SD_CS_Pin" "$MAIN_H"; then
    sed "${SED_FLAG[@]}" '/#define BOOT1_Pin/i\
#define SD_CS_Pin GPIO_PIN_1\
#define SD_CS_GPIO_Port GPIOB' "$MAIN_H"
    echo "[OK] Added SD_CS pin defines to main.h"
else
    echo "[SKIP] SD_CS already in main.h"
fi

# 5. Remove PDM_OUT (conflicts with SPI2_MOSI on PC3)
if grep -q "PDM_OUT_Pin" "$MAIN_H"; then
    sed "${SED_FLAG[@]}" '/PDM_OUT_Pin/d' "$MAIN_H"
    sed "${SED_FLAG[@]}" '/PDM_OUT_GPIO_Port/d' "$MAIN_H"
    echo "[OK] Removed PDM_OUT from main.h"
fi
if grep -q "PDM_OUT_Pin" "$MAIN_C"; then
    sed "${SED_FLAG[@]}" '/PDM_OUT_Pin/,/HAL_GPIO_Init(PDM_OUT_GPIO_Port/d' "$MAIN_C"
    echo "[OK] Removed PDM_OUT GPIO init from main.c"
fi

# 6. Fix SPI2 MOSI: PB15 -> PC3 in hal_msp.c
if grep -q "PB15.*SPI2_MOSI" "$HAL_MSP"; then
    sed "${SED_FLAG[@]}" 's/PB15     ------> SPI2_MOSI/PC3     ------> SPI2_MOSI/' "$HAL_MSP"
    sed "${SED_FLAG[@]}" 's/GPIO_PIN_2;/GPIO_PIN_2|GPIO_PIN_3;/' "$HAL_MSP"
    sed "${SED_FLAG[@]}" 's/GPIO_PIN_13|GPIO_PIN_15/GPIO_PIN_13/' "$HAL_MSP"
    echo "[OK] Fixed SPI2 MOSI pin PB15->PC3 in hal_msp.c"
else
    echo "[SKIP] SPI2 MOSI already correct"
fi

# 7. Add SD_CS GPIO init to main.c
if ! grep -q "SD_CS_Pin" "$MAIN_C"; then
    sed "${SED_FLAG[@]}" '/HAL_GPIO_WritePin(OTG_FS_PowerSwitchOn_GPIO_Port/a\
\
  /*Configure GPIO pin Output Level */\
  HAL_GPIO_WritePin(SD_CS_GPIO_Port, SD_CS_Pin, GPIO_PIN_RESET);' "$MAIN_C"
    sed "${SED_FLAG[@]}" '/HAL_GPIO_Init(OTG_FS_PowerSwitchOn_GPIO_Port/a\
\
  /*Configure GPIO pin : SD_CS_Pin */\
  GPIO_InitStruct.Pin = SD_CS_Pin;\
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;\
  GPIO_InitStruct.Pull = GPIO_NOPULL;\
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;\
  HAL_GPIO_Init(SD_CS_GPIO_Port, \&GPIO_InitStruct);' "$MAIN_C"
    echo "[OK] Added SD_CS GPIO init to main.c"
else
    echo "[SKIP] SD_CS already in main.c"
fi

# 8. Enable FatFs long file name support
if grep -q "_USE_LFN     0" "$FFCONF"; then
    sed "${SED_FLAG[@]}" 's/_USE_LFN     0/_USE_LFN     2/' "$FFCONF"
    echo "[OK] Enabled _USE_LFN=2 in ffconf.h"
else
    echo "[SKIP] _USE_LFN already set"
fi

echo "Done. Now run: make clean && make -j"
