#pragma once

#include <stdint.h>
#include "FreeRTOS.h"
#include "task.h"

typedef struct {
    TaskFunction_t entry;
    void *memory;
    uint32_t memory_size;
} ElfLoaderResult_t;

ElfLoaderResult_t elf_loader_load(const uint8_t *elf_data, uint32_t elf_size);
