#pragma once

#include <stdint.h>
#include "FreeRTOS.h"
#include "task.h"

typedef enum {
    ELF_OK = 0,
    ELF_ERR_NULL_ARG,
    ELF_ERR_TOO_SMALL,
    ELF_ERR_BAD_MAGIC,
    ELF_ERR_BAD_CLASS,
    ELF_ERR_BAD_ENDIAN,
    ELF_ERR_BAD_TYPE,
    ELF_ERR_BAD_MACHINE,
    ELF_ERR_NO_MEMORY,
    ELF_ERR_UNRESOLVED_SYMBOL,
    ELF_ERR_UNSUPPORTED_RELOC,
    ELF_ERR_NO_ENTRY,
} elf_status_t;

const char *elf_status_str(elf_status_t status);

typedef uint32_t (*symbol_resolver_t)(const char *name);

typedef struct {
    TaskFunction_t entry;
    void (*cleanup)(void);
    void *memory;
    uint32_t memory_size;
} elf_load_result_t;

elf_status_t elf_load(const uint8_t *elf_data,
                      uint32_t elf_size,
                      symbol_resolver_t resolver,
                      elf_load_result_t *out);
