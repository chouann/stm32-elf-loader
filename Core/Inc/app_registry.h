#pragma once

#include <stdint.h>

typedef struct {
    const char *name;
    const uint8_t *data;
    uint32_t size;
} app_entry_t;

const app_entry_t *app_find(const char *name);
const app_entry_t *app_list(void);
