#include <string.h>

#include "FreeRTOS.h"
#include "task.h"

#include "task_manager.h"

typedef struct {
    uint8_t used;
    TaskHandle_t handle;
    void *memory;
    uint32_t memory_size;
    char name[16];
} DynamicTask_t;

#define TASK_STACK_SIZE 256
#define TASK_DEFAULT_PRIORITY 1

static DynamicTask_t task_table[MAX_DYNAMIC_TASKS];

int task_manager_create(const char *name, const ElfLoaderResult_t *result)
{
    for (int i = 0; i < MAX_DYNAMIC_TASKS; i++) {
        if (task_table[i].used)
            continue;

        TaskHandle_t handle;
        BaseType_t ok = xTaskCreate(result->entry, name, TASK_STACK_SIZE, NULL,
                                    TASK_DEFAULT_PRIORITY, &handle);
        if (ok != pdPASS) {
            vPortFree(result->memory);
            return -1;
        }

        task_table[i].used = 1;
        task_table[i].handle = handle;
        task_table[i].memory = result->memory;
        task_table[i].memory_size = result->memory_size;
        strncpy(task_table[i].name, name, sizeof(task_table[i].name) - 1);
        task_table[i].name[sizeof(task_table[i].name) - 1] = '\0';
        return i;
    }
    vPortFree(result->memory);
    return -1;
}

int task_manager_kill(int id)
{
    if (id < 0 || id >= MAX_DYNAMIC_TASKS || !task_table[id].used)
        return -1;

    vTaskDelete(task_table[id].handle);
    vPortFree(task_table[id].memory);
    memset(&task_table[id], 0, sizeof(DynamicTask_t));
    return 0;
}

int task_manager_count(void)
{
    int count = 0;
    for (int i = 0; i < MAX_DYNAMIC_TASKS; i++) {
        if (task_table[i].used)
            count++;
    }
    return count;
}
