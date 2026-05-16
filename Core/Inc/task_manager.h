#pragma once

#include <stdint.h>
#include "FreeRTOS.h"
#include "task.h"

#define MAX_TASKS 4
#define TASK_NAME_LEN 16
#define TASK_STACK_SIZE 256

typedef struct {
    int id;
    char name[TASK_NAME_LEN];
    uint8_t used;
    uint32_t memory_size;
    uint32_t stack_free;
} task_info_t;

typedef void (*task_cleanup_t)(void);

int task_create(const char *name,
                TaskFunction_t entry,
                task_cleanup_t cleanup,
                void *memory,
                uint32_t memory_size);
int task_kill(int id);
int task_get_info(int id, task_info_t *info);
