#pragma once

#include <stdint.h>

int sd_init(void);
int sd_read_file(const char *path, uint8_t **buf, uint32_t *size);
int sd_rm_file(const char *filepath);
int sd_list_apps(void (*callback)(const char *name, uint32_t size));
int sd_list_all(void (*callback)(const char *name, uint32_t size));
