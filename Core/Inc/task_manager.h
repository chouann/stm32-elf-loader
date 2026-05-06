#pragma once

#include <stdint.h>
#include "elf_loader.h"

#define MAX_DYNAMIC_TASKS 4

int task_manager_create(const char *name, const ElfLoaderResult_t *result);
int task_manager_kill(int id);
int task_manager_count(void);
